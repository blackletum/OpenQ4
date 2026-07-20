// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	Vulkan interaction pass (Phase F1,
	docs/dev/plans/2026-07-19-vulkan-phase-f.md) + stencil shadow volumes
	(Phase G1, docs/dev/plans/2026-07-20-vulkan-phase-g.md).

	Per-light bump/diffuse/specular interactions for every view light,
	drawn between the depth fill and the ambient walks. Lights the
	shadow-map path (Phase F2) admits keep the receiver pipelines; every
	OTHER light that carries shadow surfs stamps stencil volumes and draws
	its interactions under the GEQUAL/128 exit contract — with the retail
	default r_useShadowMap 0 that is EVERY shadow-casting light.

	GL-free ports from TUs excluded from the vk module build:
	- RB_ARB2_DrawInteractions light loop (draw_arb2.cpp:11458-11666):
	  skip fog/blend/empty lights; opaque interactions (local then global)
	  additive at depth EQUAL with writes off; translucent at depth LESS.
	- The stencil-path per-light block (draw_arb2.cpp:11599-11649):
	  scissored stencil clear to 128, then globalShadows →
	  localInteractions → localShadows → globalInteractions (stencil
	  ownership: noSelfShadow receivers are lit before the local volumes
	  join), translucent GEQUAL under r_stencilTranslucentShadows.
	- RB_StencilShadowPass + RB_T_Shadow (draw_common.cpp:7194-7444):
	  two-sided single-pass wrap-op sequences, the per-surface
	  index-count/external selection ladder, the GEQUAL/128/KEEP exit.
	- RB_CreateSingleDrawInteractionsFiltered decomposition walk +
	  R_SetDrawInteraction + RB_SubmittInteraction (tr_render.cpp:782-1033):
	  light-stage × surface-stage pairing into drawInteraction_t, with the
	  r_skipBump/Diffuse/Specular substitutions and the both-black skip.
	- RB_DetermineLightScale (tr_render.cpp:675-726).
	- RB_BakeTextureMatrixIntoTexgen (draw_common.cpp:4365-4400).

	Per-draw data rides the shared 128B push block (MVP + vertex-color
	packing + ambient direction; the volume pipeline reuses it for MVP +
	local light origin); everything else streams through the executor's
	dynamic uniform ring as a std140 block on set 6. The six texture slots
	bind cached per-image single-sampler sets (0=specular table, 1=bump,
	2=falloff, 3=light projection, 4=diffuse, 5=specular).

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
bool VK_Exec_BindShadowGeometry( VkCommandBuffer cmd, int slot, const srfTriangles_t *tri );
void VK_Exec_SetSurfScissor( VkCommandBuffer cmd, const viewDef_t *viewDef, const drawSurf_t *drawSurf, int fbHeight );
void VK_BuildSurfMVP( const viewDef_t *viewDef, const drawSurf_t *drawSurf, float outMvp[ 16 ] );
VkPipeline VK_Exec_InteractionPipeline( void );
VkPipelineLayout VK_Exec_InteractionPipelineLayout( void );
VkPipeline VK_Exec_ShadowInteractionPipeline( void );
VkPipeline VK_Exec_PointShadowInteractionPipeline( void );
VkPipelineLayout VK_Exec_ShadowInteractionPipelineLayout( void );
VkPipeline VK_Exec_StencilShadowPipeline( void );
VkPipelineLayout VK_Exec_BasePipelineLayout( void );
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

// std140 mirror of the point variant's set-7 ShadowBlock (Phase F2b,
// 5 vec4 = 80 bytes; rewritten per space — the rows are the model matrix)
typedef struct vkPointShadowBlock_s {
	float			modelRow0[ 4 ];		// model -> world matrix rows
	float			modelRow1[ 4 ];
	float			modelRow2[ 4 ];
	float			lightOriginFar[ 4 ];	// xyz: world light origin, w: far envelope
	float			biasParams[ 4 ];	// x: constant bias, y: normal bias, z: texel depth bias, w: per-distance normal-offset factor
} vkPointShadowBlock_t;

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

	// Phase F2a/F2b shadow-map receivers
	bool				shadowPassPrepared;	// shadow maps rendered for this view
	VkPipeline			pipelineUnshadowed;
	VkPipeline			pipelineShadowed;		// projected receiver (atlas)
	VkPipeline			pipelinePointShadowed;	// point receiver (cube)
	VkPipelineLayout	layoutShadowed;
	VkDescriptorSet		shadowSetAtlas;		// the executor's atlas set (projected lights)
	VkDescriptorSet		shadowSet;			// active light's set-7 set (atlas or cube)
	int					shadowMode;			// 0 = unshadowed, 1 = projected, 2 = point
	bool				shadowActive;		// a shadow pipeline is bound
	const vkShadowLightState_t *shadowState;	// current light's shadow state (shadowActive only)
	int					shadowSliceOffset;	// ring offset of the current space's shadow block, -1 = unset
	int					shadowLightCount;
	int					shadowDrawCount;

	// Phase G1 stencil shadow volumes
	VkPipeline			pipelineStencilShadow;	// vec4 volume stream, color writes off
	VkPipelineLayout	layoutStencilShadow;	// the base 128B-push layout
	int					stencilLightCount;		// lights that took the stencil path
	int					volumeDrawCount;		// volume draws (preload + z-pass)
	int					volumePreloadCount;		// z-fail preload draws (internal volumes)
	int					volumeSkipCount;		// prim-batch / cache-less shadow surfs skipped
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

Per-space shadow block, streamed as a second 256B ring slice. Projected
lights (GL contract, draw_arb2.cpp:8552-8581): the light's world clip planes
localized to the surface's model space CPU-side, plus the per-light composed
atlas rect and bias scalars. Point lights (RB_GLSLPointShadowMap_
DrawInteraction contract): the model matrix rows (RB_ShadowMapModelMatrixRows),
the global light origin + far envelope, and the point bias scalars. Returns
the dynamic offset or -1 on ring overflow.
====================
*/
static int VK_Inter_WriteShadowSlice( const viewEntity_t *space ) {
	const vkShadowLightState_t *state = interPass.shadowState;
	if ( state == NULL || space == NULL ) {
		return -1;
	}

	if ( state->pointLight ) {
		vkPointShadowBlock_t pointBlock;
		memset( &pointBlock, 0, sizeof( pointBlock ) );
		const float *m = space->modelMatrix;
		for ( int i = 0 ; i < 4 ; i++ ) {
			pointBlock.modelRow0[ i ] = m[ i * 4 + 0 ];
			pointBlock.modelRow1[ i ] = m[ i * 4 + 1 ];
			pointBlock.modelRow2[ i ] = m[ i * 4 + 2 ];
		}
		pointBlock.lightOriginFar[ 0 ] = state->vLight->globalLightOrigin[ 0 ];
		pointBlock.lightOriginFar[ 1 ] = state->vLight->globalLightOrigin[ 1 ];
		pointBlock.lightOriginFar[ 2 ] = state->vLight->globalLightOrigin[ 2 ];
		pointBlock.lightOriginFar[ 3 ] = state->pointFar;
		// the depth-compare path uses r_shadowMapPointBias directly; the GL
		// storage-step floor only applies to the packed-color fallback
		pointBlock.biasParams[ 0 ] = r_shadowMapPointBias.GetFloat();
		pointBlock.biasParams[ 1 ] = r_shadowMapPointNormalBias.GetFloat();
		pointBlock.biasParams[ 2 ] = state->texelDepthBias;
		pointBlock.biasParams[ 3 ] = state->normalOffsetWorld;
		return VK_Exec_InteractionUniformAlloc( &pointBlock, sizeof( pointBlock ) );
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

Binds the projected-shadowed, point-shadowed, or unshadowed interaction
pipeline for the next chain and selects the light's set-7 descriptor set
(the shared atlas set, or the light's cube set). Entering a shadowed light
invalidates the space tracking so the per-space shadow slice (per-LIGHT
data) is rewritten even when the space is unchanged.
====================
*/
static void VK_Inter_SelectShadowMode( const vkShadowLightState_t *state ) {
	int wantMode = 0;
	if ( state != NULL ) {
		wantMode = state->pointLight ? 2 : 1;
	}
	// a missing per-class receiver pipeline (or point set) fails the light
	// back to the unshadowed F1 path
	if ( wantMode == 1 && ( interPass.pipelineShadowed == VK_NULL_HANDLE || interPass.shadowSetAtlas == VK_NULL_HANDLE ) ) {
		wantMode = 0;
		state = NULL;
	}
	if ( wantMode == 2 && ( interPass.pipelinePointShadowed == VK_NULL_HANDLE || state->pointSet == VK_NULL_HANDLE ) ) {
		wantMode = 0;
		state = NULL;
	}

	if ( wantMode != interPass.shadowMode ) {
		interPass.shadowMode = wantMode;
		VkPipeline pipeline = interPass.pipelineUnshadowed;
		if ( wantMode == 1 ) {
			pipeline = interPass.pipelineShadowed;
		} else if ( wantMode == 2 ) {
			pipeline = interPass.pipelinePointShadowed;
		}
		vkCmdBindPipeline( interPass.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	}
	interPass.shadowActive = wantMode != 0;
	interPass.shadowState = state;
	interPass.shadowSet = ( wantMode == 2 ) ? state->pointSet : interPass.shadowSetAtlas;
	if ( interPass.shadowActive ) {
		interPass.currentSpace = NULL;
		interPass.shadowSliceOffset = -1;
	}
}

/*
====================
VK_Inter_StencilClear

Per-light scissored stencil clear (draw_arb2.cpp:11600-11608 contract):
vkCmdClearAttachments over the stencil aspect of vLight->scissorRect,
converted exactly like VK_Exec_SetSurfScissor (viewport base + GL
bottom-left -> Vulkan top-left flip) and CLAMPED to the render area — the
Phase E lesson: the viewDef can carry a stale, larger size for one frame
across an OUT_OF_DATE recreate, and an escaping rect is a validation
failure. GL clears to the view-level latch (128, R_SafeStencilClearValue).
====================
*/
static void VK_Inter_StencilClear( const viewLight_t *vLight ) {
	const viewDef_t *viewDef = interPass.viewDef;
	const idScreenRect &rect = vLight->scissorRect;
	if ( rect.IsEmpty() ) {
		// a degenerate light scissor clears (and later draws) nothing
		return;
	}

	const int scX = viewDef->viewport.x1 + rect.x1;
	const int scYGL = viewDef->viewport.y1 + rect.y1;
	const int scW = rect.x2 - rect.x1 + 1;
	const int scH = rect.y2 - rect.y1 + 1;

	int x0 = scX > 0 ? scX : 0;
	int y0 = interPass.fbHeight - scYGL - scH;
	if ( y0 < 0 ) {
		y0 = 0;
	}
	int x1 = scX + scW;
	if ( x1 > (int)vkCtx.swapchainExtent.width ) {
		x1 = (int)vkCtx.swapchainExtent.width;
	}
	int y1 = interPass.fbHeight - scYGL;
	if ( y1 > (int)vkCtx.swapchainExtent.height ) {
		y1 = (int)vkCtx.swapchainExtent.height;
	}
	if ( x1 <= x0 || y1 <= y0 ) {
		return;
	}

	VkClearAttachment clearAtt;
	memset( &clearAtt, 0, sizeof( clearAtt ) );
	clearAtt.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
	clearAtt.clearValue.depthStencil.stencil = 128;
	VkClearRect clearRect;
	memset( &clearRect, 0, sizeof( clearRect ) );
	clearRect.rect.offset.x = x0;
	clearRect.rect.offset.y = y0;
	clearRect.rect.extent.width = (uint32_t)( x1 - x0 );
	clearRect.rect.extent.height = (uint32_t)( y1 - y0 );
	clearRect.layerCount = 1;
	vkCmdClearAttachments( interPass.cmd, 1, &clearAtt, 1, &clearRect );
}

/*
====================
VK_StencilShadowPass

Port of RB_StencilShadowPass + RB_T_Shadow (draw_common.cpp:7381-7444,
:7207-7362) in the two-sided single-pass formulation ONLY: wrap ops and
separate per-face stencil state are core Vulkan, so the GL capability gate
(glStencilOpSeparate && GL_INCR_WRAP, :7420-7423) is unconditionally
satisfied and the cull-flipped two-pass fallback never runs.

Enter/exit stencil contract: the caller latched GEQUAL/128/KEEP after the
per-light clear; this pass flips the per-face ops to ALWAYS + wrap writes
for the volume draws and restores GEQUAL/128/KEEP (the GL exit at
:7442-7443) so the light's interactions draw under the exit state.

Face mapping (derivation, following the E/F cull-mapping precedent): the
executor's negative-height viewport preserves GL winding parity under
VK_FRONT_FACE_COUNTER_CLOCKWISE, so CT_FRONT_SIDED maps to
VK_CULL_MODE_FRONT_BIT in non-mirror views (the Draw3DView depth fill /
GL_Cull's glCullFace(GL_FRONT) convention) — i.e. a triangle GL classifies
GL_BACK is a Vulkan back face. RB_T_Shadow assigns the legacy
CT_FRONT_SIDED ops to frontSidedFace = isMirror ? GL_FRONT : GL_BACK
(:7321), so those ops land on VK_STENCIL_FACE_BACK_BIT in non-mirror views
and flip for mirrors.

Documented Phase G1 gaps:
- depth-bounds test (r_useDepthBoundsTest, GL :7271-7273, :7416-7418): the
  optional depthBounds device feature is not enabled — skipped; pure
  fill-rate optimization, no visual effect.
- MD5R packed prim-batch volumes (md5rshadow.vp family) skip with a
  counter; the packed vertex-program path is Phase I.
- r_showShadows debug visualization: Phase I rendertools.
====================
*/
static void VK_StencilShadowPass( const drawSurf_t *drawSurfs ) {
	if ( !r_shadows.GetBool() || drawSurfs == NULL ) {
		return;
	}

	VkCommandBuffer cmd = interPass.cmd;

	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, interPass.pipelineStencilShadow );

	// GL_State(GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK |
	// GLS_DEPTHFUNC_LESS): color writes are off in the pipeline; depth
	// tests LEQUAL with writes off
	vkCmdSetDepthTestEnable( cmd, VK_TRUE );
	vkCmdSetDepthWriteEnable( cmd, VK_FALSE );
	vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_LESS_OR_EQUAL );

	// glPolygonOffset(r_shadowPolygonFactor, -r_shadowPolygonOffset): the
	// defaults (0, -1) give constant +1, slope 0 — bias IS on by default.
	// GL units map to the Vulkan constant factor and GL factor to the
	// slope factor (the Phase E polygon-offset mapping)
	const bool shadowBias = r_shadowPolygonFactor.GetFloat() != 0.0f || r_shadowPolygonOffset.GetFloat() != 0.0f;
	if ( shadowBias ) {
		vkCmdSetDepthBiasEnable( cmd, VK_TRUE );
		vkCmdSetDepthBias( cmd, -r_shadowPolygonOffset.GetFloat(), 0.0f, r_shadowPolygonFactor.GetFloat() );
	}

	// GL_Cull(CT_TWO_SIDED): both faces rasterize in one draw
	vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );

	// see the face-mapping derivation above
	const VkStencilFaceFlags frontSidedFace = interPass.viewDef->isMirror ? VK_STENCIL_FACE_FRONT_BIT : VK_STENCIL_FACE_BACK_BIT;
	const VkStencilFaceFlags backSidedFace = interPass.viewDef->isMirror ? VK_STENCIL_FACE_BACK_BIT : VK_STENCIL_FACE_FRONT_BIT;

	for ( const drawSurf_t *surf = drawSurfs ; surf ; surf = surf->nextOnLight ) {
		const srfTriangles_t *tri = surf->geo;

		// MD5R packed prim-batch volumes ride their own vertex family
		// (md5rshadow.vp + palette rows); cache-less surfs cannot draw
		// (RB_T_Shadow skips them at :7226-7229)
		if ( tri == NULL || R_TriHasPrimBatchMesh( tri ) || tri->shadowCache == NULL ) {
			interPass.volumeSkipCount++;
			continue;
		}
		if ( tri->numIndexes <= 0 || tri->indexes == NULL ) {
			continue;
		}

		if ( !VK_Exec_BindShadowGeometry( cmd, interPass.slot, tri ) ) {
			continue;
		}
		VK_Exec_SetSurfScissor( cmd, interPass.viewDef, surf, interPass.fbHeight );

		// space change: MVP (depth hacks included) + weapon depth-range,
		// sharing the pass tracking so the interleaved volume/interaction
		// chains never rebuild redundantly (VK_BuildSurfMVP is the same
		// function both walks use)
		if ( surf->space != interPass.currentSpace ) {
			interPass.currentSpace = surf->space;
			VK_BuildSurfMVP( interPass.viewDef, surf, interPass.mvp );
			const bool wantWeaponRange = surf->space->weaponDepthHack;
			if ( wantWeaponRange != interPass.weaponDepthRange ) {
				interPass.weaponDepthRange = wantWeaponRange;
				interPass.viewport.maxDepth = wantWeaponRange ? 0.5f : 1.0f;
				vkCmdSetViewport( cmd, 0, 1, &interPass.viewport );
			}
		}

		// the local light origin rides the shared push block (the env[4]
		// PP_LIGHT_ORIGIN contract, w = 0; draw_common.cpp:7211-7222).
		// With r_useShadowVertexProgram 0 the front-end bakes CPU-projected
		// caches whose w==0 verts are ALREADY light-relative directions —
		// push a zero origin so the shader's subtract becomes the
		// fixed-function pass-through
		vkInteractionPush_t push;
		memset( &push, 0, sizeof( push ) );
		memcpy( push.mvp, interPass.mvp, sizeof( push.mvp ) );
		if ( r_useShadowVertexProgram.GetBool() ) {
			idVec4 localLight;
			R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.vLight->globalLightOrigin, localLight.ToVec3() );
			localLight.w = 0.0f;
			memcpy( push.a, localLight.ToFloatPtr(), sizeof( push.a ) );
		}
		vkCmdPushConstants( cmd, interPass.layoutStencilShadow,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );

		// we always draw the sil planes, but we may not need to draw the
		// front or rear caps (RB_T_Shadow :7238-7268, verbatim)
		int numIndexes;
		bool external = false;

		if ( !r_useExternalShadows.GetInteger() ) {
			numIndexes = tri->numIndexes;
		} else if ( r_useExternalShadows.GetInteger() == 2 ) { // force to no caps for testing
			numIndexes = tri->numShadowIndexesNoCaps;
		} else if ( !( surf->dsFlags & DSF_VIEW_INSIDE_SHADOW ) ) {
			// if we aren't inside the shadow projection, no caps are ever needed
			numIndexes = tri->numShadowIndexesNoCaps;
			external = true;
		} else if ( !backEnd.vLight->viewInsideLight && !( tri->shadowCapPlaneBits & SHADOW_CAP_INFINITE ) ) {
			// if we are inside the shadow projection, but outside the light,
			// and drawing a non-infinite shadow, we can skip some caps
			if ( backEnd.vLight->viewSeesShadowPlaneBits & tri->shadowCapPlaneBits ) {
				// we can see through a rear cap, so we need to draw it, but
				// we can skip the caps on the actual surface
				numIndexes = tri->numShadowIndexesNoFrontCaps;
			} else {
				// we don't need to draw any caps
				numIndexes = tri->numShadowIndexesNoCaps;
			}
			external = true;
		} else {
			// must draw everything
			numIndexes = tri->numIndexes;
		}

		// If this surface could not use external shadow optimizations, the
		// front end already forced the "no caps" index counts back to the
		// full count; treat it as internal to keep the robust stencil path
		if ( numIndexes == tri->numIndexes ) {
			external = false;
		}
		if ( numIndexes <= 0 ) {
			continue;
		}

		// patent-free work around: "preload" the stencil buffer with the
		// number of volumes clipped by the near or far plane (z-fail ops),
		// then the traditional depth-pass draw. With wrap inc/dec the
		// interleaved single-pass deltas are order-equivalent to the legacy
		// two-pass sequence (draw_common.cpp:7313-7340). GL op order is
		// (fail, zfail, zpass); Vulkan takes (fail, pass, depthFail)
		if ( !external ) {
			vkCmdSetStencilOp( cmd, frontSidedFace, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_DECREMENT_AND_WRAP,
					VK_STENCIL_OP_DECREMENT_AND_WRAP, VK_COMPARE_OP_ALWAYS );
			vkCmdSetStencilOp( cmd, backSidedFace, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_INCREMENT_AND_WRAP,
					VK_STENCIL_OP_INCREMENT_AND_WRAP, VK_COMPARE_OP_ALWAYS );
			vkCmdDrawIndexed( cmd, (uint32_t)numIndexes, 1, 0, 0, 0 );
			interPass.volumeDrawCount++;
			interPass.volumePreloadCount++;
			backEnd.pc.c_shadowElements++;
			backEnd.pc.c_shadowIndexes += numIndexes;
			backEnd.pc.c_shadowVertexes += tri->numVerts;
		}

		// traditional depth-pass stencil shadows
		vkCmdSetStencilOp( cmd, frontSidedFace, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_INCREMENT_AND_WRAP,
				VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS );
		vkCmdSetStencilOp( cmd, backSidedFace, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_DECREMENT_AND_WRAP,
				VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS );
		vkCmdDrawIndexed( cmd, (uint32_t)numIndexes, 1, 0, 0, 0 );
		interPass.volumeDrawCount++;
		backEnd.pc.c_shadowElements++;
		backEnd.pc.c_shadowIndexes += numIndexes;
		backEnd.pc.c_shadowVertexes += tri->numVerts;
	}

	// exit contract (GL :7430-7443): bias off, GEQUAL/128 with ops KEEP for
	// the light's interactions (reference/masks stay latched from the light
	// entry); cull is re-set per surface by every consumer, so the
	// CT_FRONT_SIDED restore needs no explicit call
	if ( shadowBias ) {
		vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	}
	vkCmdSetStencilOp( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,
			VK_STENCIL_OP_KEEP, VK_COMPARE_OP_GREATER_OR_EQUAL );
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

	// Phase G1: the stencil volume pipeline serves every shadow-casting
	// light the shadow-map path does not admit; a missing pipeline (shader
	// failure) leaves those lights on the unshadowed F1 path
	interPass.pipelineStencilShadow = VK_Exec_StencilShadowPipeline();
	interPass.layoutStencilShadow = VK_Exec_BasePipelineLayout();

	// Phase F2a/F2b: classify + tile the view's shadow-map lights (CPU),
	// then render the atlas + point cubes in a frame-scope interruption
	// BEFORE any batch state is set (the resume path re-establishes the
	// executor baseline). Any failure simply leaves the affected lights on
	// the unshadowed F1 path.
	if ( VK_ShadowMap_PrepareViewLights( viewDef ) > 0 ) {
		interPass.pipelineShadowed = VK_Exec_ShadowInteractionPipeline();
		interPass.pipelinePointShadowed = VK_Exec_PointShadowInteractionPipeline();
		interPass.layoutShadowed = VK_Exec_ShadowInteractionPipelineLayout();
		if ( interPass.layoutShadowed != VK_NULL_HANDLE
				&& ( interPass.pipelineShadowed != VK_NULL_HANDLE || interPass.pipelinePointShadowed != VK_NULL_HANDLE ) ) {
			VK_ShadowMap_RenderAtlas( viewDef );
			interPass.shadowSetAtlas = VK_Exec_ShadowDescriptorSet();
			interPass.shadowPassPrepared = interPass.shadowSetAtlas != VK_NULL_HANDLE;
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

		// Phase F2a/F2b: lights with a rendered atlas tile or depth cube draw
		// their opaque interactions through the matching shadow-receiving
		// pipeline; everything else (gated lights, pool exhaustion, resource
		// failures) stays on the unshadowed F1 path. Both interaction sets
		// sample the combined caster map; translucent receivers stay
		// unshadowed scratch-first.
		const vkShadowLightState_t *shadowState = NULL;
		if ( interPass.shadowPassPrepared ) {
			shadowState = VK_ShadowMap_LightState( vLight );
			if ( shadowState != NULL ) {
				interPass.shadowLightCount++;
			}
		}
		VK_Inter_SelectShadowMode( shadowState );

		// Phase G1: lights the shadow-map path did NOT admit stamp stencil
		// volumes and draw their interactions under the GEQUAL/128 exit
		// contract; admitted lights keep the F2 receiver path untouched.
		// With the retail default r_useShadowMap 0 EVERY shadow-casting
		// light takes the stencil path.
		const bool stencilShadowLight = shadowState == NULL
				&& interPass.pipelineStencilShadow != VK_NULL_HANDLE
				&& r_shadows.GetBool()
				&& ( vLight->globalShadows != NULL || vLight->localShadows != NULL );

		if ( stencilShadowLight ) {
			interPass.stencilLightCount++;

			// scissored stencil clear to 128 over the light rect
			VK_Inter_StencilClear( vLight );

			// this light's interactions draw stencil-tested: GEQUAL ref 128
			// compareMask 255, ops KEEP — GL leaves writeMask 255 but the
			// KEEP ops never write. This is also the invariance state each
			// volume pass enters from and restores to
			vkCmdSetStencilTestEnable( cmd, VK_TRUE );
			vkCmdSetStencilOp( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,
					VK_STENCIL_OP_KEEP, VK_COMPARE_OP_GREATER_OR_EQUAL );
			vkCmdSetStencilCompareMask( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 255 );
			vkCmdSetStencilWriteMask( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 255 );
			vkCmdSetStencilReference( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 128 );

			// stencil ownership order (draw_arb2.cpp:11616-11630): global
			// volumes darken the noSelfShadow (localInteractions) receivers
			// first; the local volumes join before the self-shadowing
			// (globalInteractions) receivers draw. Each volume pass leaves
			// the volume pipeline + depth LEQUAL bound, so the interaction
			// pipeline and the EQUAL opaque depth func are re-established
			// after each one
			VK_StencilShadowPass( vLight->globalShadows );
			vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, interPass.pipelineUnshadowed );
			vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_EQUAL );
			VK_DrawInteractionChain( vLight->localInteractions );

			VK_StencilShadowPass( vLight->localShadows );
			vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, interPass.pipelineUnshadowed );
			vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_EQUAL );
			VK_DrawInteractionChain( vLight->globalInteractions );
		} else {
			// opaque interactions test EQUAL against the depth fill
			vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_EQUAL );
			VK_DrawInteractionChain( vLight->localInteractions );
			VK_DrawInteractionChain( vLight->globalInteractions );
		}

		if ( !r_skipTranslucent.GetBool() ) {
			// translucent receivers keep the light's shadow map (GL default
			// r_shadowMapTranslucentReceivers 1, draw_arb2.cpp:11566)
			VK_Inter_SelectShadowMode( r_shadowMapTranslucentReceivers.GetBool() ? shadowState : NULL );
			// stencil path: translucent interactions keep GEQUAL only when
			// they receive stencil shadows (draw_arb2.cpp:11636-11646,
			// r_stencilTranslucentShadows default 1)
			if ( stencilShadowLight && !r_stencilTranslucentShadows.GetBool() ) {
				vkCmdSetStencilTestEnable( cmd, VK_FALSE );
			}
			// GLS_DEPTHFUNC_LESS maps to glDepthFunc(GL_LEQUAL)
			vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_LESS_OR_EQUAL );
			VK_DrawInteractionChain( vLight->translucentInteractions );
		}

		// stencil reset: the next light (and the ambient walks) start
		// stencil-free
		if ( stencilShadowLight ) {
			vkCmdSetStencilTestEnable( cmd, VK_FALSE );
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

	// one-shot bring-up evidence that stencil shadow volumes drew (Phase G1)
	static bool loggedFirstStencilShadowPass = false;
	if ( !loggedFirstStencilShadowPass && interPass.volumeDrawCount > 0 ) {
		loggedFirstStencilShadowPass = true;
		common->Printf( "Vulkan: first stencil shadow pass: %d volumes (%d preload), %d lights\n",
				interPass.volumeDrawCount, interPass.volumePreloadCount, interPass.stencilLightCount );
		if ( interPass.volumeSkipCount > 0 ) {
			common->Printf( "Vulkan: stencil shadow pass skipped %d prim-batch/cache-less volumes\n",
					interPass.volumeSkipCount );
		}
	}
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
