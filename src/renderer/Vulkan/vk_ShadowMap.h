// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __VK_SHADOWMAP_H__
#define __VK_SHADOWMAP_H__

/*
===============================================================================

	Vulkan shadow maps (Phase F2a projected + F2b point,
	docs/dev/plans/2026-07-19-vulkan-phase-f.md).

	Scratch-first: one module-owned depth atlas with a per-view row-scan tile
	allocator for projected/parallel lights, a small per-view pool of depth
	cubes for point lights, and a per-view caster pass rendered in a
	frame-scope interruption before the interaction loop. No static caches,
	no CSM, no translucent moments, no update budgets. Include after
	tr_local.h (idPlane/viewLight_t) and volk.h (VkDescriptorSet).

===============================================================================
*/

// per-view point-light cube pool: lights past the pool render unshadowed
static const int VK_SHADOW_MAX_POINT_CUBES = 8;

// per-view, per-light shadow state the interaction pass consumes
typedef struct vkShadowLightState_s {
	const viewLight_t *	vLight;
	bool				valid;
	bool				pointLight;		// F2b: depth-cube path instead of an atlas tile
	int					tileX;			// atlas tile origin, image (top-left) coordinates
	int					tileY;
	int					tileSize;		// atlas tile edge, or the cube face size for point lights
	int					cubeIndex;		// point cube pool slot (pointLight only)
	float				pointFar;		// padded radial far envelope (pointLight only)
	VkDescriptorSet		pointSet;		// cube set-7 descriptor set for the active frame slot (pointLight only)
	idPlane				clipPlanes[ 4 ];	// world-space light clip planes (S, T, depth, Q)
	float				clipMatrix[ 16 ];	// GL-style projection over the clip planes
	float				atlasRect[ 4 ];		// composed atlas UV rect (u0, v0, u1, v1); v span inverted
	float				invAtlasSize[ 2 ];
	float				texelDepthBias;
	float				normalOffsetWorld;	// projected: world units; point: per-distance texel factor
} vkShadowLightState_t;

// CPU phase: classify + gate the view's lights, build projected states /
// claim point cubes, and allocate atlas tiles; returns the number of
// shadow-map-ready lights
int		VK_ShadowMap_PrepareViewLights( const viewDef_t *viewDef );

// GPU phase: frame-scope interruption that renders every prepared light's
// casters into the atlas / its depth cube, then resumes main rendering with
// LOAD
void	VK_ShadowMap_RenderAtlas( const viewDef_t *viewDef );

// prepared state for a light, or NULL when the light renders unshadowed
const vkShadowLightState_t *VK_ShadowMap_LightState( const viewLight_t *vLight );

// Phase F3: per-light-class resource truth behind the tr_local.h hook
// RB_ShadowMapResourcesKnownGood — the front-end stencil-volume elision
// gate. True once this video generation has proven the class's resources.
bool	VK_ShadowMap_ResourcesKnownGood( bool pointLight );

// Phase F3 sticky fallback contract (GL RB_ShadowMapMarkStencilFallbackSticky
// parity): a shadow-map failure on a light whose stencil volumes were elided
// restores volume generation for that light from the next frame on
void	VK_ShadowMap_MarkStencilFallbackSticky( const viewLight_t *vLight );

// mark every still-valid prepared light sticky and drop it from the table:
// the caller could not run or consume the shadow pass this view
void	VK_ShadowMap_AbandonPreparedLights( void );

// device-shutdown hook (device idle by contract)
void	VK_ShadowMap_Shutdown( void );

#endif /* !__VK_SHADOWMAP_H__ */
