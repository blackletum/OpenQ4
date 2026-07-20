// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	Vulkan shadow maps (Phase F2a projected + F2b point,
	docs/dev/plans/2026-07-19-vulkan-phase-f.md).

	Scratch-first port of the GL shadow-map backend (draw_arb2.cpp is
	excluded from the vk module build):

	- PROJECTED/PARALLEL lights: ONE module-owned depth atlas
	  (vkCtx.depthFormat, r_shadowMapAtlasSize), single copy: produced and
	  consumed inside the same command buffer, barriers serialize
	  (UNDEFINED -> DEPTH_STENCIL_ATTACHMENT at the interruption start,
	  -> DEPTH_STENCIL_READ_ONLY before resume). Per-view row-scan tile
	  allocator (tile = r_shadowMapSize), reset every view; no static
	  caches, no compose, no CSM, no update budgets. Caster pass per light
	  inside one frame-scope interruption: viewport + scissor to the
	  light's tile (negative-height viewport keeps the GL winding
	  convention), projection = R_ShadowMapClipPlanesToGLMatrix over
	  R_BuildShadowMapProjectedLightState clip planes with the shared
	  VK_FixupClipSpaceZ; the STORED depth is shader-written from the
	  light's depth plane exactly like the GL caster program (in-shader
	  slope-scale + constant offset, r_shadowMapPolygonFactor/Offset).
	- POINT lights (F2b, the dominant Q4 light class): a small per-view
	  pool of module-owned depth CUBES (vkCtx.depthFormat, 6 layers,
	  face size r_shadowMapPointSize), claimed in view-light order and
	  reset per view like the atlas tiles. Six depth-only rendering scopes
	  per light (one per face layer view, loadOp CLEAR) inside the same
	  frame-scope interruption; face view matrices ported from
	  RB_PointShadowMapBuildViewAxis/ModelViewMatrix, analytic face
	  projection from RB_PointShadowMapBuildProjectionMatrix in the
	  VK_FixupClipSpaceZ convention. The stored depth is the NORMALIZED
	  RADIAL DISTANCE written by the caster fragment stage — the GL
	  hardware-compare (r_shadowMapPointDepthCompare, default) contract;
	  the packed-color radial fallback path is NOT implemented. Faces use
	  a POSITIVE-height viewport (the GL cube-face row mapping: NDC y=-1
	  lands on texel row 0), which flips the winding convention to
	  CLOCKWISE-front for the point scopes. Receivers sample through
	  samplerCubeShadow with the shared LINEAR/LEQUAL compare sampler.
	- SIMPLIFICATION vs RB_ShadowMapRunPass: ONE combined caster render per
	  light (global + local, static + dynamic chains together) sampled by
	  BOTH interaction sets. The GL LOCAL pass renders global casters only so
	  noSelfShadow (localInteractions) receivers cannot catch their own
	  shadows; under the combined map they can. Documented divergence,
	  revisit with the F2 full pass split.
	- Further F2b reductions (documented): no depth clamp on the caster
	  pipeline, so the near plane uses the GL !DepthClampAvailable branch
	  (clamp max 4.0); the per-face caster frustum cull
	  (RB_PointShadowMapCasterOutsideFace) is skipped — every caster draws
	  into all six faces (identical output, extra vertex work); the
	  rotated multi-tap receiver disc (r_shadowMapPointFilterRadius/Taps)
	  reduces to the single hardware 2x2 PCF tap, matching the F2a
	  projected receiver reduction.
	- Any resource/pool/render failure leaves the light unshadowed (the
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
VkPipeline VK_Exec_PointCasterPipeline( void );
VkPipelineLayout VK_Exec_BasePipelineLayout( void );
bool VK_Exec_BeginMainRendering( bool clearColorDepth );
void VK_Exec_EndMainRendering( void );
bool VK_Exec_UpdateShadowAtlasDescriptors( VkImageView view, VkSampler sampler );
bool VK_Exec_CreateShadowCubeSets( VkImageView cubeView, VkSampler sampler, VkDescriptorSet sets[ VK_FRAMES_IN_FLIGHT ] );
void VK_Exec_FreeShadowCubeSets( VkDescriptorSet sets[ VK_FRAMES_IN_FLIGHT ] );
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

// one point-light depth cube: 6 attachment layers + a cube compare view
// (Phase F2b); pool entries are created on demand and reused across views
typedef struct vkPointShadowCube_s {
	VkImage				image;
	VmaAllocation		allocation;
	VkImageView			cubeSampleView;					// cube view, depth aspect (samplerCubeShadow)
	VkImageView			faceViews[ 6 ];					// per-face 2D layer views (depth attachment)
	VkDescriptorSet		sets[ VK_FRAMES_IN_FLIGHT ];	// set-7 sets (cube + shadow-block ring)
} vkPointShadowCube_t;

typedef struct vkShadowMapState_s {
	VkImage				atlasImage;
	VmaAllocation		atlasAllocation;
	VkImageView			atlasAttachmentView;	// depth + stencil aspects
	VkImageView			atlasSampleView;		// depth aspect only (compare sampling)
	VkSampler			compareSampler;
	int					atlasSize;

	// point cube pool (F2b); faceSize 0 = nothing built yet
	vkPointShadowCube_t	pointCubes[ VK_SHADOW_MAX_POINT_CUBES ];
	int					pointCubeFaceSize;

	// per-view tile allocator + light table (reset every PrepareViewLights)
	int					nextTileX;
	int					nextTileY;
	int					pointLightsUsed;		// cube pool cursor
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

static void VK_ShadowMap_DestroyPointCube( vkPointShadowCube_t &cube ) {
	VK_Exec_FreeShadowCubeSets( cube.sets );
	for ( int f = 0 ; f < 6 ; f++ ) {
		if ( cube.faceViews[ f ] != VK_NULL_HANDLE ) {
			vkDestroyImageView( vkCtx.device, cube.faceViews[ f ], NULL );
		}
	}
	if ( cube.cubeSampleView != VK_NULL_HANDLE ) {
		vkDestroyImageView( vkCtx.device, cube.cubeSampleView, NULL );
	}
	if ( cube.image != VK_NULL_HANDLE ) {
		vmaDestroyImage( vkCtx.allocator, cube.image, cube.allocation );
	}
	memset( &cube, 0, sizeof( cube ) );
}

static void VK_ShadowMap_DestroyPointCubes( void ) {
	if ( vkCtx.device == VK_NULL_HANDLE ) {
		return;
	}
	for ( int i = 0 ; i < VK_SHADOW_MAX_POINT_CUBES ; i++ ) {
		VK_ShadowMap_DestroyPointCube( vkShadow.pointCubes[ i ] );
	}
	vkShadow.pointCubeFaceSize = 0;
}

void VK_ShadowMap_Shutdown( void ) {
	if ( vkCtx.device == VK_NULL_HANDLE ) {
		memset( &vkShadow, 0, sizeof( vkShadow ) );
		return;
	}
	VK_ShadowMap_DestroyAtlas();
	VK_ShadowMap_DestroyPointCubes();
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

// creates (or resizes on r_shadowMapPointSize changes) one pool slot's depth
// cube: 6 CUBE_COMPATIBLE layers of vkCtx.depthFormat, a cube view for
// samplerCubeShadow compare sampling, per-face 2D layer views for the caster
// scopes, and the per-frame-slot set-7 descriptor sets. Requires
// VK_ShadowMap_EnsureResources to have succeeded (compare sampler + format
// checks). Failure leaves the light unshadowed.
static bool VK_ShadowMap_EnsurePointCube( int index ) {
	if ( index < 0 || index >= VK_SHADOW_MAX_POINT_CUBES || vkShadow.compareSampler == VK_NULL_HANDLE ) {
		return false;
	}

	// RB_ShadowMapPointSizeValue parity
	int wantedSize = idMath::ClampInt( 128, 2048, r_shadowMapPointSize.GetInteger() );
	const int maxDim = (int)vkCtx.deviceProperties.limits.maxImageDimensionCube;
	if ( maxDim > 0 && wantedSize > maxDim ) {
		wantedSize = maxDim;
	}

	if ( vkShadow.pointCubeFaceSize != 0 && vkShadow.pointCubeFaceSize != wantedSize ) {
		// face-size change: frames in flight may still sample the old cubes
		// and their descriptor writes; this is a rare cvar path, wait it out
		vkDeviceWaitIdle( vkCtx.device );
		VK_ShadowMap_DestroyPointCubes();
	}
	vkShadow.pointCubeFaceSize = wantedSize;

	vkPointShadowCube_t &cube = vkShadow.pointCubes[ index ];
	if ( cube.image != VK_NULL_HANDLE ) {
		return true;
	}

	VkImageCreateInfo ici;
	memset( &ici, 0, sizeof( ici ) );
	ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = vkCtx.depthFormat;
	ici.extent.width = (uint32_t)wantedSize;
	ici.extent.height = (uint32_t)wantedSize;
	ici.extent.depth = 1;
	ici.mipLevels = 1;
	ici.arrayLayers = 6;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;

	if ( vmaCreateImage( vkCtx.allocator, &ici, &vaci, &cube.image, &cube.allocation, NULL ) != VK_SUCCESS ) {
		static bool warnedCube = false;
		if ( !warnedCube ) {
			warnedCube = true;
			common->Warning( "Vulkan: point shadow cube creation failed (%dx%d x6)", wantedSize, wantedSize );
		}
		cube.image = VK_NULL_HANDLE;
		return false;
	}

	VkImageViewCreateInfo ivci;
	memset( &ivci, 0, sizeof( ivci ) );
	ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	ivci.image = cube.image;
	ivci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	ivci.format = vkCtx.depthFormat;
	// sampled depth of a combined depth/stencil image must select exactly
	// the depth aspect
	ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	ivci.subresourceRange.levelCount = 1;
	ivci.subresourceRange.layerCount = 6;
	if ( vkCreateImageView( vkCtx.device, &ivci, NULL, &cube.cubeSampleView ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: point shadow cube view creation failed" );
		VK_ShadowMap_DestroyPointCube( cube );
		return false;
	}

	for ( int f = 0 ; f < 6 ; f++ ) {
		ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		ivci.subresourceRange.baseArrayLayer = (uint32_t)f;
		ivci.subresourceRange.layerCount = 1;
		if ( vkCreateImageView( vkCtx.device, &ivci, NULL, &cube.faceViews[ f ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: point shadow cube face view creation failed" );
			VK_ShadowMap_DestroyPointCube( cube );
			return false;
		}
	}

	if ( !VK_Exec_CreateShadowCubeSets( cube.cubeSampleView, vkShadow.compareSampler, cube.sets ) ) {
		VK_ShadowMap_DestroyPointCube( cube );
		return false;
	}

	return true;
}

/*
====================
Point-light math (ports of the excluded draw_arb2.cpp helpers)
====================
*/

// port of RB_PointShadowMapLightFar (draw_arb2.cpp:7358): the padded radial
// far envelope both the caster and the receiver normalize against
static float VK_ShadowMap_PointLightFar( const viewLight_t *vLight ) {
	idVec3 adjustedRadius = vLight->lightRadius;
	if ( vLight->lightDef != NULL ) {
		const renderLight_t &parms = vLight->lightDef->parms;
		adjustedRadius[0] = parms.lightRadius[0] + idMath::Fabs( parms.lightCenter[0] );
		adjustedRadius[1] = parms.lightRadius[1] + idMath::Fabs( parms.lightCenter[1] );
		adjustedRadius[2] = parms.lightRadius[2] + idMath::Fabs( parms.lightCenter[2] );
	}

	return Max( adjustedRadius.Length() * r_shadowMapPointFarScale.GetFloat(), 1.0f );
}

// port of RB_PointShadowMapBuildViewAxis (draw_arb2.cpp:7303): the GL cube
// face view axes. Combined with the GL viewport row mapping (positive-height
// viewport: NDC y=-1 -> texel row 0) these produce the exact GL/Vulkan cube
// face (s,t) layout — the specs share the face selection table.
static void VK_ShadowMap_PointFaceViewAxis( const int cubeFace, idMat3 &axis ) {
	memset( &axis, 0, sizeof( axis ) );

	switch ( cubeFace ) {
	case 0:
		axis[0][0] = 1.0f;
		axis[1][2] = 1.0f;
		axis[2][1] = -1.0f;
		break;
	case 1:
		axis[0][0] = -1.0f;
		axis[1][2] = -1.0f;
		axis[2][1] = -1.0f;
		break;
	case 2:
		axis[0][1] = 1.0f;
		axis[1][0] = -1.0f;
		axis[2][2] = 1.0f;
		break;
	case 3:
		axis[0][1] = -1.0f;
		axis[1][0] = -1.0f;
		axis[2][2] = -1.0f;
		break;
	case 4:
		axis[0][2] = 1.0f;
		axis[1][0] = -1.0f;
		axis[2][1] = -1.0f;
		break;
	default:
		axis[0][2] = -1.0f;
		axis[1][0] = 1.0f;
		axis[2][1] = -1.0f;
		break;
	}
}

// port of RB_PointShadowMapBuildModelViewMatrix (draw_arb2.cpp:7340): the
// world -> face-view matrix through the shared R_SetViewMatrix, a rigid
// transform centered on the light origin (so view-space length == world
// radial distance)
static void VK_ShadowMap_PointFaceViewMatrix( const idVec3 &origin, const int cubeFace, float matrix[ 16 ] ) {
	viewDef_t shadowView;
	memset( &shadowView, 0, sizeof( shadowView ) );
	shadowView.renderView.vieworg = origin;
	VK_ShadowMap_PointFaceViewAxis( cubeFace, shadowView.renderView.viewaxis );
	R_SetViewMatrix( &shadowView );
	memcpy( matrix, shadowView.worldSpace.modelViewMatrix, sizeof( shadowView.worldSpace.modelViewMatrix ) );
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
and casters), then per class: POINT lights (classification.pointLight, gated
on r_shadowMapPointLights like the GL support reason) claim a cube from the
per-view pool; PROJECTED/PARALLEL lights build the projected state via the
shared front-end helpers and take an atlas tile.
====================
*/
int VK_ShadowMap_PrepareViewLights( const viewDef_t *viewDef ) {
	vkShadow.numLights = 0;
	vkShadow.nextTileX = 0;
	vkShadow.nextTileY = 0;
	vkShadow.pointLightsUsed = 0;

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

		if ( classification.pointLight ) {
			// F2b: point lights render into a pool cube; the classification
			// already excludes parallel (sun) lights (pointLight && !parallel).
			// r_shadowMapPointLights is the GL support-reason gate; pool
			// exhaustion and cube failures leave the light unshadowed.
			if ( !r_shadowMapPointLights.GetBool() ) {
				continue;
			}
			if ( vkShadow.pointLightsUsed >= VK_SHADOW_MAX_POINT_CUBES ) {
				continue;
			}
			if ( !VK_ShadowMap_EnsurePointCube( vkShadow.pointLightsUsed ) ) {
				continue;
			}

			const int faceSize = vkShadow.pointCubeFaceSize;
			vkShadowLightState_t &entry = vkShadow.lights[ vkShadow.numLights++ ];
			memset( &entry, 0, sizeof( entry ) );
			entry.vLight = vLight;
			entry.valid = true;
			entry.pointLight = true;
			entry.cubeIndex = vkShadow.pointLightsUsed++;
			entry.tileSize = faceSize;
			entry.pointFar = VK_ShadowMap_PointLightFar( vLight );
			entry.pointSet = vkShadow.pointCubes[ entry.cubeIndex ].sets[ VK_Exec_ActiveFrameSlot() ];
			// receiver bias scalars (RB_GLSLPointShadowMap_CreateDrawInteractions):
			// texel-aware depth bias plus the per-distance normal-offset texel
			// factor (a cube face spans 2*distance across faceSize texels)
			entry.texelDepthBias = Max( 0.0f, r_shadowMapTexelBiasScale.GetFloat() ) / (float)Max( 1, faceSize );
			entry.normalOffsetWorld = 2.0f * Max( 0.0f, r_shadowMapNormalOffsetScale.GetFloat() ) / (float)Max( 1, faceSize );
			prepared++;
			continue;
		}

		if ( classification.csmEnabled || classification.cascadeCount > 1 ) {
			// scratch-first: no CSM (r_shadowMapCSM defaults 0)
			continue;
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

// casters draw the AMBIENT geometry (full tri->indexes), never the
// light-tris subset (RB_ShadowMapResolveCasterDrawData); returns NULL when
// the surface cannot produce a bound-able caster
static srfTriangles_t *VK_ShadowMap_ResolveCasterGeo( const drawSurf_t *surf ) {
	if ( surf->geo == NULL || surf->space == NULL ) {
		return NULL;
	}
	srfTriangles_t *casterGeo = surf->geo->ambientSurface != NULL
			? surf->geo->ambientSurface : const_cast<srfTriangles_t *>( surf->geo );
	if ( casterGeo == NULL || casterGeo->numVerts <= 0 || casterGeo->numIndexes <= 0 || casterGeo->indexes == NULL ) {
		return NULL;
	}
	if ( casterGeo->ambientCache == NULL ) {
		if ( casterGeo->verts == NULL || !R_CreateAmbientCache( casterGeo, false ) ) {
			return NULL;
		}
	}
	return casterGeo;
}

// per-surface tail shared by the projected and point chains: material cull
// mode, then the perforated alpha-stage walk or the solid draw. The caller
// fills push.mvp/depthRow and the depth-offset scalars; the alpha rows must
// hold the solid identity on entry.
static void VK_ShadowMap_DrawResolvedCaster( vkCasterPassCtx_t &ctx, vkCasterPush_t &push,
		const drawSurf_t *surf, const srfTriangles_t *casterGeo ) {
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
		return;
	}

	VK_ShadowMap_CasterDraw( ctx, push, ctx.whiteSet, casterGeo );
}

// one caster chain into the bound tile; returns the number of casters drawn
// (chain walk = RB_ShadowMapDrawCasterChain, single cascade)
static int VK_ShadowMap_DrawCasterChain( vkCasterPassCtx_t &ctx, const vkShadowLightState_t &light,
		const drawSurf_t *surf ) {
	int drawnCasters = 0;

	for ( ; surf != NULL ; surf = surf->nextOnLight ) {
		srfTriangles_t *casterGeo = VK_ShadowMap_ResolveCasterGeo( surf );
		if ( casterGeo == NULL ) {
			continue;
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

		VK_ShadowMap_DrawResolvedCaster( ctx, push, surf, casterGeo );
		drawnCasters++;
	}

	return drawnCasters;
}

// one caster chain into the bound cube face (RB_PointShadowMapDrawCasterChain
// minus the per-face frustum cull — see the header divergence note). The push
// mvp is the model -> face-view matrix; the shader projects analytically
// through projRow and stores the radial view-space distance / far.
static int VK_ShadowMap_DrawPointCasterChain( vkCasterPassCtx_t &ctx, const float faceViewMatrix[ 16 ],
		const float projRow[ 2 ], const float farClip, const drawSurf_t *surf ) {
	int drawnCasters = 0;

	for ( ; surf != NULL ; surf = surf->nextOnLight ) {
		srfTriangles_t *casterGeo = VK_ShadowMap_ResolveCasterGeo( surf );
		if ( casterGeo == NULL ) {
			continue;
		}
		if ( !VK_Exec_BindTriGeometry( ctx.cmd, ctx.slot, casterGeo ) ) {
			continue;
		}

		vkCasterPush_t push;
		memset( &push, 0, sizeof( push ) );
		myGlMultMatrix( surf->space->modelMatrix, faceViewMatrix, push.mvp );
		push.depthRow[ 0 ] = projRow[ 0 ];
		push.depthRow[ 1 ] = projRow[ 1 ];
		push.depthRow[ 2 ] = farClip;

		VK_ShadowMap_SetPushAlphaIdentity( push );
		push.alphaS[ 2 ] = ctx.slopeFactor;
		push.alphaT[ 2 ] = ctx.constOffset;

		VK_ShadowMap_DrawResolvedCaster( ctx, push, surf, casterGeo );
		drawnCasters++;
	}

	return drawnCasters;
}

/*
====================
VK_ShadowMap_RenderAtlas

GPU phase: suspend the main rendering scope, render every prepared
projected light's casters into its atlas tile inside one depth-only
dynamic-rendering scope, render every prepared point light's casters into
its depth cube (six per-face scopes), transition everything for fragment
compare sampling, and resume the main scope with loadOp LOAD.
====================
*/

// depth/stencil image -> attachment; contents are transient (loadOp CLEAR),
// so UNDEFINED discards the previous view's data. srcStage covers the
// previous view's fragment-shader compare reads of the old contents.
static void VK_ShadowMap_BarrierToAttachment( VkCommandBuffer cmd, VkImage image, uint32_t layerCount ) {
	VkImageMemoryBarrier2 toAttachment;
	memset( &toAttachment, 0, sizeof( toAttachment ) );
	toAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toAttachment.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
			| VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	toAttachment.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	toAttachment.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	toAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toAttachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	toAttachment.image = image;
	toAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	toAttachment.subresourceRange.levelCount = 1;
	toAttachment.subresourceRange.layerCount = layerCount;
	VkDependencyInfo dep;
	memset( &dep, 0, sizeof( dep ) );
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &toAttachment;
	vkCmdPipelineBarrier2( cmd, &dep );
}

// depth/stencil image -> fragment compare sampling (the layout the shadow
// descriptor sets declare)
static void VK_ShadowMap_BarrierToSampled( VkCommandBuffer cmd, VkImage image, uint32_t layerCount ) {
	VkImageMemoryBarrier2 toSampled;
	memset( &toSampled, 0, sizeof( toSampled ) );
	toSampled.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toSampled.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	toSampled.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	toSampled.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
	toSampled.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
	toSampled.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	toSampled.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	toSampled.image = image;
	toSampled.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	toSampled.subresourceRange.levelCount = 1;
	toSampled.subresourceRange.layerCount = layerCount;
	VkDependencyInfo dep;
	memset( &dep, 0, sizeof( dep ) );
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &toSampled;
	vkCmdPipelineBarrier2( cmd, &dep );
}

void VK_ShadowMap_RenderAtlas( const viewDef_t *viewDef ) {
	if ( vkShadow.numLights <= 0 || vkShadow.atlasImage == VK_NULL_HANDLE ) {
		return;
	}

	VkCommandBuffer cmd = VK_Exec_ActiveCmd();
	VkPipeline casterPipeline = VK_Exec_CasterPipeline();
	VkPipeline pointCasterPipeline = VK_Exec_PointCasterPipeline();
	VkDescriptorSet whiteSet = VK_NULL_HANDLE;
	if ( globalImages->whiteImage != NULL ) {
		whiteSet = VK_Exec_ImageDescriptor( globalImages->whiteImage->GetDeviceHandle(), true );
	}
	if ( cmd == VK_NULL_HANDLE || whiteSet == VK_NULL_HANDLE ) {
		for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
			vkShadow.lights[ i ].valid = false;
		}
		return;
	}

	// a missing per-class caster pipeline fails only that class unshadowed
	int projectedCount = 0;
	int pointCount = 0;
	for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
		vkShadowLightState_t &light = vkShadow.lights[ i ];
		if ( !light.valid ) {
			continue;
		}
		if ( light.pointLight ) {
			if ( pointCasterPipeline == VK_NULL_HANDLE
					|| vkShadow.pointCubes[ light.cubeIndex ].image == VK_NULL_HANDLE ) {
				light.valid = false;
				continue;
			}
			pointCount++;
		} else {
			if ( casterPipeline == VK_NULL_HANDLE ) {
				light.valid = false;
				continue;
			}
			projectedCount++;
		}
	}
	if ( projectedCount + pointCount == 0 ) {
		return;
	}

	VK_Exec_EndMainRendering();

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

	if ( projectedCount > 0 ) {
		VK_ShadowMap_BarrierToAttachment( cmd, vkShadow.atlasImage, 1 );

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

		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, casterPipeline );
		vkCmdSetDepthTestEnable( cmd, VK_TRUE );
		vkCmdSetDepthWriteEnable( cmd, VK_TRUE );
		vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_LESS_OR_EQUAL );
		vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
		vkCmdSetDepthBias( cmd, 0.0f, 0.0f, 0.0f );
		vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );

		for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
			vkShadowLightState_t &light = vkShadow.lights[ i ];
			if ( !light.valid || light.pointLight ) {
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

		VK_ShadowMap_BarrierToSampled( cmd, vkShadow.atlasImage, 1 );
	}

	if ( pointCount > 0 ) {
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pointCasterPipeline );
		vkCmdSetDepthTestEnable( cmd, VK_TRUE );
		vkCmdSetDepthWriteEnable( cmd, VK_TRUE );
		vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_LESS_OR_EQUAL );
		vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
		vkCmdSetDepthBias( cmd, 0.0f, 0.0f, 0.0f );
		// POSITIVE-height viewport keeps the GL cube-face row mapping (NDC
		// y=-1 lands on texel row 0, exactly the GL FBO viewport transform),
		// which makes the GL winding convention CLOCKWISE in Vulkan
		// framebuffer terms — the material cull mapping
		// (VK_ShadowMap_CasterCullMode) is unchanged under CW front
		vkCmdSetFrontFace( cmd, VK_FRONT_FACE_CLOCKWISE );

		const int faceSize = vkShadow.pointCubeFaceSize;
		VkViewport viewport;
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (float)faceSize;
		viewport.height = (float)faceSize;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport( cmd, 0, 1, &viewport );

		VkRect2D scissor;
		scissor.offset.x = 0;
		scissor.offset.y = 0;
		scissor.extent.width = (uint32_t)faceSize;
		scissor.extent.height = (uint32_t)faceSize;
		vkCmdSetScissor( cmd, 0, 1, &scissor );

		for ( int i = 0 ; i < vkShadow.numLights ; i++ ) {
			vkShadowLightState_t &light = vkShadow.lights[ i ];
			if ( !light.valid || !light.pointLight ) {
				continue;
			}
			const viewLight_t *vLight = light.vLight;
			const vkPointShadowCube_t &cube = vkShadow.pointCubes[ light.cubeIndex ];

			VK_ShadowMap_BarrierToAttachment( cmd, cube.image, 6 );

			// near/far + the analytic face projection in the shared
			// VK_FixupClipSpaceZ convention: z_clip = zA*z_eye + zB*w_eye,
			// w_clip = -z_eye (RB_PointShadowMapBuildProjectionMatrix). The
			// caster pipeline has no depth clamp, so the near plane uses the
			// GL !DepthClampAvailable branch (clamp max 4.0).
			const float farClip = light.pointFar;
			const float nearClip = idMath::ClampFloat( 0.5f, 4.0f, farClip * 0.01f );
			const float projA = -( farClip + nearClip ) / ( farClip - nearClip );
			const float projB = -( 2.0f * farClip * nearClip ) / ( farClip - nearClip );
			float projRow[ 2 ];
			projRow[ 0 ] = 0.5f * ( projA - 1.0f );
			projRow[ 1 ] = 0.5f * projB;

			int drawnCasters = 0;
			for ( int cubeFace = 0 ; cubeFace < 6 ; cubeFace++ ) {
				float faceViewMatrix[ 16 ];
				VK_ShadowMap_PointFaceViewMatrix( vLight->globalLightOrigin, cubeFace, faceViewMatrix );

				VkRenderingAttachmentInfo depth;
				memset( &depth, 0, sizeof( depth ) );
				depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
				depth.imageView = cube.faceViews[ cubeFace ];
				depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				depth.clearValue.depthStencil.depth = 1.0f;
				depth.clearValue.depthStencil.stencil = 0;

				VkRenderingInfo ri;
				memset( &ri, 0, sizeof( ri ) );
				ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
				ri.renderArea.extent.width = (uint32_t)faceSize;
				ri.renderArea.extent.height = (uint32_t)faceSize;
				ri.layerCount = 1;
				ri.pDepthAttachment = &depth;
				ri.pStencilAttachment = &depth;
				vkCmdBeginRendering( cmd, &ri );

				drawnCasters += VK_ShadowMap_DrawPointCasterChain( ctx, faceViewMatrix, projRow, farClip, vLight->globalShadowMapCasters );
				drawnCasters += VK_ShadowMap_DrawPointCasterChain( ctx, faceViewMatrix, projRow, farClip, vLight->localShadowMapCasters );
				drawnCasters += VK_ShadowMap_DrawPointCasterChain( ctx, faceViewMatrix, projRow, farClip, vLight->globalShadowMapDynamicCasters );
				drawnCasters += VK_ShadowMap_DrawPointCasterChain( ctx, faceViewMatrix, projRow, farClip, vLight->localShadowMapDynamicCasters );

				vkCmdEndRendering( cmd );
			}

			if ( drawnCasters <= 0 ) {
				// all-skipped caster set = render miss (RB_RenderPointShadowMap)
				light.valid = false;
			}

			// unconditional: the image must end in the layout the cube's
			// descriptor sets declare
			VK_ShadowMap_BarrierToSampled( cmd, cube.image, 6 );

			// one-shot bring-up evidence the point cube pass emitted real work
			static bool loggedFirstPointShadow = false;
			if ( !loggedFirstPointShadow && light.valid ) {
				loggedFirstPointShadow = true;
				common->Printf( "Vulkan: first point shadow light: %d point shadow lights, %d cube faces, far %.1f, %d casters\n",
						pointCount, faceSize, farClip, drawnCasters );
			}
		}
	}

	// order the suspended main scope's color/depth attachment writes against
	// the resumed scope's loads (dynamic rendering scopes do not sync
	// themselves)
	{
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
		vkCmdPipelineBarrier2( cmd, &dep );
	}

	VK_Exec_BeginMainRendering( false );
	(void)viewDef;
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
