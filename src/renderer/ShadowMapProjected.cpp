// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ShadowMapProjected.h"

void R_ShadowMapResetProjectedLightState( shadowMapProjectedLightState_t &state ) {
	memset( &state, 0, sizeof( state ) );
	state.projectionPad = 0.0f;
	state.projectionScale = 1.0f;
	state.fallbackReason = SHADOWMAP_PROJECTED_FALLBACK_NONE;
	state.requestedCascadeCount = 1;
	state.fallbackCascade = -1;
	state.cascadeCount = 1;
	state.atlasDiv = 1;
	state.atlasRect[0].Set( 0.0f, 0.0f, 1.0f, 1.0f );
	for ( int planeIndex = 0; planeIndex < 4; planeIndex++ ) {
		state.baseClipPlanes[planeIndex].Zero();
	}
	for ( int matrixIndex = 0; matrixIndex < 16; matrixIndex++ ) {
		state.baseShadowMatrix[matrixIndex] = ( matrixIndex == 0 || matrixIndex == 5 || matrixIndex == 10 || matrixIndex == 15 ) ? 1.0f : 0.0f;
	}
	for ( int i = 0; i < SHADOWMAP_PROJECTED_MAX_CASCADES; i++ ) {
		state.splitDepths[i] = 1.0e30f;
		state.biasScale[i] = 1.0f;
		state.texelDepthBias[i] = 0.0f;
		state.worldTexelSize[i] = 0.0f;
		state.sliceNear[i] = 0.0f;
		state.sliceFar[i] = 0.0f;
		state.depthRange[i] = 0.0f;
		state.clipZExtent[i] = 0.0f;
	}
}

const char *R_ShadowMapProjectedFallbackReasonName( const int reason ) {
	switch ( reason ) {
	case SHADOWMAP_PROJECTED_FALLBACK_INSUFFICIENT_VALID_SAMPLES:
		return "insufficient-valid-samples";
	case SHADOWMAP_PROJECTED_FALLBACK_MIXED_W_SIGNS:
		return "mixed-w-signs";
	case SHADOWMAP_PROJECTED_FALLBACK_COLLAPSED_BOUNDS:
		return "collapsed-bounds";
	default:
		return "none";
	}
}

float R_ShadowMapProjectionPad() {
	return Max( 0.0f, r_shadowMapProjectionPad.GetFloat() );
}

float R_ShadowMapProjectionScale( const float projectionPad ) {
	return 1.0f / ( 1.0f + Max( 0.0f, projectionPad ) * 2.0f );
}

void R_ShadowMapBuildClipPlanes( const idPlane lightProject[4], idPlane clipPlanes[4] ) {
	const float projectionPad = R_ShadowMapProjectionPad();
	const float projectionScale = R_ShadowMapProjectionScale( projectionPad );

	for ( int i = 0; i < 4; i++ ) {
		clipPlanes[0][i] = ( lightProject[0][i] * 2.0f - lightProject[2][i] ) * projectionScale;
		clipPlanes[1][i] = ( lightProject[1][i] * 2.0f - lightProject[2][i] ) * projectionScale;
		clipPlanes[2][i] = lightProject[3][i];
		clipPlanes[3][i] = lightProject[2][i];
	}
}

void R_ShadowMapClipPlanesToGLMatrix( const idPlane clipPlanes[4], float matrix[16] ) {
	matrix[0] = clipPlanes[0][0];
	matrix[4] = clipPlanes[0][1];
	matrix[8] = clipPlanes[0][2];
	matrix[12] = clipPlanes[0][3];

	matrix[1] = clipPlanes[1][0];
	matrix[5] = clipPlanes[1][1];
	matrix[9] = clipPlanes[1][2];
	matrix[13] = clipPlanes[1][3];

	matrix[2] = 0.0f;
	matrix[6] = 0.0f;
	matrix[10] = 0.0f;
	matrix[14] = 0.0f;

	matrix[3] = clipPlanes[3][0];
	matrix[7] = clipPlanes[3][1];
	matrix[11] = clipPlanes[3][2];
	matrix[15] = clipPlanes[3][3];
}

idVec4 R_ShadowMapBuildAtlasRect( const int cascadeIndex, const int atlasDiv ) {
	if ( atlasDiv <= 1 ) {
		return idVec4( 0.0f, 0.0f, 1.0f, 1.0f );
	}

	const float invAtlasDiv = 1.0f / atlasDiv;
	const int tileX = cascadeIndex % atlasDiv;
	const int tileY = cascadeIndex / atlasDiv;
	return idVec4(
		tileX * invAtlasDiv,
		tileY * invAtlasDiv,
		( tileX + 1 ) * invAtlasDiv,
		( tileY + 1 ) * invAtlasDiv );
}

static void R_ShadowMapInitializeProjectedState( shadowMapProjectedLightState_t &state, const idPlane baseClipPlanes[4], const int cascadeCount, const int tileSize ) {
	state.valid = true;
	state.projectionPad = R_ShadowMapProjectionPad();
	state.projectionScale = R_ShadowMapProjectionScale( state.projectionPad );
	state.cascadeCount = idMath::ClampInt( 1, SHADOWMAP_PROJECTED_MAX_CASCADES, cascadeCount );
	state.atlasDiv = state.cascadeCount > 1 ? 2 : 1;
	state.tileSize = tileSize;
	for ( int planeIndex = 0; planeIndex < 4; planeIndex++ ) {
		state.baseClipPlanes[planeIndex] = baseClipPlanes[planeIndex];
	}
	R_ShadowMapClipPlanesToGLMatrix( state.baseClipPlanes, state.baseShadowMatrix );

	for ( int cascadeIndex = 0; cascadeIndex < SHADOWMAP_PROJECTED_MAX_CASCADES; cascadeIndex++ ) {
		for ( int planeIndex = 0; planeIndex < 4; planeIndex++ ) {
			state.clipPlanes[cascadeIndex][planeIndex] = baseClipPlanes[planeIndex];
		}
		state.atlasRect[cascadeIndex] = R_ShadowMapBuildAtlasRect( cascadeIndex, state.atlasDiv );
		state.splitDepths[cascadeIndex] = 1.0e30f;
		state.biasScale[cascadeIndex] = 1.0f;
		state.texelDepthBias[cascadeIndex] = 0.0f;
		state.worldTexelSize[cascadeIndex] = 0.0f;
		state.sliceNear[cascadeIndex] = 0.0f;
		state.sliceFar[cascadeIndex] = 0.0f;
		state.depthRange[cascadeIndex] = 0.0f;
		state.clipZExtent[cascadeIndex] = 0.0f;
	}
}

static float R_ShadowMapViewNear( const viewDef_t *viewDef ) {
	float zNear = r_znear.GetFloat();
	if ( viewDef != NULL && viewDef->renderView.cramZNear ) {
		zNear *= 0.25f;
	}
	return Max( zNear, 0.25f );
}

static float R_ShadowMapCascadeDistanceForView( const viewDef_t *viewDef ) {
	const float zNear = R_ShadowMapViewNear( viewDef );
	return Max( r_shadowMapCascadeDistance.GetFloat(), zNear + 32.0f );
}

static void R_ShadowMapGetViewExtents( const viewDef_t *viewDef, float &zNear, float &xmin, float &xmax, float &ymin, float &ymax ) {
	const renderView_t &renderView = viewDef->renderView;

	zNear = R_ShadowMapViewNear( viewDef );
	ymax = zNear * tan( renderView.fov_y * idMath::PI / 360.0f );
	ymin = -ymax;

	xmax = zNear * tan( renderView.fov_x * idMath::PI / 360.0f );
	xmin = -xmax;

	if ( tr_levelshotProjectionShiftActive ) {
		const float width = xmax - xmin;
		const float height = ymax - ymin;
		const float xShift = 0.5f * width * tr_levelshotProjectionShiftX;
		const float yShift = 0.5f * height * tr_levelshotProjectionShiftY;
		xmin += xShift;
		xmax += xShift;
		ymin += yShift;
		ymax += yShift;
	}
}

static void R_ShadowMapBuildSliceCorners( const viewDef_t *viewDef, const float sliceNear, const float sliceFar, idVec3 corners[8] ) {
	const renderView_t &renderView = viewDef->renderView;
	const idVec3 &origin = renderView.vieworg;
	const idVec3 &forward = renderView.viewaxis[0];
	const idVec3 &left = renderView.viewaxis[1];
	const idVec3 &up = renderView.viewaxis[2];
	float zNear, xmin, xmax, ymin, ymax;

	R_ShadowMapGetViewExtents( viewDef, zNear, xmin, xmax, ymin, ymax );

	const float depths[2] = { sliceNear, sliceFar };
	for ( int depthIndex = 0; depthIndex < 2; depthIndex++ ) {
		const float depth = depths[depthIndex];
		const idVec3 center = origin + forward * depth;
		const float depthScale = depth / Max( zNear, idMath::FLOAT_EPSILON );
		const float leftExtent = xmax * depthScale;
		const float rightExtent = xmin * depthScale;
		const float topExtent = ymax * depthScale;
		const float bottomExtent = ymin * depthScale;
		const int baseIndex = depthIndex * 4;

		corners[baseIndex + 0] = center + left * leftExtent + up * topExtent;
		corners[baseIndex + 1] = center + left * rightExtent + up * topExtent;
		corners[baseIndex + 2] = center + left * rightExtent + up * bottomExtent;
		corners[baseIndex + 3] = center + left * leftExtent + up * bottomExtent;
	}
}

static int R_ShadowMapBuildSliceSamplePoints( const viewDef_t *viewDef, const float sliceNear, const float sliceFar, idVec3 samplePoints[SHADOWMAP_PROJECTED_CASCADE_SAMPLE_POINT_COUNT] ) {
	idVec3 corners[8];
	R_ShadowMapBuildSliceCorners( viewDef, sliceNear, sliceFar, corners );

	for ( int i = 0; i < 8; i++ ) {
		samplePoints[i] = corners[i];
	}

	const idVec3 nearCenter = ( corners[0] + corners[1] + corners[2] + corners[3] ) * 0.25f;
	const idVec3 farCenter = ( corners[4] + corners[5] + corners[6] + corners[7] ) * 0.25f;
	int sampleCount = 8;
	samplePoints[sampleCount++] = nearCenter;
	samplePoints[sampleCount++] = farCenter;
	samplePoints[sampleCount++] = ( nearCenter + farCenter ) * 0.5f;

	for ( int i = 0; i < 4; i++ ) {
		const int next = ( i + 1 ) & 3;
		samplePoints[sampleCount++] = ( corners[i] + corners[next] ) * 0.5f;
		samplePoints[sampleCount++] = ( corners[i + 4] + corners[next + 4] ) * 0.5f;
		samplePoints[sampleCount++] = ( corners[i] + corners[i + 4] ) * 0.5f;
	}

	return sampleCount;
}

static float R_ShadowMapWorldTexelSizeFromSamples( const idVec3 *samplePoints, const int sampleCount, const int tileSize ) {
	if ( samplePoints == NULL || sampleCount <= 0 || tileSize <= 0 ) {
		return 0.0f;
	}

	idBounds bounds;
	bounds.Clear();
	for ( int i = 0; i < sampleCount; i++ ) {
		bounds.AddPoint( samplePoints[i] );
	}

	const idVec3 size = bounds[1] - bounds[0];
	const float maxExtent = Max( Max( idMath::Fabs( size.x ), idMath::Fabs( size.y ) ), idMath::Fabs( size.z ) );
	return maxExtent / Max( 1, tileSize );
}

static float R_ShadowMapSliceWorldTexelSize( const viewDef_t *viewDef, const float sliceNear, const float sliceFar, const int tileSize ) {
	if ( viewDef == NULL ) {
		return 0.0f;
	}

	idVec3 samplePoints[SHADOWMAP_PROJECTED_CASCADE_SAMPLE_POINT_COUNT];
	const int sampleCount = R_ShadowMapBuildSliceSamplePoints( viewDef, sliceNear, sliceFar, samplePoints );
	return R_ShadowMapWorldTexelSizeFromSamples( samplePoints, sampleCount, tileSize );
}

// World width of the projected light's frustum at its far plane: the authored
// projection spans +-right / +-up around the target axis. Projected (spot)
// lights never populate lightRadius, so this is their only footprint source.
static float R_ShadowMapProjectedFarPlaneWidth( const viewLight_t *vLight ) {
	if ( vLight == NULL || vLight->lightDef == NULL || vLight->lightDef->parms.pointLight ) {
		return 0.0f;
	}
	const renderLight_t &parms = vLight->lightDef->parms;
	return 2.0f * Max( parms.right.Length(), parms.up.Length() );
}

// World distance over which the stored falloff depth spans 0..1. The depth
// row is the authored falloff plane (clipPlanes[2] = lightProject[3]), whose
// normal length is the reciprocal of that distance. This is the one true
// normalization for converting world-space bias into stored-depth units; it
// is identical for every cascade because the depth row is never cropped.
static float R_ShadowMapFalloffDepthExtent( const idPlane baseClipPlanes[4] ) {
	const idVec3 falloffNormal = baseClipPlanes[2].Normal();
	const float normalLength = falloffNormal.Length();
	if ( normalLength <= idMath::FLOAT_EPSILON ) {
		return 1.0f;
	}
	return Max( 1.0f, 1.0f / normalLength );
}

static float R_ShadowMapLightWorldTexelSize( const viewLight_t *vLight, const int tileSize ) {
	if ( vLight == NULL || tileSize <= 0 ) {
		return 0.0f;
	}

	idVec3 radius = vLight->lightRadius;
	if ( vLight->lightDef != NULL ) {
		const renderLight_t &parms = vLight->lightDef->parms;
		radius[0] = Max( idMath::Fabs( radius[0] ), idMath::Fabs( parms.lightRadius[0] ) + idMath::Fabs( parms.lightCenter[0] ) );
		radius[1] = Max( idMath::Fabs( radius[1] ), idMath::Fabs( parms.lightRadius[1] ) + idMath::Fabs( parms.lightCenter[1] ) );
		radius[2] = Max( idMath::Fabs( radius[2] ), idMath::Fabs( parms.lightRadius[2] ) + idMath::Fabs( parms.lightCenter[2] ) );
	}

	float maxExtent = Max( Max( idMath::Fabs( radius.x ), idMath::Fabs( radius.y ) ), idMath::Fabs( radius.z ) ) * 2.0f;
	maxExtent = Max( maxExtent, R_ShadowMapProjectedFarPlaneWidth( vLight ) );
	return maxExtent / Max( 1, tileSize );
}

// Cascade texel footprint from the fitted light-space crop: crop NDC width
// mapped back through the far-plane world width. Conservative (texels shrink
// toward the light apex) and tied to the actual shadow-map texel grid,
// unlike the camera-slice AABB which overestimates for rotated frusta.
static float R_ShadowMapCascadeCropWorldTexelSize( const viewLight_t *vLight, const idVec3 &ndcMins, const idVec3 &ndcMaxs, const int tileSize ) {
	const float farWidth = R_ShadowMapProjectedFarPlaneWidth( vLight );
	if ( farWidth <= 0.0f || tileSize <= 0 ) {
		return 0.0f;
	}
	const float cropNdcWidth = Max( ndcMaxs.x - ndcMins.x, ndcMaxs.y - ndcMins.y );
	return farWidth * cropNdcWidth * 0.5f / Max( 1, tileSize );
}

float R_ShadowMapTexelDepthBias( const float worldTexelSize, const float depthRange ) {
	if ( worldTexelSize <= 0.0f || depthRange <= 0.0f ) {
		return 0.0f;
	}
	return Max( 0.0f, r_shadowMapTexelBiasScale.GetFloat() ) * worldTexelSize / Max( depthRange, 1.0f );
}

static void R_ShadowMapTransformPointToClip( const idVec3 &point, const idPlane clipPlanes[4], idVec4 &clip ) {
	for ( int i = 0; i < 4; i++ ) {
		clip[i] = point[0] * clipPlanes[i][0] + point[1] * clipPlanes[i][1] + point[2] * clipPlanes[i][2] + clipPlanes[i][3];
	}
}

static float R_ShadowMapProjectedKernelGuardNDC( const int tileSize ) {
	const float texelStep = 2.0f / Max( 1, tileSize );
	const float kernelRadius = Max( 0.5f, r_shadowMapFilterRadius.GetFloat() + 0.75f );
	return texelStep * kernelRadius;
}

static bool R_ShadowMapBuildCascadeBounds( const idPlane baseClipPlanes[4], const viewDef_t *viewDef, const float sliceNear, const float sliceFar, const int tileSize, shadowMapProjectedCascadeFit_t &fit, idVec3 &ndcMins, idVec3 &ndcMaxs ) {
	idVec3 samplePoints[SHADOWMAP_PROJECTED_CASCADE_SAMPLE_POINT_COUNT];
	const int sampleCount = R_ShadowMapBuildSliceSamplePoints( viewDef, sliceNear, sliceFar, samplePoints );

	fit.sampleCount = sampleCount;
	fit.validPoints = 0;
	fit.skippedPoints = 0;
	fit.positiveWPoints = 0;
	fit.negativeWPoints = 0;
	fit.nearZeroWPoints = 0;
	fit.nanWPoints = 0;
	fit.invalidNdcPoints = 0;
	fit.mixedWSigns = false;
	fit.collapsedBounds = false;
	fit.fallbackReason = SHADOWMAP_PROJECTED_FALLBACK_NONE;
	ndcMins.Set( 1.0e30f, 1.0e30f, 1.0e30f );
	ndcMaxs.Set( -1.0e30f, -1.0e30f, -1.0e30f );
	const float wEpsilon = 1.0e-5f;

	for ( int i = 0; i < sampleCount; i++ ) {
		idVec4 clip;
		R_ShadowMapTransformPointToClip( samplePoints[i], baseClipPlanes, clip );
		if ( clip.w != clip.w ) {
			fit.nanWPoints++;
			fit.skippedPoints++;
			continue;
		}

		if ( clip.w > wEpsilon ) {
			fit.positiveWPoints++;
		} else if ( clip.w < -wEpsilon ) {
			fit.negativeWPoints++;
		} else {
			fit.nearZeroWPoints++;
		}

		if ( clip.w <= wEpsilon ) {
			fit.skippedPoints++;
			continue;
		}

		const float invW = 1.0f / clip.w;
		idVec3 ndc( clip.x * invW, clip.y * invW, clip.z );
		if ( ndc.x != ndc.x || ndc.y != ndc.y || ndc.z != ndc.z ) {
			fit.invalidNdcPoints++;
			fit.skippedPoints++;
			continue;
		}

		for ( int axis = 0; axis < 3; axis++ ) {
			ndcMins[axis] = Min( ndcMins[axis], ndc[axis] );
			ndcMaxs[axis] = Max( ndcMaxs[axis], ndc[axis] );
		}
		fit.validPoints++;
	}

	fit.mixedWSigns = ( fit.positiveWPoints > 0 && fit.negativeWPoints > 0 );

	if ( fit.validPoints < 4 ) {
		fit.fallbackReason = SHADOWMAP_PROJECTED_FALLBACK_INSUFFICIENT_VALID_SAMPLES;
		return false;
	}

	const float pad = Max( 0.0f, r_shadowMapProjectionPad.GetFloat() * 0.5f );
	const float filterGuard = R_ShadowMapProjectedKernelGuardNDC( tileSize );
	idVec3 center = ( ndcMins + ndcMaxs ) * 0.5f;
	idVec3 extent = ( ndcMaxs - ndcMins ) * 0.5f;

	extent.x = Max( extent.x * ( 1.0f + pad * 2.0f ) + filterGuard, 2.0f / Max( 1, tileSize ) );
	extent.y = Max( extent.y * ( 1.0f + pad * 2.0f ) + filterGuard, 2.0f / Max( 1, tileSize ) );
	extent.z = Max( extent.z * ( 1.0f + pad ), 0.001f );

	if ( r_shadowMapCascadeStabilize.GetBool() ) {
		const float texelStep = 2.0f / Max( 1, tileSize );
		center.x = floor( center.x / texelStep + 0.5f ) * texelStep;
		center.y = floor( center.y / texelStep + 0.5f ) * texelStep;
		extent.x = Max( texelStep, float( ceil( extent.x / texelStep ) ) * texelStep );
		extent.y = Max( texelStep, float( ceil( extent.y / texelStep ) ) * texelStep );
	}

	ndcMins = center - extent;
	ndcMaxs = center + extent;

	ndcMins.x = idMath::ClampFloat( -1.0f, 1.0f, ndcMins.x );
	ndcMins.y = idMath::ClampFloat( -1.0f, 1.0f, ndcMins.y );
	ndcMins.z = idMath::ClampFloat( 0.0f, 1.0f, ndcMins.z );
	ndcMaxs.x = idMath::ClampFloat( -1.0f, 1.0f, ndcMaxs.x );
	ndcMaxs.y = idMath::ClampFloat( -1.0f, 1.0f, ndcMaxs.y );
	ndcMaxs.z = idMath::ClampFloat( 0.0f, 1.0f, ndcMaxs.z );

	if ( ndcMaxs.x - ndcMins.x <= 1.0e-4f || ndcMaxs.y - ndcMins.y <= 1.0e-4f ) {
		fit.collapsedBounds = true;
		fit.fallbackReason = SHADOWMAP_PROJECTED_FALLBACK_COLLAPSED_BOUNDS;
		return false;
	}

	fit.valid = true;
	return true;
}

static void R_ShadowMapBuildCascadeClipPlanes( const idPlane baseClipPlanes[4], const idVec3 &ndcMins, const idVec3 &ndcMaxs, idPlane cascadeClipPlanes[4] ) {
	const float scaleX = 2.0f / ( ndcMaxs.x - ndcMins.x );
	const float scaleY = 2.0f / ( ndcMaxs.y - ndcMins.y );
	const float offsetX = -( ndcMaxs.x + ndcMins.x ) / ( ndcMaxs.x - ndcMins.x );
	const float offsetY = -( ndcMaxs.y + ndcMins.y ) / ( ndcMaxs.y - ndcMins.y );

	for ( int i = 0; i < 4; i++ ) {
		cascadeClipPlanes[0][i] = baseClipPlanes[0][i] * scaleX + baseClipPlanes[3][i] * offsetX;
		cascadeClipPlanes[1][i] = baseClipPlanes[1][i] * scaleY + baseClipPlanes[3][i] * offsetY;
		cascadeClipPlanes[2][i] = baseClipPlanes[2][i];
		cascadeClipPlanes[3][i] = baseClipPlanes[3][i];
	}
}

static float R_ShadowMapCascadeBiasScale( const idVec3 &ndcMins, const idVec3 &ndcMaxs ) {
	const float xyExtent = Max( ndcMaxs.x - ndcMins.x, ndcMaxs.y - ndcMins.y );
	const float zExtent = ndcMaxs.z - ndcMins.z;
	const float footprintScale = xyExtent * 0.5f;
	const float depthScale = Max( zExtent, 0.001f );
	const float combinedScale = Max( footprintScale, depthScale );
	return idMath::ClampFloat( 0.35f, 3.0f, combinedScale );
}

static void R_ShadowMapCollapseProjectedStateToSingleCascade( shadowMapProjectedLightState_t &state, const viewLight_t *vLight, const idPlane baseClipPlanes[4], const int tileSize, const float sliceNear, const float sliceFar, const int fallbackCascade, const int fallbackReason ) {
	shadowMapProjectedCascadeFit_t fallbackFit;
	memset( &fallbackFit, 0, sizeof( fallbackFit ) );
	if ( fallbackCascade >= 0 && fallbackCascade < SHADOWMAP_PROJECTED_MAX_CASCADES ) {
		fallbackFit = state.cascadeFit[fallbackCascade];
	}
	fallbackFit.fallbackReason = fallbackReason;
	const int requestedCascadeCount = state.requestedCascadeCount;
	R_ShadowMapResetProjectedLightState( state );
	state.requestedCascadeCount = requestedCascadeCount;
	state.cascadeFallback = true;
	state.fallbackCascade = fallbackCascade;
	state.fallbackReason = fallbackReason;
	if ( fallbackCascade >= 0 && fallbackCascade < SHADOWMAP_PROJECTED_MAX_CASCADES ) {
		state.cascadeFit[fallbackCascade] = fallbackFit;
	}
	R_ShadowMapInitializeProjectedState( state, baseClipPlanes, 1, tileSize );
	state.worldTexelSize[0] = R_ShadowMapLightWorldTexelSize( vLight, tileSize );
	state.sliceNear[0] = sliceNear;
	state.sliceFar[0] = sliceFar;
	state.depthRange[0] = R_ShadowMapFalloffDepthExtent( baseClipPlanes );
	state.clipZExtent[0] = 1.0f;
	state.texelDepthBias[0] = R_ShadowMapTexelDepthBias( state.worldTexelSize[0], state.depthRange[0] );
}

void R_BuildShadowMapProjectedLightState( const viewLight_t *vLight, const viewDef_t *viewDef, const int tileSize, shadowMapProjectedLightState_t &state ) {
	R_ShadowMapResetProjectedLightState( state );
	if ( vLight == NULL || tileSize <= 0 ) {
		return;
	}

	idPlane baseClipPlanes[4];
	R_ShadowMapBuildClipPlanes( vLight->lightProject, baseClipPlanes );

	const int requestedCascadeCount = R_ClassifyShadowMapLight( vLight ).cascadeCount;
	state.requestedCascadeCount = requestedCascadeCount;
	R_ShadowMapInitializeProjectedState( state, baseClipPlanes, requestedCascadeCount, tileSize );

	if ( requestedCascadeCount <= 1 || viewDef == NULL ) {
		const float worldTexelSize = R_ShadowMapLightWorldTexelSize( vLight, tileSize );
		const float depthRange = R_ShadowMapFalloffDepthExtent( baseClipPlanes );
		state.worldTexelSize[0] = worldTexelSize;
		state.sliceNear[0] = 0.0f;
		state.sliceFar[0] = depthRange;
		state.depthRange[0] = depthRange;
		state.clipZExtent[0] = 1.0f;
		state.texelDepthBias[0] = R_ShadowMapTexelDepthBias( worldTexelSize, depthRange );
		return;
	}

	const float zNear = R_ShadowMapViewNear( viewDef );
	const float maxDistance = R_ShadowMapCascadeDistanceForView( viewDef );
	const float lambda = idMath::ClampFloat( 0.0f, 1.0f, r_shadowMapCascadeLambda.GetFloat() );
	const float range = maxDistance - zNear;
	const float ratio = maxDistance / zNear;

	for ( int splitIndex = 0; splitIndex < requestedCascadeCount - 1; splitIndex++ ) {
		const float p = float( splitIndex + 1 ) / float( requestedCascadeCount );
		const float uniformSplit = zNear + range * p;
		const float logSplit = zNear * idMath::Pow( ratio, p );
		state.splitDepths[splitIndex] = uniformSplit + ( logSplit - uniformSplit ) * lambda;
	}

	float sliceNear = zNear;
	for ( int cascadeIndex = 0; cascadeIndex < requestedCascadeCount; cascadeIndex++ ) {
		const bool finalCascade = cascadeIndex == requestedCascadeCount - 1;
		const float targetFar = finalCascade ? maxDistance : state.splitDepths[cascadeIndex];
		const float sliceFar = Max( targetFar, sliceNear + 1.0f );
		idVec3 ndcMins;
		idVec3 ndcMaxs;
		shadowMapProjectedCascadeFit_t &fit = state.cascadeFit[cascadeIndex];
		fit.attempted = true;
		fit.sliceNear = sliceNear;
		fit.sliceFar = sliceFar;
		fit.valid = R_ShadowMapBuildCascadeBounds( baseClipPlanes, viewDef, sliceNear, sliceFar, tileSize, fit, ndcMins, ndcMaxs );
		if ( fit.valid ) {
			if ( fit.mixedWSigns ) {
				fit.fallbackReason = SHADOWMAP_PROJECTED_FALLBACK_MIXED_W_SIGNS;
				R_ShadowMapCollapseProjectedStateToSingleCascade( state, vLight, baseClipPlanes, tileSize, zNear, maxDistance, cascadeIndex, SHADOWMAP_PROJECTED_FALLBACK_MIXED_W_SIGNS );
				return;
			}
			R_ShadowMapBuildCascadeClipPlanes( baseClipPlanes, ndcMins, ndcMaxs, state.clipPlanes[cascadeIndex] );
			state.biasScale[cascadeIndex] = R_ShadowMapCascadeBiasScale( ndcMins, ndcMaxs );
			state.clipZExtent[cascadeIndex] = ndcMaxs.z - ndcMins.z;
			const float cropTexelSize = R_ShadowMapCascadeCropWorldTexelSize( vLight, ndcMins, ndcMaxs, tileSize );
			state.worldTexelSize[cascadeIndex] = cropTexelSize > 0.0f ? cropTexelSize : R_ShadowMapSliceWorldTexelSize( viewDef, sliceNear, sliceFar, tileSize );
		} else {
			R_ShadowMapCollapseProjectedStateToSingleCascade( state, vLight, baseClipPlanes, tileSize, zNear, maxDistance, cascadeIndex, fit.fallbackReason );
			return;
		}
		state.sliceNear[cascadeIndex] = sliceNear;
		state.sliceFar[cascadeIndex] = sliceFar;
		// stored depth is falloff-plane depth: bias normalization uses the
		// light's falloff extent, identically for every cascade, so bias
		// magnitude cannot jump across split boundaries
		state.depthRange[cascadeIndex] = R_ShadowMapFalloffDepthExtent( baseClipPlanes );
		state.texelDepthBias[cascadeIndex] = R_ShadowMapTexelDepthBias( state.worldTexelSize[cascadeIndex], state.depthRange[cascadeIndex] );
		sliceNear = sliceFar;
	}
}
