// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	Vulkan projected-light shadow maps (Phase F2a,
	docs/dev/plans/2026-07-19-vulkan-phase-f.md).

	Scratch-first port of the GL shadow-map backend for PROJECTED and
	PARALLEL lights (draw_arb2.cpp is excluded from the vk module build):

	- ONE module-owned depth atlas (vkCtx.depthFormat, r_shadowMapAtlasSize),
	  single copy: produced and consumed inside the same command buffer,
	  barriers serialize (UNDEFINED -> DEPTH_STENCIL_ATTACHMENT at the
	  interruption start, -> DEPTH_STENCIL_READ_ONLY before resume).
	- Per-view row-scan tile allocator (tile = r_shadowMapSize), reset every
	  view; no static caches, no compose, no CSM, no update budgets.
	- Caster pass per light inside one frame-scope interruption: viewport +
	  scissor to the light's tile (negative-height viewport keeps the GL
	  winding convention), projection = R_ShadowMapClipPlanesToGLMatrix over
	  R_BuildShadowMapProjectedLightState clip planes with the shared
	  VK_FixupClipSpaceZ; the STORED depth is shader-written from the light's
	  depth plane exactly like the GL caster program (in-shader slope-scale +
	  constant offset, r_shadowMapPolygonFactor/Offset).
	- SIMPLIFICATION vs RB_ShadowMapRunPass: ONE combined caster render per
	  light (global + local, static + dynamic chains together) sampled by
	  BOTH interaction sets. The GL LOCAL pass renders global casters only so
	  noSelfShadow (localInteractions) receivers cannot catch their own
	  shadows; under the combined map they can. Documented divergence,
	  revisit with the F2 full pass split.
	- Any resource/tile/render failure leaves the light unshadowed (the
	  Phase F1 path); there is no stencil fallback until Phase G.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../ShadowMapProjected.h"

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
#include "vk_mem_alloc.h"

#include "VulkanDevice.h"
#include "vk_ShadowMap.h"

// vk_GuiExecutor.cpp narrow accessors (vkExec stays file-static there)
VkCommandBuffer VK_Exec_ActiveCmd( void );
int VK_Exec_ActiveFrameSlot( void );
bool VK_Exec_BindTriGeometry( VkCommandBuffer cmd, int slot, const srfTriangles_t *tri );
VkDescriptorSet VK_Exec_ImageDescriptor( unsigned int texnum, bool require2D );
VkPipeline VK_Exec_CasterPipeline( void );
VkPipelineLayout VK_Exec_BasePipelineLayout( void );
bool VK_Exec_BeginMainRendering( bool clearColorDepth );
void VK_Exec_EndMainRendering( void );
bool VK_Exec_UpdateShadowAtlasDescriptors( VkImageView view, VkSampler sampler );
void VK_FixupClipSpaceZ( float dst[ 16 ], const float src[ 16 ] );

/*
====================
Module state
====================
*/
static const int VK_SHADOW_MAX_LIGHTS = 64;

// mirror of the shared 128B caster push block; the alpha matrix rows' unused
// z components carry the two caster depth-offset scalars
typedef struct vkCasterPush_s {
	float			mvp[ 16 ];
	float			depthRow[ 4 ];
	float			alphaS[ 4 ];	// z = slope-scale depth factor
	float			alphaT[ 4 ];	// z = constant depth offset
	float			params[ 4 ];	// x: alpha mode (0 off, 1 greater, -1 less, 2 equal), y: ref, z: scale
} vkCasterPush_t;

typedef struct vkShadowMapState_s {
	VkImage				atlasImage;
	VmaAllocation		atlasAllocation;
	VkImageView			atlasAttachmentView;	// depth + stencil aspects
	VkImageView			atlasSampleView;		// depth aspect only (compare sampling)
	VkSampler			compareSampler;
	int					atlasSize;

	// per-view tile allocator + light table (reset every PrepareViewLights)
	int					nextTileX;
	int					nextTileY;
	int					numLights;
	vkShadowLightState_t lights[ VK_SHADOW_MAX_LIGHTS ];
} vkShadowMapState_t;

static vkShadowMapState_t vkShadow;

/*
====================
Resources
====================
*/
static void VK_ShadowMap_DestroyAtlas( void ) {
	if ( vkCtx.device == VK_NULL_HANDLE ) {
		return;
	}
	if ( vkShadow.atlasSampleView != VK_NULL_HANDLE ) {
		vkDestroyImageView( vkCtx.device, vkShadow.atlasSampleView, NULL );
		vkShadow.atlasSampleView = VK_NULL_HANDLE;
	}
	if ( vkShadow.atlasAttachmentView != VK_NULL_HANDLE ) {
		vkDestroyImageView( vkCtx.device, vkShadow.atlasAttachmentView, NULL );
		vkShadow.atlasAttachmentView = VK_NULL_HANDLE;
	}
	if ( vkShadow.atlasImage != VK_NULL_HANDLE ) {
		vmaDestroyImage( vkCtx.allocator, vkShadow.atlasImage, vkShadow.atlasAllocation );
		vkShadow.atlasImage = VK_NULL_HANDLE;
		vkShadow.atlasAllocation = NULL;
	}
	vkShadow.atlasSize = 0;
}

void VK_ShadowMap_Shutdown( void ) {
	if ( vkCtx.device == VK_NULL_HANDLE ) {
		memset( &vkShadow, 0, sizeof( vkShadow ) );
		return;
	}
	VK_ShadowMap_DestroyAtlas();
	if ( vkShadow.compareSampler != VK_NULL_HANDLE ) {
		vkDestroySampler( vkCtx.device, vkShadow.compareSampler, NULL );
	}
	memset( &vkShadow, 0, sizeof( vkShadow ) );
}

// creates (or resizes on r_shadowMapAtlasSize changes) the depth atlas plus
// the hardware-compare sampler and points the executor's shadow descriptor
// sets at them. Failure leaves every light unshadowed for the view.
static bool VK_ShadowMap_EnsureResources( void ) {
	if ( !vkCtx.initialized || vkCtx.depthFormat == VK_FORMAT_UNDEFINED ) {
		return false;
	}

	int wantedSize = idMath::ClampInt( 2048, 8192, r_shadowMapAtlasSize.GetInteger() );
	const int maxDim = (int)vkCtx.deviceProperties.limits.maxImageDimension2D;
	if ( maxDim > 0 && wantedSize > maxDim ) {
		wantedSize = maxDim;
	}

	if ( vkShadow.atlasImage != VK_NULL_HANDLE && vkShadow.atlasSize == wantedSize ) {
		return true;
	}

	// the depth format must support compare sampling next to attachment use
	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties( vkCtx.physicalDevice, vkCtx.depthFormat, &formatProps );
	if ( ( formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT ) == 0 ) {
		static bool warned = false;
		if ( !warned ) {
			warned = true;
			common->Warning( "Vulkan: depth format %d not sampleable; shadow maps unavailable", (int)vkCtx.depthFormat );
		}
		return false;
	}

	if ( vkShadow.atlasImage != VK_NULL_HANDLE ) {
		// size change: frames in flight may still reference the old atlas and
		// its descriptor writes; this is a rare cvar path, wait it out
		vkDeviceWaitIdle( vkCtx.device );
		VK_Exec_UpdateShadowAtlasDescriptors( VK_NULL_HANDLE, VK_NULL_HANDLE );
		VK_ShadowMap_DestroyAtlas();
	}

	VkImageCreateInfo ici;
	memset( &ici, 0, sizeof( ici ) );
	ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = vkCtx.depthFormat;
	ici.extent.width = (uint32_t)wantedSize;
	ici.extent.height = (uint32_t)wantedSize;
	ici.extent.depth = 1;
	ici.mipLevels = 1;
	ici.arrayLayers = 1;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;

	if ( vmaCreateImage( vkCtx.allocator, &ici, &vaci, &vkShadow.atlasImage, &vkShadow.atlasAllocation, NULL ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow atlas creation failed (%dx%d)", wantedSize, wantedSize );
		vkShadow.atlasImage = VK_NULL_HANDLE;
		return false;
	}

	VkImageViewCreateInfo ivci;
	memset( &ivci, 0, sizeof( ivci ) );
	ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	ivci.image = vkShadow.atlasImage;
	ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ivci.format = vkCtx.depthFormat;
	ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	ivci.subresourceRange.levelCount = 1;
	ivci.subresourceRange.layerCount = 1;
	if ( vkCreateImageView( vkCtx.device, &ivci, NULL, &vkShadow.atlasAttachmentView ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow atlas attachment view creation failed" );
		VK_ShadowMap_DestroyAtlas();
		return false;
	}
	// sampled depth of a combined depth/stencil image must select exactly the
	// depth aspect
	ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	if ( vkCreateImageView( vkCtx.device, &ivci, NULL, &vkShadow.atlasSampleView ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow atlas sample view creation failed" );
		VK_ShadowMap_DestroyAtlas();
		return false;
	}

	if ( vkShadow.compareSampler == VK_NULL_HANDLE ) {
		// hardware LEQUAL compare with LINEAR filtering = 2x2 PCF, the exact
		// GL binding for r_shadowMapDepthCompare (the default path)
		VkSamplerCreateInfo sci;
		memset( &sci, 0, sizeof( sci ) );
		sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sci.magFilter = VK_FILTER_LINEAR;
		sci.minFilter = VK_FILTER_LINEAR;
		sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.compareEnable = VK_TRUE;
		sci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		sci.maxLod = 0.25f;
		if ( vkCreateSampler( vkCtx.device, &sci, NULL, &vkShadow.compareSampler ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: shadow compare sampler creation failed" );
			VK_ShadowMap_DestroyAtlas();
			return false;
		}
	}

	if ( !VK_Exec_UpdateShadowAtlasDescriptors( vkShadow.atlasSampleView, vkShadow.compareSampler ) ) {
		VK_ShadowMap_DestroyAtlas();
		return false;
	}

	vkShadow.atlasSize = wantedSize;
	return true;
}

/*
====================
Tile allocator (row scan, reset per view)
====================
*/
static bool VK_ShadowMap_AllocTile( int tileSize, int &tileX, int &tileY ) {
	if ( vkShadow.nextTileX + tileSize > vkShadow.atlasSize ) {
		vkShadow.nextTileX = 0;
		vkShadow.nextTileY += tileSize;
	}
	if ( vkShadow.nextTileY + tileSize > vkShadow.atlasSize ) {
		return false;
	}
	tileX = vkShadow.nextTileX;
	tileY = vkShadow.nextTileY;
	vkShadow.nextTileX += tileSize;
	return true;
}

/*
====================
VK_ShadowMap_PrepareViewLights

CPU phase: the RB_ShadowMapLightSupportReason-equivalent gate (r_useShadowMap
&& r_shadows, not fog/blend/ambient/noShadows, casts shadows, has receivers
and casters, PROJECTED/PARALLEL class only — point lights explicitly fall
back to the unshadowed F1 path in F2a), then the projected-light state via
the shared front-end helpers and an atlas tile per admitted light.
====================
*/
int VK_ShadowMap_PrepareViewLights( const viewDef_t *viewDef ) {
	vkShadow.numLights = 0;
	vkShadow.nextTileX = 0;
	vkShadow.nextTileY = 0;

	if ( viewDef == NULL || !r_useShadowMap.GetBool() || !r_shadows.GetBool() ) {
		return 0;
	}
	if ( !vkCtx.initialized ) {
		return 0;
	}

	int prepared = 0;
	bool resourcesChecked = false;
	bool resourcesOk = false;

	for ( const viewLight_t *vLight = viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		if ( vkShadow.numLights >= VK_SHADOW_MAX_LIGHTS ) {
			break;
		}
		if ( vLight->lightShader == NULL || vLight->lightShader->IsFogLight() || vLight->lightShader->IsBlendLight() ) {
			continue;
		}
		// policy gate (RB_ShadowMapLightPolicySupportReason)
		if ( vLight->lightDef != NULL
				&& ( vLight->lightDef->parms.noShadows || vLight->lightDef->parms.noDynamicShadows ) ) {
			continue;
		}
		if ( vLight->lightShader->IsAmbientLight() || !vLight->lightShader->LightCastsShadows() ) {
			continue;
		}
		if ( vLight->globalInteractions == NULL && vLight->localInteractions == NULL ) {
			continue;
		}

		const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );
		if ( classification.pointLight ) {
			// F2a: point lights render unshadowed (F1 path); cube passes are F2b
			continue;
		}
		if ( classification.csmEnabled || classification.cascadeCount > 1 ) {
			// scratch-first: no CSM (r_shadowMapCSM defaults 0)
			continue;
		}

		// combined caster set (see the stencil-ownership divergence note above)
		if ( vLight->globalShadowMapCasters == NULL && vLight->localShadowMapCasters == NULL
				&& vLight->globalShadowMapDynamicCasters == NULL && vLight->localShadowMapDynamicCasters == NULL ) {
			continue;
		}

		if ( !resourcesChecked ) {
			resourcesChecked = true;
			resourcesOk = VK_ShadowMap_EnsureResources();
		}
		if ( !resourcesOk ) {
			return 0;
		}

		const int tileSize = idMath::ClampInt( 128, vkShadow.atlasSize, r_shadowMapSize.GetInteger() );

		shadowMapProjectedLightState_t projectedState;
		R_BuildShadowMapProjectedLightState( vLight, viewDef, tileSize, projectedState );
		if ( !projectedState.valid || projectedState.cascadeCount != 1 ) {
			continue;
		}

		int tileX = 0;
		int tileY = 0;
		if ( !VK_ShadowMap_AllocTile( tileSize, tileX, tileY ) ) {
			// atlas exhausted for this view: remaining lights render unshadowed
			continue;
		}

		vkShadowLightState_t &entry = vkShadow.lights[ vkShadow.numLights++ ];
		memset( &entry, 0, sizeof( entry ) );
		entry.vLight = vLight;
		entry.valid = true;
		entry.tileX = tileX;
		entry.tileY = tileY;
		entry.tileSize = tileSize;
		for ( int i = 0 ; i < 4 ; i++ ) {
			entry.clipPlanes[ i ] = projectedState.clipPlanes[ 0 ][ i ];
		}
		R_ShadowMapClipPlanesToGLMatrix( entry.clipPlanes, entry.clipMatrix );

		// composed atlas rect: the caster tile renders through a negative-height
		// viewport (GL winding parity), so tile-local v=0 (NDC y=-1) lands on the
		// tile's BOTTOM image row — the rect's v span is inverted to match
		const float invAtlas = 1.0f / (float)vkShadow.atlasSize;
		entry.invAtlasSize[ 0 ] = invAtlas;
		entry.invAtlasSize[ 1 ] = invAtlas;
		entry.atlasRect[ 0 ] = (float)tileX * invAtlas;
		entry.atlasRect[ 1 ] = (float)( tileY + tileSize ) * invAtlas;
		entry.atlasRect[ 2 ] = (float)( tileX + tileSize ) * invAtlas;
		entry.atlasRect[ 3 ] = (float)tileY * invAtlas;

		entry.texelDepthBias = projectedState.texelDepthBias[ 0 ];
		entry.normalOffsetWorld = projectedState.worldTexelSize[ 0 ]
				* Max( 0.0f, r_shadowMapNormalOffsetScale.GetFloat() );
		prepared++;
	}

	return prepared;
}

const vkShadowLightState_t *VK_ShadowMap_LightState( const viewLight_t *vLight ) {
	for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
		if ( vkShadow.lights[ i ].vLight == vLight ) {
			return vkShadow.lights[ i ].valid ? &vkShadow.lights[ i ] : NULL;
		}
	}
	return NULL;
}

/*
====================
Caster drawing
====================
*/

// r_shadowMapCasterCulling in the executor's GL-parity winding convention
// (negative-height viewport, CCW front): mode 1 stores light-facing
// engine-front faces (cull FRONT), mode 2 (default) stores engine-back faces
// (cull BACK); material twoSided/backSided is always honored
static VkCullModeFlags VK_ShadowMap_CasterCullMode( const idMaterial *shader ) {
	const int mode = idMath::ClampInt( 0, 2, r_shadowMapCasterCulling.GetInteger() );
	const int materialCull = ( shader != NULL ) ? shader->GetCullType() : CT_FRONT_SIDED;
	if ( mode == 0 || materialCull == CT_TWO_SIDED ) {
		return VK_CULL_MODE_NONE;
	}
	bool cullFront = ( mode == 1 );
	if ( materialCull == CT_BACK_SIDED ) {
		cullFront = !cullFront;
	}
	return cullFront ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
}

// port of RB_ShadowMapCanProgramPerforatedCaster minus the cinematic-image
// acceptance: an active alpha-test stage without a static explicit-texgen
// image drops the surface to the solid caster path
static bool VK_ShadowMap_CanProgramPerforatedCaster( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->material == NULL || surf->shaderRegisters == NULL ) {
		return false;
	}
	bool haveActiveStage = false;
	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;
	const int stageCount = shader->GetNumStages();
	for ( int stage = 0 ; stage < stageCount ; stage++ ) {
		const shaderStage_t *pStage = shader->GetStage( stage );
		if ( !pStage->hasAlphaTest || regs[ pStage->conditionRegister ] == 0 ) {
			continue;
		}
		haveActiveStage = true;
		if ( pStage->texture.image == NULL || pStage->texture.texgen != TG_EXPLICIT ) {
			return false;
		}
	}
	return haveActiveStage;
}

static void VK_ShadowMap_SetPushAlphaIdentity( vkCasterPush_t &push ) {
	push.alphaS[ 0 ] = 1.0f;
	push.alphaS[ 1 ] = 0.0f;
	push.alphaS[ 3 ] = 0.0f;
	push.alphaT[ 0 ] = 0.0f;
	push.alphaT[ 1 ] = 1.0f;
	push.alphaT[ 3 ] = 0.0f;
	push.params[ 0 ] = 0.0f;
	push.params[ 1 ] = 0.0f;
	push.params[ 2 ] = 1.0f;
}

static float VK_ShadowMap_AlphaTestModeValue( const int alphaTestMode ) {
	if ( alphaTestMode == GL_LESS ) {
		return -1.0f;
	}
	if ( alphaTestMode == GL_EQUAL ) {
		return 2.0f;
	}
	return 1.0f;
}

typedef struct vkCasterPassCtx_s {
	VkCommandBuffer		cmd;
	int					slot;
	VkPipelineLayout	layout;
	VkDescriptorSet		whiteSet;
	VkDescriptorSet		boundImageSet;
	VkCullModeFlags		boundCullMode;
	float				slopeFactor;
	float				constOffset;
} vkCasterPassCtx_t;

static void VK_ShadowMap_CasterDraw( vkCasterPassCtx_t &ctx, const vkCasterPush_t &push,
		VkDescriptorSet imageSet, const srfTriangles_t *casterGeo ) {
	if ( imageSet != ctx.boundImageSet ) {
		ctx.boundImageSet = imageSet;
		vkCmdBindDescriptorSets( ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.layout, 0, 1, &imageSet, 0, NULL );
	}
	vkCmdPushConstants( ctx.cmd, ctx.layout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
	vkCmdDrawIndexed( ctx.cmd, (uint32_t)casterGeo->numIndexes, 1, 0, 0, 0 );
}

// one caster chain into the bound tile; returns the number of casters drawn
// (chain walk = RB_ShadowMapDrawCasterChain, single cascade)
static int VK_ShadowMap_DrawCasterChain( vkCasterPassCtx_t &ctx, const vkShadowLightState_t &light,
		const drawSurf_t *surf ) {
	int drawnCasters = 0;

	for ( ; surf != NULL ; surf = surf->nextOnLight ) {
		if ( surf->geo == NULL || surf->space == NULL ) {
			continue;
		}
		// casters draw the AMBIENT geometry (full tri->indexes), never the
		// light-tris subset (RB_ShadowMapResolveCasterDrawData)
		srfTriangles_t *casterGeo = surf->geo->ambientSurface != NULL
				? surf->geo->ambientSurface : const_cast<srfTriangles_t *>( surf->geo );
		if ( casterGeo == NULL || casterGeo->numVerts <= 0 || casterGeo->numIndexes <= 0 || casterGeo->indexes == NULL ) {
			continue;
		}
		if ( casterGeo->ambientCache == NULL ) {
			if ( casterGeo->verts == NULL || !R_CreateAmbientCache( casterGeo, false ) ) {
				continue;
			}
		}
		if ( !VK_Exec_BindTriGeometry( ctx.cmd, ctx.slot, casterGeo ) ) {
			continue;
		}

		// light-space MVP: the light's clip matrix over the model matrix, with
		// the shared clip-z fixup; the stored depth comes from the localized
		// depth plane instead (shader-written, GL caster parity)
		vkCasterPush_t push;
		memset( &push, 0, sizeof( push ) );
		float mvpGL[ 16 ];
		myGlMultMatrix( surf->space->modelMatrix, light.clipMatrix, mvpGL );
		VK_FixupClipSpaceZ( push.mvp, mvpGL );

		idPlane localDepthPlane;
		R_GlobalPlaneToLocal( surf->space->modelMatrix, light.clipPlanes[ 2 ], localDepthPlane );
		memcpy( push.depthRow, localDepthPlane.ToFloatPtr(), sizeof( push.depthRow ) );

		VK_ShadowMap_SetPushAlphaIdentity( push );
		push.alphaS[ 2 ] = ctx.slopeFactor;
		push.alphaT[ 2 ] = ctx.constOffset;

		const VkCullModeFlags cullMode = VK_ShadowMap_CasterCullMode( surf->material );
		if ( cullMode != ctx.boundCullMode ) {
			ctx.boundCullMode = cullMode;
			vkCmdSetCullMode( ctx.cmd, cullMode );
		}

		const idMaterial *shader = surf->material;
		if ( shader != NULL && shader->Coverage() == MC_PERFORATED && VK_ShadowMap_CanProgramPerforatedCaster( surf ) ) {
			const float *regs = surf->shaderRegisters;
			bool didDraw = false;
			const int stageCount = shader->GetNumStages();
			for ( int stage = 0 ; stage < stageCount ; stage++ ) {
				const shaderStage_t *pStage = shader->GetStage( stage );
				if ( !pStage->hasAlphaTest || regs[ pStage->conditionRegister ] == 0 ) {
					continue;
				}
				didDraw = true;
				const float alphaScale = regs[ pStage->color.registers[ 3 ] ];
				if ( alphaScale <= 0.0f ) {
					continue;
				}
				VkDescriptorSet stageSet = VK_Exec_ImageDescriptor( pStage->texture.image->GetDeviceHandle(), true );
				if ( stageSet == VK_NULL_HANDLE ) {
					continue;
				}
				if ( pStage->texture.hasMatrix ) {
					push.alphaS[ 0 ] = regs[ pStage->texture.matrix[ 0 ][ 0 ] ];
					push.alphaS[ 1 ] = regs[ pStage->texture.matrix[ 0 ][ 1 ] ];
					push.alphaS[ 3 ] = regs[ pStage->texture.matrix[ 0 ][ 2 ] ];
					push.alphaT[ 0 ] = regs[ pStage->texture.matrix[ 1 ][ 0 ] ];
					push.alphaT[ 1 ] = regs[ pStage->texture.matrix[ 1 ][ 1 ] ];
					push.alphaT[ 3 ] = regs[ pStage->texture.matrix[ 1 ][ 2 ] ];
				}
				push.params[ 0 ] = VK_ShadowMap_AlphaTestModeValue( pStage->alphaTestMode );
				push.params[ 1 ] = regs[ pStage->alphaTestRegister ];
				push.params[ 2 ] = alphaScale;
				VK_ShadowMap_CasterDraw( ctx, push, stageSet, casterGeo );
				// restore the solid defaults for the next stage/surface
				VK_ShadowMap_SetPushAlphaIdentity( push );
			}
			if ( !didDraw ) {
				VK_ShadowMap_CasterDraw( ctx, push, ctx.whiteSet, casterGeo );
			}
			drawnCasters++;
			continue;
		}

		VK_ShadowMap_CasterDraw( ctx, push, ctx.whiteSet, casterGeo );
		drawnCasters++;
	}

	return drawnCasters;
}

/*
====================
VK_ShadowMap_RenderAtlas

GPU phase: suspend the main rendering scope, render every prepared light's
casters into its atlas tile inside one depth-only dynamic-rendering scope,
transition the atlas for fragment compare sampling, and resume the main
scope with loadOp LOAD.
====================
*/
void VK_ShadowMap_RenderAtlas( const viewDef_t *viewDef ) {
	if ( vkShadow.numLights <= 0 || vkShadow.atlasImage == VK_NULL_HANDLE ) {
		return;
	}

	VkCommandBuffer cmd = VK_Exec_ActiveCmd();
	VkPipeline casterPipeline = VK_Exec_CasterPipeline();
	VkDescriptorSet whiteSet = VK_NULL_HANDLE;
	if ( globalImages->whiteImage != NULL ) {
		whiteSet = VK_Exec_ImageDescriptor( globalImages->whiteImage->GetDeviceHandle(), true );
	}
	if ( cmd == VK_NULL_HANDLE || casterPipeline == VK_NULL_HANDLE || whiteSet == VK_NULL_HANDLE ) {
		for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
			vkShadow.lights[ i ].valid = false;
		}
		return;
	}

	VK_Exec_EndMainRendering();

	// atlas -> depth attachment; contents are transient (loadOp CLEAR below),
	// so UNDEFINED discards last view's tiles. srcStage covers the previous
	// view's fragment-shader reads of the old contents.
	{
		VkImageMemoryBarrier2 toAttachment;
		memset( &toAttachment, 0, sizeof( toAttachment ) );
		toAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		toAttachment.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
				| VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		toAttachment.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		toAttachment.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		toAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		toAttachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		toAttachment.image = vkShadow.atlasImage;
		toAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		toAttachment.subresourceRange.levelCount = 1;
		toAttachment.subresourceRange.layerCount = 1;
		VkDependencyInfo dep;
		memset( &dep, 0, sizeof( dep ) );
		dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dep.imageMemoryBarrierCount = 1;
		dep.pImageMemoryBarriers = &toAttachment;
		vkCmdPipelineBarrier2( cmd, &dep );
	}

	// one depth-only scope over the whole atlas; the full-surface CLEAR
	// covers every tile (each tile is written at most once per view)
	VkRenderingAttachmentInfo depth;
	memset( &depth, 0, sizeof( depth ) );
	depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depth.imageView = vkShadow.atlasAttachmentView;
	depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth.clearValue.depthStencil.depth = 1.0f;
	depth.clearValue.depthStencil.stencil = 0;

	VkRenderingInfo ri;
	memset( &ri, 0, sizeof( ri ) );
	ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	ri.renderArea.extent.width = (uint32_t)vkShadow.atlasSize;
	ri.renderArea.extent.height = (uint32_t)vkShadow.atlasSize;
	ri.layerCount = 1;
	ri.pDepthAttachment = &depth;
	ri.pStencilAttachment = &depth;
	vkCmdBeginRendering( cmd, &ri );

	vkCasterPassCtx_t ctx;
	memset( &ctx, 0, sizeof( ctx ) );
	ctx.cmd = cmd;
	ctx.slot = VK_Exec_ActiveFrameSlot();
	ctx.layout = VK_Exec_BasePipelineLayout();
	ctx.whiteSet = whiteSet;
	ctx.boundCullMode = (VkCullModeFlags)~0u;
	ctx.slopeFactor = r_shadowMapPolygonFactor.GetFloat();
	// pre-scaled to one resolvable depth-buffer step (glPolygonOffset parity)
	ctx.constOffset = r_shadowMapPolygonOffset.GetFloat() * ( 1.0f / 16777216.0f );

	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, casterPipeline );
	vkCmdSetDepthTestEnable( cmd, VK_TRUE );
	vkCmdSetDepthWriteEnable( cmd, VK_TRUE );
	vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_LESS_OR_EQUAL );
	vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	vkCmdSetDepthBias( cmd, 0.0f, 0.0f, 0.0f );
	vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );

	for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
		vkShadowLightState_t &light = vkShadow.lights[ i ];
		if ( !light.valid ) {
			continue;
		}
		const viewLight_t *vLight = light.vLight;

		// negative-height viewport on the tile: GL winding/cull parity, with
		// the composed atlas rect's inverted v span compensating
		VkViewport viewport;
		viewport.x = (float)light.tileX;
		viewport.y = (float)( light.tileY + light.tileSize );
		viewport.width = (float)light.tileSize;
		viewport.height = -(float)light.tileSize;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport( cmd, 0, 1, &viewport );

		VkRect2D scissor;
		scissor.offset.x = light.tileX;
		scissor.offset.y = light.tileY;
		scissor.extent.width = (uint32_t)light.tileSize;
		scissor.extent.height = (uint32_t)light.tileSize;
		vkCmdSetScissor( cmd, 0, 1, &scissor );

		int drawnCasters = 0;
		drawnCasters += VK_ShadowMap_DrawCasterChain( ctx, light, vLight->globalShadowMapCasters );
		drawnCasters += VK_ShadowMap_DrawCasterChain( ctx, light, vLight->localShadowMapCasters );
		drawnCasters += VK_ShadowMap_DrawCasterChain( ctx, light, vLight->globalShadowMapDynamicCasters );
		drawnCasters += VK_ShadowMap_DrawCasterChain( ctx, light, vLight->localShadowMapDynamicCasters );
		if ( drawnCasters <= 0 ) {
			// nothing rendered into the tile: sampling an all-far map is
			// harmless but pointless; keep the light unshadowed (GL treats an
			// all-skipped caster set as a render miss)
			light.valid = false;
		}

		// one-shot bring-up evidence the caster pass emitted real work
		static bool loggedFirstShadowPass = false;
		if ( !loggedFirstShadowPass && light.valid ) {
			loggedFirstShadowPass = true;
			common->Printf( "Vulkan: first shadow-map pass: %d shadow lights, tile %d, atlas tile at (%d,%d) of %d\n",
					vkShadow.numLights, light.tileSize, light.tileX, light.tileY, vkShadow.atlasSize );
		}
	}

	vkCmdEndRendering( cmd );

	// atlas -> fragment compare sampling; the extra memory barrier orders the
	// suspended main scope's color/depth attachment writes against the
	// resumed scope's loads (dynamic rendering scopes do not sync themselves)
	{
		VkImageMemoryBarrier2 toSampled;
		memset( &toSampled, 0, sizeof( toSampled ) );
		toSampled.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		toSampled.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		toSampled.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		toSampled.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		toSampled.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		toSampled.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		toSampled.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		toSampled.image = vkShadow.atlasImage;
		toSampled.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		toSampled.subresourceRange.levelCount = 1;
		toSampled.subresourceRange.layerCount = 1;

		VkMemoryBarrier2 attachmentOrder;
		memset( &attachmentOrder, 0, sizeof( attachmentOrder ) );
		attachmentOrder.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
		attachmentOrder.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
				| VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		attachmentOrder.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		attachmentOrder.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
				| VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		attachmentOrder.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
				| VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

		VkDependencyInfo dep;
		memset( &dep, 0, sizeof( dep ) );
		dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dep.memoryBarrierCount = 1;
		dep.pMemoryBarriers = &attachmentOrder;
		dep.imageMemoryBarrierCount = 1;
		dep.pImageMemoryBarriers = &toSampled;
		vkCmdPipelineBarrier2( cmd, &dep );
	}

	VK_Exec_BeginMainRendering( false );
	(void)viewDef;
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
