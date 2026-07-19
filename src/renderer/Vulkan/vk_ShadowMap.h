// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __VK_SHADOWMAP_H__
#define __VK_SHADOWMAP_H__

/*
===============================================================================

	Vulkan projected-light shadow maps (Phase F2a,
	docs/dev/plans/2026-07-19-vulkan-phase-f.md).

	Scratch-first: one module-owned depth atlas, a per-view row-scan tile
	allocator, and a per-view caster pass rendered in a frame-scope
	interruption before the interaction loop. No static caches, no CSM, no
	translucent moments, no update budgets; point lights fall back to the
	unshadowed Phase F1 path. Include after tr_local.h (idPlane/viewLight_t).

===============================================================================
*/

// per-view, per-light shadow state the interaction pass consumes
typedef struct vkShadowLightState_s {
	const viewLight_t *	vLight;
	bool				valid;
	int					tileX;			// atlas tile origin, image (top-left) coordinates
	int					tileY;
	int					tileSize;
	idPlane				clipPlanes[ 4 ];	// world-space light clip planes (S, T, depth, Q)
	float				clipMatrix[ 16 ];	// GL-style projection over the clip planes
	float				atlasRect[ 4 ];		// composed atlas UV rect (u0, v0, u1, v1); v span inverted
	float				invAtlasSize[ 2 ];
	float				texelDepthBias;
	float				normalOffsetWorld;
} vkShadowLightState_t;

// CPU phase: classify + gate the view's lights, build projected states, and
// allocate atlas tiles; returns the number of shadow-map-ready lights
int		VK_ShadowMap_PrepareViewLights( const viewDef_t *viewDef );

// GPU phase: frame-scope interruption that renders every prepared light's
// casters into the atlas, then resumes main rendering with LOAD
void	VK_ShadowMap_RenderAtlas( const viewDef_t *viewDef );

// prepared state for a light, or NULL when the light renders unshadowed
const vkShadowLightState_t *VK_ShadowMap_LightState( const viewLight_t *vLight );

// device-shutdown hook (device idle by contract)
void	VK_ShadowMap_Shutdown( void );

#endif /* !__VK_SHADOWMAP_H__ */
