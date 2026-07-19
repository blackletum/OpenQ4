// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	Vulkan interaction pass (Phase F1,
	docs/dev/plans/2026-07-19-vulkan-phase-f.md).

	Unshadowed per-light bump/diffuse/specular interactions for every view
	light, drawn between the depth fill and the ambient walks. This is the
	GL behavior for lights without shadow surfaces (stencil ALWAYS +
	interactions) applied to all lights — the Phase E pipelines carry no
	stencil state, and stencil shadows stay Phase G.

	GL-free ports from TUs excluded from the vk module build:
	- RB_ARB2_DrawInteractions light loop (draw_arb2.cpp:11458-11666):
	  skip fog/blend/empty lights; opaque interactions (local then global)
	  additive at depth EQUAL with writes off; translucent at depth LESS.
	- RB_CreateSingleDrawInteractionsFiltered decomposition walk +
	  R_SetDrawInteraction + RB_SubmittInteraction (tr_render.cpp:782-1033):
	  light-stage × surface-stage pairing into drawInteraction_t, with the
	  r_skipBump/Diffuse/Specular substitutions and the both-black skip.
	- RB_DetermineLightScale (tr_render.cpp:675-726).
	- RB_BakeTextureMatrixIntoTexgen (draw_common.cpp:4365-4400).

	Per-draw data rides the shared 128B push block (MVP + vertex-color
	packing + ambient direction); everything else streams through the
	executor's dynamic uniform ring as a std140 block on set 6. The six
	texture slots bind cached per-image single-sampler sets (0=specular
	table, 1=bump, 2=falloff, 3=light projection, 4=diffuse, 5=specular).

===============================================================================
*/

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"

#undef snprintf
#undef vsnprintf
#ifndef INT_MAX
#define INT_MAX		2147483647
#endif
#ifndef INT_MIN
#define INT_MIN		( -2147483647 - 1 )
#endif
#ifndef UINT_MAX
#define UINT_MAX	0xffffffffu
#endif
#include <cstdio>
#include "volk.h"

#include "VulkanDevice.h"
#include "vk_ShadowMap.h"

// vk_GuiExecutor.cpp narrow accessors (vkExec stays file-static there)
VkCommandBuffer VK_Exec_ActiveCmd( void );
int VK_Exec_ActiveFrameSlot( void );
bool VK_Exec_BindTriGeometry( VkCommandBuffer cmd, int slot, const srfTriangles_t *tri );
void VK_Exec_SetSurfScissor( VkCommandBuffer cmd, const viewDef_t *viewDef, const drawSurf_t *drawSurf, int fbHeight );
void VK_BuildSurfMVP( const viewDef_t *viewDef, const drawSurf_t *drawSurf, float outMvp[ 16 ] );
VkPipeline VK_Exec_InteractionPipeline( void );
VkPipelineLayout VK_Exec_InteractionPipelineLayout( void );
VkPipeline VK_Exec_ShadowInteractionPipeline( void );
VkPipelineLayout VK_Exec_ShadowInteractionPipelineLayout( void );
VkDescriptorSet VK_Exec_ShadowDescriptorSet( void );
VkDescriptorSet VK_Exec_ImageDescriptor( unsigned int texnum, bool require2D );
VkDescriptorSet VK_Exec_InteractionUniformSet( void );
int VK_Exec_InteractionUniformAlloc( const void *data, int bytes );

/*
====================
Per-draw GPU blocks
====================
*/

// mirror of the shared 128B push block ({mat4; vec4 a,b,c,d}):
// a = (vertexColorModulate, vertexColorAdd, ambientLight, unused),
// b = tangent-space ambient light direction (cube-quantized), c/d unused
typedef struct vkInteractionPush_s {
	float			mvp[ 16 ];
	float			a[ 4 ];
	float			b[ 4 ];
	float			c[ 4 ];
	float			d[ 4 ];
} vkInteractionPush_t;

// std140 mirror of the set-6 InteractionBlock (14 vec4 = 224 bytes,
// inside the 256B ring slice)
typedef struct vkInteractionBlock_s {
	float			localLightOrigin[ 4 ];
	float			localViewOrigin[ 4 ];
	float			lightProjectionS[ 4 ];
	float			lightProjectionT[ 4 ];
	float			lightProjectionQ[ 4 ];
	float			lightFalloffS[ 4 ];
	float			bumpMatrixS[ 4 ];
	float			bumpMatrixT[ 4 ];
	float			diffuseMatrixS[ 4 ];
	float			diffuseMatrixT[ 4 ];
	float			specularMatrixS[ 4 ];
	float			specularMatrixT[ 4 ];
	float			diffuseColor[ 4 ];
	float			specularColor[ 4 ];
} vkInteractionBlock_t;

// std140 mirror of the set-7 ShadowBlock (7 vec4 = 112 bytes, its own
// 256B ring slice; rewritten per space — the rows are model-local)
typedef struct vkShadowBlock_s {
	float			shadowRow0[ 4 ];
	float			shadowRow1[ 4 ];
	float			shadowRow2[ 4 ];
	float			shadowRow3[ 4 ];
	float			atlasRect[ 4 ];
	float			biasParams[ 4 ];	// x: constant bias, y: normal bias, z: texel depth bias, w: normal-offset world
	float			texelSize[ 4 ];		// x,y: 1 / atlas dimensions
} vkShadowBlock_t;

// per-view pass state (space/depth-range tracking mirrors the Draw3DView
// walks; reset per VK_Interactions_DrawLights call)
typedef struct vkInterPass_s {
	const viewDef_t *	viewDef;
	VkCommandBuffer		cmd;
	int					slot;
	int					fbHeight;
	VkViewport			viewport;
	VkPipelineLayout	layout;
	VkDescriptorSet		specTableSet;
	const viewEntity_t *currentSpace;
	bool				weaponDepthRange;
	float				mvp[ 16 ];
	float				ambientDir[ 3 ];
	int					lightCount;
	int					drawCount;

	// Phase F2a shadow-map receivers
	bool				shadowPassPrepared;	// atlas rendered for this view
	VkPipeline			pipelineUnshadowed;
	VkPipeline			pipelineShadowed;
	VkPipelineLayout	layoutShadowed;
	VkDescriptorSet		shadowSet;
	bool				shadowActive;		// the shadow pipeline is bound
	const vkShadowLightState_t *shadowState;	// current light's shadow state (shadowActive only)
	int					shadowSliceOffset;	// ring offset of the current space's shadow block, -1 = unset
	int					shadowLightCount;
	int					shadowDrawCount;
} vkInterPass_t;

static vkInterPass_t interPass;

/*
====================
RB_BakeTextureMatrixIntoTexgen

Port of the excluded draw_common.cpp implementation: folds a light-stage
texture matrix into the S/T texgen planes (Q passes through the multiply
untouched). The GL version reads backEnd.lightTextureMatrix directly; this
one honors the parameter (every caller passes that global).
====================
*/
void RB_BakeTextureMatrixIntoTexgen( idPlane lightProject[3], const float textureMatrix[16] ) {
	float	genMatrix[16];
	float	final[16];

	genMatrix[0] = lightProject[0][0];
	genMatrix[4] = lightProject[0][1];
	genMatrix[8] = lightProject[0][2];
	genMatrix[12] = lightProject[0][3];

	genMatrix[1] = lightProject[1][0];
	genMatrix[5] = lightProject[1][1];
	genMatrix[9] = lightProject[1][2];
	genMatrix[13] = lightProject[1][3];

	genMatrix[2] = 0;
	genMatrix[6] = 0;
	genMatrix[10] = 0;
	genMatrix[14] = 0;

	genMatrix[3] = lightProject[2][0];
	genMatrix[7] = lightProject[2][1];
	genMatrix[11] = lightProject[2][2];
	genMatrix[15] = lightProject[2][3];

	myGlMultMatrix( genMatrix, textureMatrix, final );

	lightProject[0][0] = final[0];
	lightProject[0][1] = final[4];
	lightProject[0][2] = final[8];
	lightProject[0][3] = final[12];

	lightProject[1][0] = final[1];
	lightProject[1][1] = final[5];
	lightProject[1][2] = final[9];
	lightProject[1][3] = final[13];
}

/*
====================
VK_DetermineLightScale

Port of RB_DetermineLightScale (tr_render.cpp:675). The vk module always
runs the ARB2-shaped front-end path (backEndRendererMaxLight = 999), so
lightScale normally stays r_lightScale and overBright 1.0; the GL-only
disableARB2Interactions driver-quirk branch has no vk analog.
====================
*/
static void VK_DetermineLightScale( void ) {
	viewLight_t			*vLight;
	const idMaterial	*shader;
	float				max;
	int					i, j, numStages;
	const shaderStage_t	*stage;

	// the light scale will be based on the largest color component of any
	// surface that will be drawn; if there are no lights, this stays 1.0
	max = 1.0;

	for ( vLight = backEnd.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		// lights with no surfaces or shaderparms may still be present
		// for debug display
		if ( !vLight->localInteractions && !vLight->globalInteractions
			&& !vLight->translucentInteractions ) {
			continue;
		}

		shader = vLight->lightShader;
		numStages = shader->GetNumStages();
		for ( i = 0 ; i < numStages ; i++ ) {
			stage = shader->GetStage( i );
			for ( j = 0 ; j < 3 ; j++ ) {
				float	v = r_lightScale.GetFloat() * vLight->shaderRegisters[ stage->color.registers[j] ];
				if ( v > max ) {
					max = v;
				}
			}
		}
	}

	backEnd.pc.maxLightValue = max;
	if ( max <= tr.backEndRendererMaxLight ) {
		backEnd.lightScale = r_lightScale.GetFloat();
		backEnd.overBright = 1.0;
	} else {
		backEnd.lightScale = r_lightScale.GetFloat() * tr.backEndRendererMaxLight / max;
		backEnd.overBright = max / tr.backEndRendererMaxLight;
	}
}

/*
====================
VK_SetDrawInteraction

Port of R_SetDrawInteraction (tr_render.cpp:782): stage texture matrix as
two S/T rows (scroll offsets wrapped at ±40), stage color clamped [0,1].
====================
*/
static void VK_SetDrawInteraction( const shaderStage_t *surfaceStage, const float *surfaceRegs,
								   idImage **image, idVec4 matrix[2], float color[4] ) {
	*image = surfaceStage->texture.image;
	if ( surfaceStage->texture.hasMatrix ) {
		matrix[0][0] = surfaceRegs[surfaceStage->texture.matrix[0][0]];
		matrix[0][1] = surfaceRegs[surfaceStage->texture.matrix[0][1]];
		matrix[0][2] = 0;
		matrix[0][3] = surfaceRegs[surfaceStage->texture.matrix[0][2]];

		matrix[1][0] = surfaceRegs[surfaceStage->texture.matrix[1][0]];
		matrix[1][1] = surfaceRegs[surfaceStage->texture.matrix[1][1]];
		matrix[1][2] = 0;
		matrix[1][3] = surfaceRegs[surfaceStage->texture.matrix[1][2]];

		// we attempt to keep scrolls from generating incredibly large
		// texture values, but center rotations and center scales can still
		// generate offsets that need to be > 1
		if ( matrix[0][3] < -40 || matrix[0][3] > 40 ) {
			matrix[0][3] -= (int)matrix[0][3];
		}
		if ( matrix[1][3] < -40 || matrix[1][3] > 40 ) {
			matrix[1][3] -= (int)matrix[1][3];
		}
	} else {
		matrix[0][0] = 1;
		matrix[0][1] = 0;
		matrix[0][2] = 0;
		matrix[0][3] = 0;

		matrix[1][0] = 0;
		matrix[1][1] = 1;
		matrix[1][2] = 0;
		matrix[1][3] = 0;
	}

	if ( color ) {
		for ( int i = 0 ; i < 4 ; i++ ) {
			color[i] = surfaceRegs[surfaceStage->color.registers[i]];
			// clamp here, so cards with greater range don't look different
			if ( color[i] < 0 ) {
				color[i] = 0;
			} else if ( color[i] > 1.0 ) {
				color[i] = 1.0;
			}
		}
	}
}

/*
====================
VK_DrawSingleInteraction

The Vulkan analog of RB_ARB2_DrawInteraction: streams the interaction
block into the uniform ring, binds the six cached image sets + the ring
set, pushes the 128B block, and draws the bound light-tris geometry.
====================
*/
static void VK_DrawSingleInteraction( const drawInteraction_t *din ) {
	if ( din->bumpImage == NULL || din->lightFalloffImage == NULL || din->lightImage == NULL
			|| din->diffuseImage == NULL || din->specularImage == NULL ) {
		return;
	}

	// shadowed lights bind set 7 (atlas + shadow block) and pass a second
	// dynamic offset; a missing per-space shadow slice (ring overflow) skips
	// the draw exactly like the interaction-slice failure below
	const bool shadowDraw = interPass.shadowActive;
	if ( shadowDraw && interPass.shadowSliceOffset < 0 ) {
		return;
	}
	const int setCount = shadowDraw ? 8 : 7;
	const VkPipelineLayout layout = shadowDraw ? interPass.layoutShadowed : interPass.layout;

	VkDescriptorSet sets[ 8 ];
	sets[ 0 ] = interPass.specTableSet;
	sets[ 1 ] = VK_Exec_ImageDescriptor( din->bumpImage->GetDeviceHandle(), true );
	sets[ 2 ] = VK_Exec_ImageDescriptor( din->lightFalloffImage->GetDeviceHandle(), true );
	sets[ 3 ] = VK_Exec_ImageDescriptor( din->lightImage->GetDeviceHandle(), true );
	sets[ 4 ] = VK_Exec_ImageDescriptor( din->diffuseImage->GetDeviceHandle(), true );
	sets[ 5 ] = VK_Exec_ImageDescriptor( din->specularImage->GetDeviceHandle(), true );
	sets[ 6 ] = VK_Exec_InteractionUniformSet();
	sets[ 7 ] = interPass.shadowSet;
	for ( int i = 0 ; i < setCount ; i++ ) {
		if ( sets[ i ] == VK_NULL_HANDLE ) {
			return;
		}
	}

	vkInteractionBlock_t block;
	memcpy( block.localLightOrigin, din->localLightOrigin.ToFloatPtr(), sizeof( block.localLightOrigin ) );
	memcpy( block.localViewOrigin, din->localViewOrigin.ToFloatPtr(), sizeof( block.localViewOrigin ) );
	memcpy( block.lightProjectionS, din->lightProjection[0].ToFloatPtr(), sizeof( block.lightProjectionS ) );
	memcpy( block.lightProjectionT, din->lightProjection[1].ToFloatPtr(), sizeof( block.lightProjectionT ) );
	memcpy( block.lightProjectionQ, din->lightProjection[2].ToFloatPtr(), sizeof( block.lightProjectionQ ) );
	memcpy( block.lightFalloffS, din->lightProjection[3].ToFloatPtr(), sizeof( block.lightFalloffS ) );
	memcpy( block.bumpMatrixS, din->bumpMatrix[0].ToFloatPtr(), sizeof( block.bumpMatrixS ) );
	memcpy( block.bumpMatrixT, din->bumpMatrix[1].ToFloatPtr(), sizeof( block.bumpMatrixT ) );
	memcpy( block.diffuseMatrixS, din->diffuseMatrix[0].ToFloatPtr(), sizeof( block.diffuseMatrixS ) );
	memcpy( block.diffuseMatrixT, din->diffuseMatrix[1].ToFloatPtr(), sizeof( block.diffuseMatrixT ) );
	memcpy( block.specularMatrixS, din->specularMatrix[0].ToFloatPtr(), sizeof( block.specularMatrixS ) );
	memcpy( block.specularMatrixT, din->specularMatrix[1].ToFloatPtr(), sizeof( block.specularMatrixT ) );
	// the shader doubles the specular term (ARB2 doubles the env constant)
	memcpy( block.diffuseColor, din->diffuseColor.ToFloatPtr(), sizeof( block.diffuseColor ) );
	memcpy( block.specularColor, din->specularColor.ToFloatPtr(), sizeof( block.specularColor ) );

	const int uboOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
	if ( uboOffset < 0 ) {
		return;
	}

	// SVC packing exactly like the stock interaction.vfp (ICM_PACKED)
	vkInteractionPush_t push;
	memset( &push, 0, sizeof( push ) );
	memcpy( push.mvp, interPass.mvp, sizeof( push.mvp ) );
	switch ( din->vertexColor ) {
		case SVC_MODULATE:
			push.a[ 0 ] = 1.0f;
			push.a[ 1 ] = 0.0f;
			break;
		case SVC_INVERSE_MODULATE:
			push.a[ 0 ] = -1.0f;
			push.a[ 1 ] = 1.0f;
			break;
		default:	// SVC_IGNORE
			push.a[ 0 ] = 0.0f;
			push.a[ 1 ] = 1.0f;
			break;
	}
	push.a[ 2 ] = din->ambientLight ? 1.0f : 0.0f;
	push.b[ 0 ] = interPass.ambientDir[ 0 ];
	push.b[ 1 ] = interPass.ambientDir[ 1 ];
	push.b[ 2 ] = interPass.ambientDir[ 2 ];

	// dynamic offsets consume in set order: set 6 interaction slice, then
	// (shadowed only) set 7 binding 1 shadow slice
	uint32_t dynamicOffsets[ 2 ];
	dynamicOffsets[ 0 ] = (uint32_t)uboOffset;
	dynamicOffsets[ 1 ] = (uint32_t)interPass.shadowSliceOffset;
	vkCmdBindDescriptorSets( interPass.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
			0, (uint32_t)setCount, sets, shadowDraw ? 2 : 1, dynamicOffsets );
	vkCmdPushConstants( interPass.cmd, layout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
	vkCmdDrawIndexed( interPass.cmd, (uint32_t)din->surf->geo->numIndexes, 1, 0, 0, 0 );
	interPass.drawCount++;
	if ( shadowDraw ) {
		interPass.shadowDrawCount++;
	}
}

/*
====================
VK_SubmitInteraction

Port of RB_SubmittInteraction (tr_render.cpp:836): blackImage defaults for
missing diffuse/specular (and the r_skip* debug substitutions), flat
normal map for skipped bump, and the skip-if-nothing-would-draw rule.
====================
*/
static void VK_SubmitInteraction( drawInteraction_t *din ) {
	if ( !din->bumpImage ) {
		return;
	}

	if ( !din->diffuseImage || r_skipDiffuse.GetBool() ) {
		din->diffuseImage = globalImages->blackImage;
	}
	if ( !din->specularImage || r_skipSpecular.GetBool() || din->ambientLight ) {
		din->specularImage = globalImages->blackImage;
	}
	if ( !din->bumpImage || r_skipBump.GetBool() ) {
		din->bumpImage = globalImages->flatNormalMap;
	}

	// if we wouldn't draw anything, don't call the Draw function
	if (
		( ( din->diffuseColor[0] > 0 ||
		din->diffuseColor[1] > 0 ||
		din->diffuseColor[2] > 0 ) && din->diffuseImage != globalImages->blackImage )
		|| ( ( din->specularColor[0] > 0 ||
		din->specularColor[1] > 0 ||
		din->specularColor[2] > 0 ) && din->specularImage != globalImages->blackImage ) ) {
		VK_DrawSingleInteraction( din );
	}
}

/*
====================
VK_Inter_WriteShadowSlice

Per-space shadow block (GL contract, draw_arb2.cpp:8552-8581): the light's
world clip planes localized to the surface's model space CPU-side, plus the
per-light composed atlas rect and bias scalars, streamed as a second 256B
ring slice. Returns the dynamic offset or -1 on ring overflow.
====================
*/
static int VK_Inter_WriteShadowSlice( const viewEntity_t *space ) {
	const vkShadowLightState_t *state = interPass.shadowState;
	if ( state == NULL || space == NULL ) {
		return -1;
	}

	vkShadowBlock_t block;
	memset( &block, 0, sizeof( block ) );
	idPlane localPlane;
	R_GlobalPlaneToLocal( space->modelMatrix, state->clipPlanes[ 0 ], localPlane );
	memcpy( block.shadowRow0, localPlane.ToFloatPtr(), sizeof( block.shadowRow0 ) );
	R_GlobalPlaneToLocal( space->modelMatrix, state->clipPlanes[ 1 ], localPlane );
	memcpy( block.shadowRow1, localPlane.ToFloatPtr(), sizeof( block.shadowRow1 ) );
	R_GlobalPlaneToLocal( space->modelMatrix, state->clipPlanes[ 2 ], localPlane );
	memcpy( block.shadowRow2, localPlane.ToFloatPtr(), sizeof( block.shadowRow2 ) );
	R_GlobalPlaneToLocal( space->modelMatrix, state->clipPlanes[ 3 ], localPlane );
	memcpy( block.shadowRow3, localPlane.ToFloatPtr(), sizeof( block.shadowRow3 ) );

	memcpy( block.atlasRect, state->atlasRect, sizeof( block.atlasRect ) );
	block.biasParams[ 0 ] = r_shadowMapBias.GetFloat();
	block.biasParams[ 1 ] = r_shadowMapNormalBias.GetFloat();
	block.biasParams[ 2 ] = state->texelDepthBias;
	block.biasParams[ 3 ] = state->normalOffsetWorld;
	block.texelSize[ 0 ] = state->invAtlasSize[ 0 ];
	block.texelSize[ 1 ] = state->invAtlasSize[ 1 ];

	return VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
}

/*
====================
VK_Inter_SelectShadowMode

Binds the shadowed or unshadowed interaction pipeline for the next chain.
Entering a shadowed light invalidates the space tracking so the per-space
shadow slice (per-LIGHT data) is rewritten even when the space is unchanged.
====================
*/
static void VK_Inter_SelectShadowMode( const vkShadowLightState_t *state ) {
	const bool wantShadow = state != NULL;
	if ( wantShadow != interPass.shadowActive ) {
		interPass.shadowActive = wantShadow;
		vkCmdBindPipeline( interPass.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				wantShadow ? interPass.pipelineShadowed : interPass.pipelineUnshadowed );
	}
	interPass.shadowState = state;
	if ( wantShadow ) {
		interPass.currentSpace = NULL;
		interPass.shadowSliceOffset = -1;
	}
}

/*
====================
VK_CreateSingleDrawInteractions

Port of RB_CreateSingleDrawInteractionsFiltered (tr_render.cpp:875, no
stage filter, no packed prim-batch path): per-surface geometry/scissor/
space handling in the executor's conventions, then the light-stage ×
surface-stage decomposition into primitive drawInteraction_t draws.

The chain surfaces' geo is the light-tris subset — its own culled index
list over the ambient surface's shared idDrawVert cache.
====================
*/
static void VK_CreateSingleDrawInteractions( const drawSurf_t *surf ) {
	const idMaterial	*surfaceShader = surf->material;
	const float			*surfaceRegs = surf->shaderRegisters;
	const viewLight_t	*vLight = backEnd.vLight;
	const idMaterial	*lightShader = vLight->lightShader;
	const float			*lightRegs = vLight->shaderRegisters;
	drawInteraction_t	inter;

	if ( r_skipInteractions.GetBool() || surf->geo == NULL || surf->geo->ambientCache == NULL ) {
		return;
	}
	if ( surf->geo->numIndexes <= 0 || surf->geo->indexes == NULL ) {
		return;
	}

	if ( !VK_Exec_BindTriGeometry( interPass.cmd, interPass.slot, surf->geo ) ) {
		return;
	}
	VK_Exec_SetSurfScissor( interPass.cmd, interPass.viewDef, surf, interPass.fbHeight );

	// space change: rebuild the MVP (depth hacks included), the weapon
	// depth-range window, and the shadowed lights' per-space shadow slice,
	// mirroring the Draw3DView walks
	if ( surf->space != interPass.currentSpace ) {
		interPass.currentSpace = surf->space;
		VK_BuildSurfMVP( interPass.viewDef, surf, interPass.mvp );
		const bool wantWeaponRange = surf->space->weaponDepthHack;
		if ( wantWeaponRange != interPass.weaponDepthRange ) {
			interPass.weaponDepthRange = wantWeaponRange;
			interPass.viewport.maxDepth = wantWeaponRange ? 0.5f : 1.0f;
			vkCmdSetViewport( interPass.cmd, 0, 1, &interPass.viewport );
		}
		if ( interPass.shadowActive ) {
			interPass.shadowSliceOffset = VK_Inter_WriteShadowSlice( surf->space );
		}
	}

	// material cull with the mirror swap (GL_Cull contract)
	switch ( surfaceShader->GetCullType() ) {
		case CT_TWO_SIDED:
			vkCmdSetCullMode( interPass.cmd, VK_CULL_MODE_NONE );
			break;
		case CT_BACK_SIDED:
			vkCmdSetCullMode( interPass.cmd, interPass.viewDef->isMirror ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT );
			break;
		default:
			vkCmdSetCullMode( interPass.cmd, interPass.viewDef->isMirror ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT );
			break;
	}

	// Quake 4 applies decal polygon offset in interaction passes as well
	// (RB_ARB2_DrawInteraction does this per draw; the material is shared
	// by every primitive interaction of the surface)
	const bool polygonOffset = surfaceShader->TestMaterialFlag( MF_POLYGONOFFSET );
	if ( polygonOffset ) {
		vkCmdSetDepthBiasEnable( interPass.cmd, VK_TRUE );
		vkCmdSetDepthBias( interPass.cmd, r_offsetUnits.GetFloat() * surfaceShader->GetPolygonOffset(), 0.0f, r_offsetFactor.GetFloat() );
	}

	inter.surf = surf;
	inter.lightFalloffImage = vLight->falloffImage;
	inter.vertexColor = SVC_IGNORE;

	R_GlobalPointToLocal( surf->space->modelMatrix, vLight->globalLightOrigin, inter.localLightOrigin.ToVec3() );
	R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, inter.localViewOrigin.ToVec3() );
	inter.localLightOrigin[3] = 0;
	inter.localViewOrigin[3] = 1;
	inter.ambientLight = lightShader->IsAmbientLight();

	// the base projections may be modified by texture matrix on light stages
	idPlane lightProject[4];
	for ( int i = 0 ; i < 4 ; i++ ) {
		R_GlobalPlaneToLocal( surf->space->modelMatrix, vLight->lightProject[i], lightProject[i] );
	}

	const int lightStageCount = lightShader->GetNumStages();
	const int surfaceStageCount = surfaceShader->GetNumStages();
	for ( int lightStageNum = 0 ; lightStageNum < lightStageCount ; lightStageNum++ ) {
		const shaderStage_t	*lightStage = lightShader->GetStage( lightStageNum );

		// ignore stages that fail the condition
		if ( !lightRegs[ lightStage->conditionRegister ] ) {
			continue;
		}

		inter.lightImage = lightStage->texture.image;

		memcpy( inter.lightProjection, lightProject, sizeof( inter.lightProjection ) );
		// now multiply the texgen by the light texture matrix
		if ( lightStage->texture.hasMatrix ) {
			RB_GetShaderTextureMatrix( lightRegs, &lightStage->texture, backEnd.lightTextureMatrix );
			RB_BakeTextureMatrixIntoTexgen( reinterpret_cast<class idPlane *>(inter.lightProjection), backEnd.lightTextureMatrix );
		}

		inter.bumpImage = NULL;
		inter.specularImage = NULL;
		inter.diffuseImage = NULL;
		inter.diffuseColor[0] = inter.diffuseColor[1] = inter.diffuseColor[2] = inter.diffuseColor[3] = 0;
		inter.specularColor[0] = inter.specularColor[1] = inter.specularColor[2] = inter.specularColor[3] = 0;

		float lightColor[4];

		// backEnd.lightScale is calculated so that lightColor[] will never
		// exceed tr.backEndRendererMaxLight
		lightColor[0] = backEnd.lightScale * lightRegs[ lightStage->color.registers[0] ];
		lightColor[1] = backEnd.lightScale * lightRegs[ lightStage->color.registers[1] ];
		lightColor[2] = backEnd.lightScale * lightRegs[ lightStage->color.registers[2] ];
		lightColor[3] = lightRegs[ lightStage->color.registers[3] ];

		// go through the individual stages
		for ( int surfaceStageNum = 0 ; surfaceStageNum < surfaceStageCount ; surfaceStageNum++ ) {
			const shaderStage_t	*surfaceStage = surfaceShader->GetStage( surfaceStageNum );

			switch( surfaceStage->lighting ) {
				case SL_AMBIENT: {
					// ignore ambient stages while drawing interactions
					break;
				}
				case SL_BUMP: {
					// ignore stage that fails the condition
					if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
						break;
					}
					// draw any previous interaction
					VK_SubmitInteraction( &inter );
					inter.diffuseImage = NULL;
					inter.specularImage = NULL;
					VK_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.bumpImage, inter.bumpMatrix, NULL );
					break;
				}
				case SL_DIFFUSE: {
					// ignore stage that fails the condition
					if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
						break;
					}
					if ( inter.diffuseImage ) {
						VK_SubmitInteraction( &inter );
					}
					VK_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.diffuseImage,
											inter.diffuseMatrix, inter.diffuseColor.ToFloatPtr() );
					inter.diffuseColor[0] *= lightColor[0];
					inter.diffuseColor[1] *= lightColor[1];
					inter.diffuseColor[2] *= lightColor[2];
					inter.diffuseColor[3] *= lightColor[3];
					inter.vertexColor = surfaceStage->vertexColor;
					break;
				}
				case SL_SPECULAR: {
					// ignore stage that fails the condition
					if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
						break;
					}
					if ( inter.specularImage ) {
						VK_SubmitInteraction( &inter );
					}
					VK_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.specularImage,
											inter.specularMatrix, inter.specularColor.ToFloatPtr() );
					inter.specularColor[0] *= lightColor[0];
					inter.specularColor[1] *= lightColor[1];
					inter.specularColor[2] *= lightColor[2];
					inter.specularColor[3] *= lightColor[3];
					inter.vertexColor = surfaceStage->vertexColor;
					break;
				}
			}
		}

		// draw the final interaction
		VK_SubmitInteraction( &inter );
	}

	if ( polygonOffset ) {
		vkCmdSetDepthBiasEnable( interPass.cmd, VK_FALSE );
	}
}

/*
====================
VK_DrawInteractionChain
====================
*/
static void VK_DrawInteractionChain( const drawSurf_t *surf ) {
	for ( ; surf ; surf = surf->nextOnLight ) {
		VK_CreateSingleDrawInteractions( surf );
	}
}

/*
====================
VK_Interactions_DrawLights

The Phase F1 light loop (RB_ARB2_DrawInteractions skeleton): skip fog and
blend lights (fogging is a later phase) and lights with no interactions;
draw local then global interactions additively at depth EQUAL with depth
writes off, then translucent interactions at depth LESS. All lights draw
unshadowed — with the Phase E pipelines stencil-free this is exactly the
GL branch for lights without shadow surfaces.

Called from VK_GuiExecutor_Draw3DView between the depth fill and the
ambient walks; exits with depth bias off and the depth-range baseline
(maxDepth 1.0) restored.
====================
*/
void VK_Interactions_DrawLights( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->viewLights == NULL ) {
		return;
	}
	if ( r_skipInteractions.GetBool() ) {
		return;
	}

	VkCommandBuffer cmd = VK_Exec_ActiveCmd();
	if ( cmd == VK_NULL_HANDLE ) {
		return;
	}
	VkPipeline pipeline = VK_Exec_InteractionPipeline();
	if ( pipeline == VK_NULL_HANDLE ) {
		return;
	}

	memset( &interPass, 0, sizeof( interPass ) );
	interPass.viewDef = viewDef;
	interPass.cmd = cmd;
	interPass.slot = VK_Exec_ActiveFrameSlot();
	interPass.fbHeight = (int)vkCtx.swapchainExtent.height;
	interPass.layout = VK_Exec_InteractionPipelineLayout();
	interPass.pipelineUnshadowed = pipeline;
	interPass.shadowSliceOffset = -1;

	// Phase F2a: classify + tile the view's shadow-map lights (CPU), then
	// render the atlas in a frame-scope interruption BEFORE any batch state
	// is set (the resume path re-establishes the executor baseline). Any
	// failure simply leaves every light on the unshadowed F1 path.
	if ( VK_ShadowMap_PrepareViewLights( viewDef ) > 0 ) {
		interPass.pipelineShadowed = VK_Exec_ShadowInteractionPipeline();
		interPass.layoutShadowed = VK_Exec_ShadowInteractionPipelineLayout();
		if ( interPass.pipelineShadowed != VK_NULL_HANDLE && interPass.layoutShadowed != VK_NULL_HANDLE ) {
			VK_ShadowMap_RenderAtlas( viewDef );
			interPass.shadowSet = VK_Exec_ShadowDescriptorSet();
			interPass.shadowPassPrepared = interPass.shadowSet != VK_NULL_HANDLE;
		}
	}

	// the specular table rides slot 0 for every draw (ARB2 binds it once
	// on unit 6); without a device-resident table the pass cannot draw
	interPass.specTableSet = VK_Exec_ImageDescriptor( globalImages->specularTableImage->GetDeviceHandle(), true );
	if ( interPass.specTableSet == VK_NULL_HANDLE ) {
		return;
	}

	// ambient lights: both GL paths sample the ambient normal-map cube and
	// decode rgb*2-1. R_AmbientNormalImage stores x in the alpha channel
	// (the red slot holds 255), so the decoded tangent-space constant is
	// (1, q(y), q(z)) with 8-bit quantization — reproduce it exactly
	interPass.ambientDir[ 0 ] = 1.0f;
	interPass.ambientDir[ 1 ] = ( (float)(byte)( 255 * tr.ambientLightVector[1] ) / 255.0f ) * 2.0f - 1.0f;
	interPass.ambientDir[ 2 ] = ( (float)(byte)( 255 * tr.ambientLightVector[2] ) / 255.0f ) * 2.0f - 1.0f;

	VK_DetermineLightScale();

	// GL bottom-left viewport -> Vulkan negative-height viewport (the same
	// baseline Draw3DView established; re-issued only for depth-range hacks)
	const int vpX = viewDef->viewport.x1;
	const int vpYGL = viewDef->viewport.y1;
	const int vpW = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int vpH = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;
	interPass.viewport.x = (float)vpX;
	interPass.viewport.y = (float)( interPass.fbHeight - vpYGL );
	interPass.viewport.width = (float)vpW;
	interPass.viewport.height = -(float)vpH;
	interPass.viewport.minDepth = 0.0f;
	interPass.viewport.maxDepth = 1.0f;

	// batch state: one pipeline (ONE/ONE additive), depth test on with
	// writes off (GLS_DEPTHMASK), bias off until a decal material needs it.
	// The viewport is issued unconditionally: the depth-fill walk may have
	// left a weapon depth-range (maxDepth 0.5) latched, and the pass's
	// tracking assumes the baseline at entry
	vkCmdSetViewport( cmd, 0, 1, &interPass.viewport );
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	vkCmdSetDepthTestEnable( cmd, VK_TRUE );
	vkCmdSetDepthWriteEnable( cmd, VK_FALSE );
	vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );

	for ( viewLight_t *vLight = viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		backEnd.vLight = vLight;

		// do fogging later
		if ( vLight->lightShader->IsFogLight() ) {
			continue;
		}
		if ( vLight->lightShader->IsBlendLight() ) {
			continue;
		}

		if ( !vLight->localInteractions && !vLight->globalInteractions
			&& !vLight->translucentInteractions ) {
			continue;
		}

		interPass.lightCount++;

		// Phase F2a: lights with a rendered atlas tile draw their opaque
		// interactions through the shadow-receiving pipeline; everything
		// else (point lights, gated lights, resource failures) stays on the
		// unshadowed F1 path. Both interaction sets sample the combined
		// caster map; translucent receivers stay unshadowed scratch-first.
		const vkShadowLightState_t *shadowState = NULL;
		if ( interPass.shadowPassPrepared ) {
			shadowState = VK_ShadowMap_LightState( vLight );
			if ( shadowState != NULL ) {
				interPass.shadowLightCount++;
			}
		}
		VK_Inter_SelectShadowMode( shadowState );

		// opaque interactions test EQUAL against the depth fill
		vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_EQUAL );
		VK_DrawInteractionChain( vLight->localInteractions );
		VK_DrawInteractionChain( vLight->globalInteractions );

		if ( !r_skipTranslucent.GetBool() ) {
			VK_Inter_SelectShadowMode( NULL );
			// GLS_DEPTHFUNC_LESS maps to glDepthFunc(GL_LEQUAL)
			vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_LESS_OR_EQUAL );
			VK_DrawInteractionChain( vLight->translucentInteractions );
		}
	}

	// restore the depth-range baseline for the ambient walks
	if ( interPass.weaponDepthRange ) {
		interPass.viewport.maxDepth = 1.0f;
		vkCmdSetViewport( cmd, 0, 1, &interPass.viewport );
	}

	// one-shot bring-up evidence that the interaction pass emitted real work
	static bool loggedFirstInteractionPass = false;
	if ( !loggedFirstInteractionPass && interPass.drawCount > 0 ) {
		loggedFirstInteractionPass = true;
		common->Printf( "Vulkan: first interaction pass drew %d interactions across %d lights\n",
				interPass.drawCount, interPass.lightCount );
	}

	// one-shot bring-up evidence that shadow-receiving interactions drew
	static bool loggedFirstShadowReceivers = false;
	if ( !loggedFirstShadowReceivers && interPass.shadowDrawCount > 0 ) {
		loggedFirstShadowReceivers = true;
		common->Printf( "Vulkan: first shadow-receiving interaction pass drew %d shadowed interactions across %d shadow lights\n",
				interPass.shadowDrawCount, interPass.shadowLightCount );
	}
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
