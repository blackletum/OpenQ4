// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ShadowMapClassification.h"

static shadowMapLightClass_t R_ShadowMapLightClassForViewLight( const viewLight_t *vLight ) {
	if ( vLight == NULL ) {
		return SHADOWMAP_LIGHT_PROJECTED;
	}
	if ( vLight->lightDef != NULL && vLight->lightDef->parms.globalLight ) {
		return SHADOWMAP_LIGHT_GLOBAL;
	}
	if ( vLight->parallel ) {
		return SHADOWMAP_LIGHT_PARALLEL;
	}
	if ( vLight->pointLight ) {
		return SHADOWMAP_LIGHT_POINT;
	}
	return SHADOWMAP_LIGHT_PROJECTED;
}

static bool R_ShadowMapProjectedLightIsViewScoped( const viewLight_t *vLight ) {
	if ( vLight == NULL || vLight->lightDef == NULL ) {
		return false;
	}
	const renderLight_t &parms = vLight->lightDef->parms;
	return parms.allowLightInViewID != 0 || parms.suppressLightInViewID != 0;
}

static bool R_ShadowMapProjectedLightUsesStockFlashlightShader( const viewLight_t *vLight ) {
	if ( vLight == NULL ) {
		return false;
	}

	const idMaterial *lightShader = vLight->lightShader;
	if ( lightShader == NULL && vLight->lightDef != NULL ) {
		lightShader = vLight->lightDef->lightShader;
	}
	if ( lightShader == NULL ) {
		return false;
	}

	const char *shaderName = lightShader->GetName();
	return shaderName != NULL && idStr::Icmp( shaderName, "gfx/lights/flashlight" ) == 0;
}

static bool R_ShadowMapProjectedLightNeedsAuthoredSingleProjection( const viewLight_t *vLight ) {
	return R_ShadowMapProjectedLightIsViewScoped( vLight )
		|| R_ShadowMapProjectedLightUsesStockFlashlightShader( vLight );
}

shadowMapLightClassification_t R_ClassifyShadowMapLight( const viewLight_t *vLight ) {
	shadowMapLightClassification_t classification;
	memset( &classification, 0, sizeof( classification ) );

	classification.lightClass = R_ShadowMapLightClassForViewLight( vLight );
	// Parallel (sun) lights carry pointLight=true with a faked far-away origin;
	// radial cube-map depth saturates for them, so they route through the
	// projected machinery with a synthesized orthographic projection instead
	// (R_ShadowMapBuildParallelClipPlanes).
	classification.pointLight = vLight != NULL && vLight->pointLight && !vLight->parallel;
	classification.projectedLight = !classification.pointLight;
	classification.ordinaryProjectedLight = classification.lightClass == SHADOWMAP_LIGHT_PROJECTED;
	classification.parallelLight = classification.lightClass == SHADOWMAP_LIGHT_PARALLEL;
	classification.globalLight = classification.lightClass == SHADOWMAP_LIGHT_GLOBAL;
	classification.projectedCSMGateApplies = classification.ordinaryProjectedLight;
	// Player weapon lights, including the stock flashlight projector, must keep
	// their authored projection instead of being camera-fitted into cascades.
	classification.projectedCSMEnabled = classification.projectedCSMGateApplies
		&& r_shadowMapProjectedCSM.GetBool()
		&& !R_ShadowMapProjectedLightNeedsAuthoredSingleProjection( vLight );
	classification.cascadeCount = 1;
	classification.atlasDiv = classification.pointLight ? 3 : 1;
	classification.tileCount = classification.pointLight ? 6 : 1;

	if ( vLight == NULL || classification.pointLight ) {
		return classification;
	}

	const int requestedCascadeCount = idMath::ClampInt( 1, SHADOWMAP_CLASSIFICATION_MAX_CASCADES, r_shadowMapCascadeCount.GetInteger() );
	classification.csmEnabled = r_shadowMapCSM.GetBool()
		&& requestedCascadeCount > 1
		&& ( !classification.projectedCSMGateApplies || classification.projectedCSMEnabled );

	if ( classification.csmEnabled ) {
		classification.cascadeCount = requestedCascadeCount;
		classification.atlasDiv = 2;
		classification.tileCount = requestedCascadeCount;
	}

	return classification;
}

const char *R_ShadowMapLightClassName( shadowMapLightClass_t lightClass ) {
	switch ( lightClass ) {
	case SHADOWMAP_LIGHT_POINT:
		return "point";
	case SHADOWMAP_LIGHT_PARALLEL:
		return "parallel";
	case SHADOWMAP_LIGHT_GLOBAL:
		return "global";
	default:
		return "projected";
	}
}
