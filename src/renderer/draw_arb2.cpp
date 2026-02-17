/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/




#include "tr_local.h"

#include "cg_explicit.h"
#include <ctype.h>

CGcontext cg_context;

typedef enum {
	ICM_PACKED,
	ICM_VECTOR
} interactionColorMode_t;

static interactionColorMode_t g_interactionVertexProgramAutoColorMode = ICM_PACKED;
static interactionColorMode_t g_interactionVertexProgramColorMode = ICM_PACKED;
static int g_interactionVertexProgramOverride = 0;

static idImage *g_shadowMapDepthImage = NULL;
static idRenderTexture *g_shadowMapRenderTexture = NULL;
static idImage *g_shadowMapPointDepthImage = NULL;
static idRenderTexture *g_shadowMapPointRenderTexture = NULL;
static int g_shadowMapImageSize = 0;
static bool g_shadowMapWarnedNoTextureUnit = false;
static bool g_shadowMapWarnedPointLightFallback = false;
static bool g_shadowMapWarnedPointRadiusFallback = false;
static bool g_shadowMapWarnedNoEligibleLights = false;
static bool g_shadowMapReportedActive = false;

// Shadow-map ARB fragment programs use env[2]/env[3] for depth/debug params.
static const int FP_SHADOW_PARAMS = 2;
static const int FP_SHADOW_DEBUG = 3;

typedef enum {
	SMCR_OK = 0,
	SMCR_NULL_GEO,
	SMCR_NULL_AMBIENT_CACHE,
	SMCR_NULL_MATERIAL,
	SMCR_NO_SHADOW_FLAG,
	SMCR_TRANSLUCENT,
	SMCR_PERFORATED_DISABLED
} shadowMapCasterReject_t;

typedef struct {
	int inputSurfs;
	int drawnSurfs;
	int rejectNullGeo;
	int rejectNullAmbientCache;
	int rejectNullMaterial;
	int rejectNoShadowFlag;
	int rejectTranslucent;
	int rejectPerforated;
} shadowMapCasterStats_t;

static void RB_ARB2_SetShadowDebugVisibilityParam( void ) {
	const float forcedVisibility = r_shadowMapDebugForceVisibility.GetFloat();
	float shadowDebug[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
	if ( forcedVisibility >= 0.0f ) {
		shadowDebug[0] = -1.0f; // negative selects forced value in shader CMP
		shadowDebug[1] = idMath::ClampFloat( 0.0f, 1.0f, forcedVisibility );
	}
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, FP_SHADOW_DEBUG, shadowDebug );
}

static const char *RB_InteractionColorModeName( interactionColorMode_t mode ) {
	switch ( mode ) {
	case ICM_PACKED:
		return "packed env16.xy";
	case ICM_VECTOR:
		return "vector env16/env17";
	default:
		return "unknown";
	}
}

static const char *RB_InteractionColorOverrideName( int modeOverride ) {
	switch ( modeOverride ) {
	case 0:
		return "auto";
	case 1:
		return "packed";
	case 2:
		return "vector";
	default:
		return "invalid";
	}
}

static void RB_StripARBProgramCommentsAndWhitespace( const char *source, idStr &normalizedSource ) {
	normalizedSource.Empty();
	if ( source == NULL ) {
		return;
	}

	bool inComment = false;
	for ( const char *p = source; *p != '\0'; ++p ) {
		const char c = *p;
		if ( c == '\r' || c == '\n' ) {
			inComment = false;
			continue;
		}

		if ( inComment ) {
			continue;
		}

		if ( c == '#' ) {
			inComment = true;
			continue;
		}

		if ( c == ' ' || c == '\t' ) {
			continue;
		}

		normalizedSource.Append( (char)tolower( (unsigned char)c ) );
	}
}

static bool RB_DetectInteractionColorMode( const char *programSource, interactionColorMode_t &modeOut ) {
	idStr normalizedProgram;
	RB_StripARBProgramCommentsAndWhitespace( programSource, normalizedProgram );
	const char *normalized = normalizedProgram.c_str();

	if ( strstr( normalized, "madresult.color,vertex.color,program.env[16].x,program.env[16].y;" ) != NULL ) {
		modeOut = ICM_PACKED;
		return true;
	}

	if ( strstr( normalized, "madresult.color,vertex.color,program.env[16],program.env[17];" ) != NULL ) {
		modeOut = ICM_VECTOR;
		return true;
	}

	const bool packedHints = strstr( normalized, "program.env[16].x" ) != NULL &&
		strstr( normalized, "program.env[16].y" ) != NULL;
	const bool usesEnv17 = strstr( normalized, "program.env[17]" ) != NULL;

	if ( packedHints && !usesEnv17 ) {
		modeOut = ICM_PACKED;
		return true;
	}

	if ( usesEnv17 ) {
		modeOut = ICM_VECTOR;
		return true;
	}

	return false;
}

static void RB_UpdateInteractionColorMode( bool forcePrint ) {
	const int modeOverride = idMath::ClampInt( 0, 2, r_interactionColorMode.GetInteger() );
	interactionColorMode_t requestedMode = g_interactionVertexProgramAutoColorMode;
	interactionColorMode_t effectiveMode = g_interactionVertexProgramAutoColorMode;

	if ( modeOverride == 1 ) {
		requestedMode = ICM_PACKED;
	} else if ( modeOverride == 2 ) {
		requestedMode = ICM_VECTOR;
	}

	if ( modeOverride != 0 && requestedMode != g_interactionVertexProgramAutoColorMode ) {
		effectiveMode = g_interactionVertexProgramAutoColorMode;
		if ( forcePrint ||
			modeOverride != g_interactionVertexProgramOverride ||
			effectiveMode != g_interactionVertexProgramColorMode ) {
			common->Warning( "r_interactionColorMode=%d (%s) is incompatible with interaction.vfp (%s); forcing compatible mode",
				modeOverride,
				RB_InteractionColorModeName( requestedMode ),
				RB_InteractionColorModeName( g_interactionVertexProgramAutoColorMode ) );
		}
	} else {
		effectiveMode = requestedMode;
	}

	const bool changed = effectiveMode != g_interactionVertexProgramColorMode ||
		modeOverride != g_interactionVertexProgramOverride;

	g_interactionVertexProgramColorMode = effectiveMode;
	g_interactionVertexProgramOverride = modeOverride;

	if ( forcePrint || changed ) {
		common->Printf( ": interaction color mode = %s (auto=%s, override=%s)\n",
			RB_InteractionColorModeName( g_interactionVertexProgramColorMode ),
			RB_InteractionColorModeName( g_interactionVertexProgramAutoColorMode ),
			RB_InteractionColorOverrideName( g_interactionVertexProgramOverride ) );
	}
}

static void cg_error_callback( void ) {
	CGerror i = cgGetError();
	common->Printf( "Cg error (%d): %s\n", i, cgGetErrorString(i) );
}

/*
=========================================================================================

GENERAL INTERACTION RENDERING

=========================================================================================
*/

/*
====================
GL_SelectTextureNoClient
====================
*/
void GL_SelectTextureNoClient( int unit ) {
	backEnd.glState.currenttmu = unit;
	glActiveTextureARB( GL_TEXTURE0_ARB + unit );
	RB_LogComment( "glActiveTextureARB( %i )\n", unit );
}

/*
==================
RB_ARB2_ShadowMapCanUseLight
==================
*/
static bool RB_ARB2_ShadowMapCanUseLight( const viewLight_t *vLight ) {
	if ( !r_useShadowMapping.GetBool() || !r_shadows.GetBool() ) {
		return false;
	}

	// Keep ambient lights on the stock stencil path.
	// Quake 4 uses wide ambient fill lights that are not suitable for this basic
	// single-cubemap depth implementation and can over-darken entire scenes.
	if ( vLight->lightShader != NULL && vLight->lightShader->IsAmbientLight() ) {
		return false;
	}

	if ( glConfig.maxTextureUnits <= 7 && glConfig.maxTextureImageUnits <= 7 ) {
		if ( !g_shadowMapWarnedNoTextureUnit ) {
			common->Warning( "r_useShadowMapping=1 requires at least 8 texture units, falling back to stencil shadows" );
			g_shadowMapWarnedNoTextureUnit = true;
		}
		return false;
	}

	if ( vLight->pointLight && !r_shadowMapPointLight.GetBool() ) {
		if ( !g_shadowMapWarnedPointLightFallback ) {
			common->Warning( "point lights currently use stencil shadows when r_shadowMapPointLight=0" );
			g_shadowMapWarnedPointLightFallback = true;
		}
		return false;
	}

	if ( vLight->pointLight ) {
		const float pointRadiusLimit = r_shadowMapPointMaxRadius.GetFloat();
		const float maxRadius = Max( vLight->lightRadius.x, Max( vLight->lightRadius.y, vLight->lightRadius.z ) );
		if ( pointRadiusLimit > 0.0f && maxRadius > pointRadiusLimit ) {
			if ( !g_shadowMapWarnedPointRadiusFallback ) {
				common->Warning( "point lights larger than r_shadowMapPointMaxRadius=%.1f use stencil shadows to avoid unstable depth-cubemap coverage", pointRadiusLimit );
				g_shadowMapWarnedPointRadiusFallback = true;
			}
			return false;
		}
	}

	return true;
}

/*
==================
RB_ARB2_EnsureShadowMapRenderTarget
==================
*/
static bool RB_ARB2_EnsureShadowMapRenderTarget( void ) {
	const int imageSize = idMath::ClampInt( 128, 4096, r_shadowMapImageSize.GetInteger() );
	if ( imageSize <= 0 ) {
		return false;
	}

	if ( g_shadowMapDepthImage == NULL || g_shadowMapImageSize != imageSize ) {
		idImageOpts shadowImageOpts;
		shadowImageOpts.textureType = TT_2D;
		shadowImageOpts.format = FMT_DEPTH;
		shadowImageOpts.width = imageSize;
		shadowImageOpts.height = imageSize;
		shadowImageOpts.numLevels = 1;

		g_shadowMapDepthImage = tr.CreateImage( "_shadowMapDepth", &shadowImageOpts, TF_LINEAR );
		if ( g_shadowMapDepthImage == NULL ) {
			return false;
		}

		if ( g_shadowMapRenderTexture == NULL ) {
			g_shadowMapRenderTexture = tr.CreateRenderTexture( NULL, g_shadowMapDepthImage );
		} else {
			g_shadowMapRenderTexture->Resize( imageSize, imageSize );
		}

		g_shadowMapImageSize = imageSize;
	}

	return ( g_shadowMapDepthImage != NULL && g_shadowMapRenderTexture != NULL );
}

/*
==================
RB_ARB2_EnsureShadowMapPointRenderTarget
==================
*/
static bool RB_ARB2_EnsureShadowMapPointRenderTarget( void ) {
	const int imageSize = idMath::ClampInt( 128, 4096, r_shadowMapImageSize.GetInteger() );
	if ( imageSize <= 0 ) {
		return false;
	}

	if ( g_shadowMapPointDepthImage == NULL || g_shadowMapImageSize != imageSize ) {
		idImageOpts shadowImageOpts;
		shadowImageOpts.textureType = TT_CUBIC;
		shadowImageOpts.format = FMT_DEPTH;
		shadowImageOpts.width = imageSize;
		shadowImageOpts.height = imageSize;
		shadowImageOpts.numLevels = 1;

		g_shadowMapPointDepthImage = tr.CreateImage( "_shadowMapPointDepth", &shadowImageOpts, TF_LINEAR );
		if ( g_shadowMapPointDepthImage == NULL ) {
			return false;
		}

		if ( g_shadowMapPointRenderTexture == NULL ) {
			g_shadowMapPointRenderTexture = tr.CreateRenderTexture( NULL, g_shadowMapPointDepthImage );
		} else {
			g_shadowMapPointRenderTexture->Resize( imageSize, imageSize );
		}

		g_shadowMapImageSize = imageSize;
	}

	return ( g_shadowMapPointDepthImage != NULL && g_shadowMapPointRenderTexture != NULL );
}

/*
==================
RB_ARB2_BindShadowMapSampler
==================
*/
static void RB_ARB2_BindShadowMapSampler( void ) {
	if ( g_shadowMapDepthImage == NULL ) {
		return;
	}

	const GLenum filter = r_shadowMapSoftShadows.GetBool() ? GL_LINEAR : GL_NEAREST;
	const float borderColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	GL_SelectTextureNoClient( 7 );
	g_shadowMapDepthImage->Bind();
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );
	glTexParameterfv( GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
}

/*
==================
RB_ARB2_BindShadowCubeSampler
==================
*/
static void RB_ARB2_BindShadowCubeSampler( void ) {
	if ( g_shadowMapPointDepthImage == NULL ) {
		return;
	}

	const GLenum filter = r_shadowMapSoftShadows.GetBool() ? GL_LINEAR : GL_NEAREST;

	GL_SelectTextureNoClient( 7 );
	g_shadowMapPointDepthImage->Bind();
	glTexParameteri( GL_TEXTURE_CUBE_MAP_EXT, GL_TEXTURE_MIN_FILTER, filter );
	glTexParameteri( GL_TEXTURE_CUBE_MAP_EXT, GL_TEXTURE_MAG_FILTER, filter );
	glTexParameteri( GL_TEXTURE_CUBE_MAP_EXT, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_CUBE_MAP_EXT, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_CUBE_MAP_EXT, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_CUBE_MAP_EXT, GL_TEXTURE_COMPARE_MODE, GL_NONE );
}

/*
==================
RB_ARB2_CountShadowMapSurfChain
==================
*/
static int RB_ARB2_CountShadowMapSurfChain( const drawSurf_t *surf ) {
	int count = 0;
	for ( ; surf != NULL; surf = surf->nextOnLight ) {
		count++;
	}
	return count;
}

/*
==================
RB_ARB2_ClassifyShadowMapCaster
==================
*/
static shadowMapCasterReject_t RB_ARB2_ClassifyShadowMapCaster( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->geo == NULL ) {
		return SMCR_NULL_GEO;
	}

	if ( surf->geo->ambientCache == NULL ) {
		return SMCR_NULL_AMBIENT_CACHE;
	}

	if ( surf->material == NULL ) {
		return SMCR_NULL_MATERIAL;
	}

	const idMaterial *material = surf->material;
	if ( !material->SurfaceCastsShadow() ) {
		return SMCR_NO_SHADOW_FLAG;
	}

	if ( material->Coverage() == MC_TRANSLUCENT ) {
		return SMCR_TRANSLUCENT;
	}

	if ( material->Coverage() == MC_PERFORATED && !r_shadowMapPerforatedCasters.GetBool() ) {
		return SMCR_PERFORATED_DISABLED;
	}

	return SMCR_OK;
}

/*
==================
RB_ARB2_DrawShadowMapSurfChain
==================
*/
static int RB_ARB2_DrawShadowMapSurfChain( const drawSurf_t *surf, shadowMapCasterStats_t *stats ) {
	int numDrawn = 0;

	for ( ; surf; surf = surf->nextOnLight ) {
		if ( stats != NULL ) {
			stats->inputSurfs++;
		}

		const shadowMapCasterReject_t rejectReason = RB_ARB2_ClassifyShadowMapCaster( surf );
		if ( rejectReason != SMCR_OK ) {
			if ( stats != NULL ) {
				switch ( rejectReason ) {
				case SMCR_NULL_GEO:
					stats->rejectNullGeo++;
					break;
				case SMCR_NULL_AMBIENT_CACHE:
					stats->rejectNullAmbientCache++;
					break;
				case SMCR_NULL_MATERIAL:
					stats->rejectNullMaterial++;
					break;
				case SMCR_NO_SHADOW_FLAG:
					stats->rejectNoShadowFlag++;
					break;
				case SMCR_TRANSLUCENT:
					stats->rejectTranslucent++;
					break;
				case SMCR_PERFORATED_DISABLED:
					stats->rejectPerforated++;
					break;
				default:
					break;
				}
			}
			continue;
		}

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( surf->geo->ambientCache );

		idPlane localShadowProject[4];
		R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.vLight->lightProject[0], localShadowProject[0] );
		R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.vLight->lightProject[1], localShadowProject[1] );
		R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.vLight->lightProject[2], localShadowProject[2] );
		R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.vLight->lightProject[3], localShadowProject[3] );

		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_PROJECT_S, localShadowProject[0].ToFloatPtr() );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_PROJECT_T, localShadowProject[1].ToFloatPtr() );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_PROJECT_Q, localShadowProject[2].ToFloatPtr() );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_FALLOFF_S, localShadowProject[3].ToFloatPtr() );

		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
		RB_DrawElementsWithCounters( surf->geo );
		numDrawn++;
		if ( stats != NULL ) {
			stats->drawnSurfs++;
		}
	}

	return numDrawn;
}

/*
==================
RB_ARB2_BuildPointShadowProjectionMatrix
==================
*/
static void RB_ARB2_BuildPointShadowProjectionMatrix( float zNear, float zFar, float out[16] ) {
	const float safeNear = Max( 0.1f, zNear );
	const float safeFar = Max( safeNear + 1.0f, zFar );
	const float invTan = 1.0f; // 90 degree FOV
	const float range = safeFar - safeNear;

	memset( out, 0, sizeof( float ) * 16 );
	out[0] = invTan;
	out[5] = invTan;
	out[10] = -( safeFar + safeNear ) / range;
	out[14] = -( 2.0f * safeFar * safeNear ) / range;
	out[11] = -1.0f;
}

/*
==================
RB_ARB2_BuildPointShadowViewMatrix
==================
*/
static void RB_ARB2_BuildPointShadowViewMatrix( const idVec3 &origin, const idVec3 &forward, const idVec3 &upHint, float out[16] ) {
	idVec3 f = forward;
	f.Normalize();

	idVec3 up = upHint;
	up.Normalize();

	idVec3 s;
	s.Cross( f, up );
	if ( s.LengthSqr() < 1e-10f ) {
		up.Set( 0.0f, 0.0f, 1.0f );
		s.Cross( f, up );
		if ( s.LengthSqr() < 1e-10f ) {
			up.Set( 0.0f, 1.0f, 0.0f );
			s.Cross( f, up );
		}
	}
	s.Normalize();

	idVec3 u;
	u.Cross( s, f );

	out[0] = s[0];
	out[4] = s[1];
	out[8] = s[2];
	out[12] = -( origin * s );

	out[1] = u[0];
	out[5] = u[1];
	out[9] = u[2];
	out[13] = -( origin * u );

	out[2] = -f[0];
	out[6] = -f[1];
	out[10] = -f[2];
	out[14] = origin * f;

	out[3] = 0.0f;
	out[7] = 0.0f;
	out[11] = 0.0f;
	out[15] = 1.0f;
}

/*
==================
RB_ARB2_DrawShadowMapSurfChainPoint
==================
*/
static int RB_ARB2_DrawShadowMapSurfChainPoint( const drawSurf_t *surf, const float faceViewMatrix[16], shadowMapCasterStats_t *stats ) {
	int numDrawn = 0;
	const viewEntity_t *currentSpace = NULL;

	for ( ; surf; surf = surf->nextOnLight ) {
		if ( stats != NULL ) {
			stats->inputSurfs++;
		}

		const shadowMapCasterReject_t rejectReason = RB_ARB2_ClassifyShadowMapCaster( surf );
		if ( rejectReason != SMCR_OK ) {
			if ( stats != NULL ) {
				switch ( rejectReason ) {
				case SMCR_NULL_GEO:
					stats->rejectNullGeo++;
					break;
				case SMCR_NULL_AMBIENT_CACHE:
					stats->rejectNullAmbientCache++;
					break;
				case SMCR_NULL_MATERIAL:
					stats->rejectNullMaterial++;
					break;
				case SMCR_NO_SHADOW_FLAG:
					stats->rejectNoShadowFlag++;
					break;
				case SMCR_TRANSLUCENT:
					stats->rejectTranslucent++;
					break;
				case SMCR_PERFORATED_DISABLED:
					stats->rejectPerforated++;
					break;
				default:
					break;
				}
			}
			continue;
		}

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( surf->geo->ambientCache );

		if ( surf->space != currentSpace ) {
			float shadowModelViewMatrix[16];
			myGlMultMatrix( surf->space->modelMatrix, faceViewMatrix, shadowModelViewMatrix );
			glLoadMatrixf( shadowModelViewMatrix );
			currentSpace = surf->space;
		}

		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
		RB_DrawElementsWithCounters( surf->geo );
		numDrawn++;
		if ( stats != NULL ) {
			stats->drawnSurfs++;
		}
	}

	return numDrawn;
}

/*
==================
RB_ARB2_RenderShadowMap
==================
*/
static bool RB_ARB2_RenderShadowMap( const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters ) {
	if ( primaryCasters == NULL && secondaryCasters == NULL ) {
		return false;
	}

	if ( !RB_ARB2_EnsureShadowMapRenderTarget() ) {
		return false;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1;
	GLint previousFramebuffer = 0;
	GLint previousDrawBuffer = GL_BACK;
	GLint previousReadBuffer = GL_BACK;
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &previousFramebuffer );
	glGetIntegerv( GL_DRAW_BUFFER, &previousDrawBuffer );
	glGetIntegerv( GL_READ_BUFFER, &previousReadBuffer );

	g_shadowMapRenderTexture->MakeCurrent();
	glDrawBuffer( GL_NONE );
	glReadBuffer( GL_NONE );
	glViewport( 0, 0, g_shadowMapImageSize, g_shadowMapImageSize );
	glScissor( 0, 0, g_shadowMapImageSize, g_shadowMapImageSize );

	glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	glDisable( GL_BLEND );
	glDisable( GL_STENCIL_TEST );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	glClear( GL_DEPTH_BUFFER_BIT );

	switch ( r_shadowMapOccluderFacing.GetInteger() ) {
	case 0:
		glEnable( GL_CULL_FACE );
		glCullFace( GL_BACK );
		break;
	case 1:
		glEnable( GL_CULL_FACE );
		glCullFace( GL_FRONT );
		break;
	default:
		glDisable( GL_CULL_FACE );
		break;
	}

	const float polygonFactor = r_shadowMapPolygonFactor.GetFloat();
	const float polygonOffset = r_shadowMapPolygonOffset.GetFloat();
	if ( polygonFactor != 0.0f || polygonOffset != 0.0f ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( polygonFactor, polygonOffset );
	}

	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );

	glEnable( GL_VERTEX_PROGRAM_ARB );
	glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_SHADOWMAP_DEPTH );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );

	shadowMapCasterStats_t casterStats;
	memset( &casterStats, 0, sizeof( casterStats ) );
	const int primaryCount = RB_ARB2_CountShadowMapSurfChain( primaryCasters );
	const int secondaryCount = RB_ARB2_CountShadowMapSurfChain( secondaryCasters );
	const int numDrawn = RB_ARB2_DrawShadowMapSurfChain( primaryCasters, &casterStats ) + RB_ARB2_DrawShadowMapSurfChain( secondaryCasters, &casterStats );

	glDisable( GL_VERTEX_PROGRAM_ARB );
	if ( polygonFactor != 0.0f || polygonOffset != 0.0f ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
	glEnable( GL_CULL_FACE );

	glBindFramebuffer( GL_FRAMEBUFFER, previousFramebuffer );
	glDrawBuffer( previousDrawBuffer );
	glReadBuffer( previousReadBuffer );
	glViewport( backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1, viewportWidth, viewportHeight );

	if ( r_useScissor.GetBool() ) {
		backEnd.currentScissor = backEnd.viewDef->scissor;
		glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
			backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}

	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glEnable( GL_BLEND );
	glEnable( GL_STENCIL_TEST );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );

	backEnd.currentSpace = NULL;
	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );
	GL_Cull( CT_FRONT_SIDED );
	GL_ClearStateDelta();

	if ( r_shadowMapDebugStats.GetInteger() >= 2 ) {
		common->Printf( "shadowMap2D: shader='%s' chains=(%d,%d) checked=%d drawn=%d rejects{nullGeo=%d nullAmbient=%d nullMaterial=%d noShadow=%d translucent=%d perforated=%d} imageSize=%d\n",
			backEnd.vLight && backEnd.vLight->lightShader ? backEnd.vLight->lightShader->GetName() : "<null>",
			primaryCount, secondaryCount,
			casterStats.inputSurfs, numDrawn,
			casterStats.rejectNullGeo, casterStats.rejectNullAmbientCache,
			casterStats.rejectNullMaterial, casterStats.rejectNoShadowFlag,
			casterStats.rejectTranslucent, casterStats.rejectPerforated,
			g_shadowMapImageSize );
	}

	return ( numDrawn > 0 );
}

/*
==================
RB_ARB2_RenderShadowMapPoint
==================
*/
static bool RB_ARB2_RenderShadowMapPoint( const drawSurf_t *primaryCasters, const drawSurf_t *secondaryCasters ) {
	if ( primaryCasters == NULL && secondaryCasters == NULL ) {
		return false;
	}

	if ( !RB_ARB2_EnsureShadowMapPointRenderTarget() ) {
		return false;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1;
	GLint previousFramebuffer = 0;
	GLint previousDrawBuffer = GL_BACK;
	GLint previousReadBuffer = GL_BACK;
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &previousFramebuffer );
	glGetIntegerv( GL_DRAW_BUFFER, &previousDrawBuffer );
	glGetIntegerv( GL_READ_BUFFER, &previousReadBuffer );

	const float shadowFar = Max( 16.0f, Max( backEnd.vLight->lightRadius.x, Max( backEnd.vLight->lightRadius.y, backEnd.vLight->lightRadius.z ) ) );
	const float shadowNear = Max( 1.0f, shadowFar * 0.005f );

	float projectionMatrix[16];
	RB_ARB2_BuildPointShadowProjectionMatrix( shadowNear, shadowFar, projectionMatrix );

	static const idVec3 faceForward[6] = {
		idVec3( 1.0f, 0.0f, 0.0f ),
		idVec3( -1.0f, 0.0f, 0.0f ),
		idVec3( 0.0f, 1.0f, 0.0f ),
		idVec3( 0.0f, -1.0f, 0.0f ),
		idVec3( 0.0f, 0.0f, 1.0f ),
		idVec3( 0.0f, 0.0f, -1.0f )
	};
	static const idVec3 faceUp[6] = {
		idVec3( 0.0f, -1.0f, 0.0f ),
		idVec3( 0.0f, -1.0f, 0.0f ),
		idVec3( 0.0f, 0.0f, 1.0f ),
		idVec3( 0.0f, 0.0f, -1.0f ),
		idVec3( 0.0f, -1.0f, 0.0f ),
		idVec3( 0.0f, -1.0f, 0.0f )
	};

	glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	glDisable( GL_BLEND );
	glDisable( GL_STENCIL_TEST );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );

	switch ( r_shadowMapOccluderFacing.GetInteger() ) {
	case 0:
		glEnable( GL_CULL_FACE );
		glCullFace( GL_BACK );
		break;
	case 1:
		glEnable( GL_CULL_FACE );
		glCullFace( GL_FRONT );
		break;
	default:
		glDisable( GL_CULL_FACE );
		break;
	}

	const float polygonFactor = r_shadowMapPolygonFactor.GetFloat();
	const float polygonOffset = r_shadowMapPolygonOffset.GetFloat();
	if ( polygonFactor != 0.0f || polygonOffset != 0.0f ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( polygonFactor, polygonOffset );
	}

	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( projectionMatrix );
	glMatrixMode( GL_MODELVIEW );

	shadowMapCasterStats_t casterStats;
	memset( &casterStats, 0, sizeof( casterStats ) );
	const int primaryCount = RB_ARB2_CountShadowMapSurfChain( primaryCasters );
	const int secondaryCount = RB_ARB2_CountShadowMapSurfChain( secondaryCasters );
	int numDrawn = 0;
	for ( int face = 0; face < 6; ++face ) {
		float faceViewMatrix[16];
		RB_ARB2_BuildPointShadowViewMatrix( backEnd.vLight->globalLightOrigin, faceForward[face], faceUp[face], faceViewMatrix );

		g_shadowMapPointRenderTexture->MakeCurrent( face );
		glDrawBuffer( GL_NONE );
		glReadBuffer( GL_NONE );
		glViewport( 0, 0, g_shadowMapImageSize, g_shadowMapImageSize );
		glScissor( 0, 0, g_shadowMapImageSize, g_shadowMapImageSize );
		glClear( GL_DEPTH_BUFFER_BIT );

		numDrawn += RB_ARB2_DrawShadowMapSurfChainPoint( primaryCasters, faceViewMatrix, &casterStats );
		numDrawn += RB_ARB2_DrawShadowMapSurfChainPoint( secondaryCasters, faceViewMatrix, &casterStats );
	}

	glBindFramebuffer( GL_FRAMEBUFFER, previousFramebuffer );
	glDrawBuffer( previousDrawBuffer );
	glReadBuffer( previousReadBuffer );

	glViewport( backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1, viewportWidth, viewportHeight );
	if ( r_useScissor.GetBool() ) {
		backEnd.currentScissor = backEnd.viewDef->scissor;
		glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
			backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );

	if ( polygonFactor != 0.0f || polygonOffset != 0.0f ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
	glEnable( GL_CULL_FACE );

	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glEnable( GL_BLEND );
	glEnable( GL_STENCIL_TEST );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );

	backEnd.currentSpace = NULL;
	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );
	GL_Cull( CT_FRONT_SIDED );
	GL_ClearStateDelta();

	if ( r_shadowMapDebugStats.GetInteger() >= 2 ) {
		common->Printf( "shadowMapPoint: shader='%s' chains=(%d,%d) checked=%d drawn=%d rejects{nullGeo=%d nullAmbient=%d nullMaterial=%d noShadow=%d translucent=%d perforated=%d} imageSize=%d near=%.2f far=%.2f\n",
			backEnd.vLight && backEnd.vLight->lightShader ? backEnd.vLight->lightShader->GetName() : "<null>",
			primaryCount, secondaryCount,
			casterStats.inputSurfs, numDrawn,
			casterStats.rejectNullGeo, casterStats.rejectNullAmbientCache,
			casterStats.rejectNullMaterial, casterStats.rejectNoShadowFlag,
			casterStats.rejectTranslucent, casterStats.rejectPerforated,
			g_shadowMapImageSize, shadowNear, shadowFar );
	}

	return ( numDrawn > 0 );
}

/*
==================
RB_ARB2_DrawInteraction
==================
*/
void	RB_ARB2_DrawInteraction( const drawInteraction_t *din ) {
	// load all the vertex program parameters
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_ORIGIN, din->localLightOrigin.ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_VIEW_ORIGIN, din->localViewOrigin.ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_PROJECT_S, din->lightProjection[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_PROJECT_T, din->lightProjection[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_PROJECT_Q, din->lightProjection[2].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_FALLOFF_S, din->lightProjection[3].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_BUMP_MATRIX_S, din->bumpMatrix[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_BUMP_MATRIX_T, din->bumpMatrix[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_DIFFUSE_MATRIX_S, din->diffuseMatrix[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_DIFFUSE_MATRIX_T, din->diffuseMatrix[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_SPECULAR_MATRIX_S, din->specularMatrix[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_SPECULAR_MATRIX_T, din->specularMatrix[1].ToFloatPtr() );

	// testing fragment based normal mapping
	if ( r_testARBProgram.GetBool() ) {
		glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 2, din->localLightOrigin.ToFloatPtr() );
		glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 3, din->localViewOrigin.ToFloatPtr() );
	}

	static const float zero[4] = { 0, 0, 0, 0 };
	float modulate = 0.0f;
	float add = 1.0f;

	switch ( din->vertexColor ) {
	case SVC_IGNORE:
		modulate = 0.0f;
		add = 1.0f;
		break;
	case SVC_MODULATE:
		modulate = 1.0f;
		add = 0.0f;
		break;
	case SVC_INVERSE_MODULATE:
		modulate = -1.0f;
		add = 1.0f;
		break;
	}

	if ( g_interactionVertexProgramColorMode == ICM_PACKED ) {
		// Stock Quake 4 interaction.vfp packs vertex-color mode as env[16].xy.
		const float packed[4] = { modulate, add, 0.0f, 0.0f };
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_MODULATE, packed );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_ADD, zero );
	} else {
		float modulateVec[4] = { modulate, modulate, modulate, modulate };
		float addVec[4] = { add, add, add, add };
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_MODULATE, modulateVec );
		glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_ADD, addVec );
	}

	// set the constant colors
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, din->diffuseColor.ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 1, din->specularColor.ToFloatPtr() );

	// set the textures

	// texture 1 will be the per-surface bump map
	GL_SelectTextureNoClient( 1 );
	din->bumpImage->Bind();

	// texture 2 will be the light falloff texture
	GL_SelectTextureNoClient( 2 );
	din->lightFalloffImage->Bind();

	// texture 3 will be the light projection texture
	GL_SelectTextureNoClient( 3 );
	din->lightImage->Bind();

	// texture 4 is the per-surface diffuse map
	GL_SelectTextureNoClient( 4 );
	din->diffuseImage->Bind();

	// texture 5 is the per-surface specular map
	GL_SelectTextureNoClient( 5 );
	din->specularImage->Bind();

	// Quake 4 applies decal polygon offset in interaction passes as well.
	const idMaterial *surfaceMaterial = din->surf->material;
	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * surfaceMaterial->GetPolygonOffset() );
	}

	// draw it
	RB_DrawElementsWithCounters( din->surf->geo );

	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}

/*
==================
RB_ARB2_DrawInteraction_ShadowMap
==================
*/
static void RB_ARB2_DrawInteraction_ShadowMap( const drawInteraction_t *din ) {
	// load all the vertex program parameters
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_ORIGIN, din->localLightOrigin.ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_VIEW_ORIGIN, din->localViewOrigin.ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_PROJECT_S, din->lightProjection[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_PROJECT_T, din->lightProjection[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_PROJECT_Q, din->lightProjection[2].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_FALLOFF_S, din->lightProjection[3].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_BUMP_MATRIX_S, din->bumpMatrix[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_BUMP_MATRIX_T, din->bumpMatrix[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_DIFFUSE_MATRIX_S, din->diffuseMatrix[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_DIFFUSE_MATRIX_T, din->diffuseMatrix[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_SPECULAR_MATRIX_S, din->specularMatrix[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_SPECULAR_MATRIX_T, din->specularMatrix[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_SHADOW_PROJECT_S, din->shadowProjection[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_SHADOW_PROJECT_T, din->shadowProjection[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_SHADOW_PROJECT_R, din->shadowProjection[2].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_SHADOW_PROJECT_Q, din->shadowProjection[3].ToFloatPtr() );

	float modulate = 0.0f;
	float add = 1.0f;

	switch ( din->vertexColor ) {
	case SVC_IGNORE:
		modulate = 0.0f;
		add = 1.0f;
		break;
	case SVC_MODULATE:
		modulate = 1.0f;
		add = 0.0f;
		break;
	case SVC_INVERSE_MODULATE:
		modulate = -1.0f;
		add = 1.0f;
		break;
	}

	const float packedColorMode[4] = { modulate, add, 0.0f, 0.0f };
	static const float zeroColorMode[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_MODULATE, packedColorMode );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_ADD, zeroColorMode );

	// set the constant colors
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, din->diffuseColor.ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 1, din->specularColor.ToFloatPtr() );
	const float shadowDepthBias[4] = { r_shadowMapDepthBiasScale.GetFloat(), 0.0f, 0.0f, 0.0f };
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, FP_SHADOW_PARAMS, shadowDepthBias );
	// Keep this alias for legacy variants that may still read env[22].
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, PP_SHADOW_DEPTH_BIAS, shadowDepthBias );
	RB_ARB2_SetShadowDebugVisibilityParam();

	// set the textures
	GL_SelectTextureNoClient( 1 );
	din->bumpImage->Bind();

	GL_SelectTextureNoClient( 2 );
	din->lightFalloffImage->Bind();

	GL_SelectTextureNoClient( 3 );
	din->lightImage->Bind();

	GL_SelectTextureNoClient( 4 );
	din->diffuseImage->Bind();

	GL_SelectTextureNoClient( 5 );
	din->specularImage->Bind();

	const idMaterial *surfaceMaterial = din->surf->material;
	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * surfaceMaterial->GetPolygonOffset() );
	}

	RB_DrawElementsWithCounters( din->surf->geo );

	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}

/*
==================
RB_ARB2_DrawInteraction_ShadowMapPoint
==================
*/
static void RB_ARB2_DrawInteraction_ShadowMapPoint( const drawInteraction_t *din ) {
	// load all the vertex program parameters
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_ORIGIN, din->localLightOrigin.ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_VIEW_ORIGIN, din->localViewOrigin.ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_PROJECT_S, din->lightProjection[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_PROJECT_T, din->lightProjection[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_PROJECT_Q, din->lightProjection[2].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_FALLOFF_S, din->lightProjection[3].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_BUMP_MATRIX_S, din->bumpMatrix[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_BUMP_MATRIX_T, din->bumpMatrix[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_DIFFUSE_MATRIX_S, din->diffuseMatrix[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_DIFFUSE_MATRIX_T, din->diffuseMatrix[1].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_SPECULAR_MATRIX_S, din->specularMatrix[0].ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_SPECULAR_MATRIX_T, din->specularMatrix[1].ToFloatPtr() );

	const float *modelMatrix = din->surf->space->modelMatrix;
	const idVec3 &lightOrigin = backEnd.vLight->globalLightOrigin;
	const float shadowCubeX[4] = {
		modelMatrix[0], modelMatrix[4], modelMatrix[8], modelMatrix[12] - lightOrigin.x
	};
	const float shadowCubeY[4] = {
		modelMatrix[1], modelMatrix[5], modelMatrix[9], modelMatrix[13] - lightOrigin.y
	};
	const float shadowCubeZ[4] = {
		modelMatrix[2], modelMatrix[6], modelMatrix[10], modelMatrix[14] - lightOrigin.z
	};
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_SHADOW_CUBE_X, shadowCubeX );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_SHADOW_CUBE_Y, shadowCubeY );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_SHADOW_CUBE_Z, shadowCubeZ );

	float modulate = 0.0f;
	float add = 1.0f;

	switch ( din->vertexColor ) {
	case SVC_IGNORE:
		modulate = 0.0f;
		add = 1.0f;
		break;
	case SVC_MODULATE:
		modulate = 1.0f;
		add = 0.0f;
		break;
	case SVC_INVERSE_MODULATE:
		modulate = -1.0f;
		add = 1.0f;
		break;
	}

	const float packedColorMode[4] = { modulate, add, 0.0f, 0.0f };
	static const float zeroColorMode[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_MODULATE, packedColorMode );
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_COLOR_ADD, zeroColorMode );

	// set the constant colors
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, din->diffuseColor.ToFloatPtr() );
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 1, din->specularColor.ToFloatPtr() );
	const float shadowFar = Max( 16.0f, Max( backEnd.vLight->lightRadius.x, Max( backEnd.vLight->lightRadius.y, backEnd.vLight->lightRadius.z ) ) );
	const float shadowNear = Max( 1.0f, shadowFar * 0.005f );
	const float shadowDepthParams[4] = { r_shadowMapDepthBiasScale.GetFloat(), shadowNear, shadowFar, 0.0f };
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, FP_SHADOW_PARAMS, shadowDepthParams );
	// Keep this alias for legacy variants that may still read env[22].
	glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, PP_SHADOW_DEPTH_BIAS, shadowDepthParams );
	RB_ARB2_SetShadowDebugVisibilityParam();

	// set the textures
	GL_SelectTextureNoClient( 1 );
	din->bumpImage->Bind();

	GL_SelectTextureNoClient( 2 );
	din->lightFalloffImage->Bind();

	GL_SelectTextureNoClient( 3 );
	din->lightImage->Bind();

	GL_SelectTextureNoClient( 4 );
	din->diffuseImage->Bind();

	GL_SelectTextureNoClient( 5 );
	din->specularImage->Bind();

	const idMaterial *surfaceMaterial = din->surf->material;
	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * surfaceMaterial->GetPolygonOffset() );
	}

	RB_DrawElementsWithCounters( din->surf->geo );

	if ( surfaceMaterial && surfaceMaterial->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}


/*
=============
RB_ARB2_CreateDrawInteractions

=============
*/
void RB_ARB2_CreateDrawInteractions( const drawSurf_t *surf ) {
	if ( !surf ) {
		return;
	}

	// perform setup here that will be constant for all interactions
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	// bind the vertex program
	if ( r_testARBProgram.GetBool() ) {
		glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_TEST );
		glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_TEST );
	} else {
		glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_INTERACTION );
		glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_INTERACTION );
	}

	glEnable(GL_VERTEX_PROGRAM_ARB);
	glEnable(GL_FRAGMENT_PROGRAM_ARB);

	// enable the vertex arrays
	glEnableVertexAttribArrayARB( 8 );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );
	glEnableVertexAttribArrayARB( 11 );
	glEnableClientState( GL_COLOR_ARRAY );

	// texture 0 is the normalization cube map for the vector towards the light
	GL_SelectTextureNoClient( 0 );
	if ( backEnd.vLight->lightShader->IsAmbientLight() ) {
		globalImages->ambientNormalMap->Bind();
	} else {
		globalImages->normalCubeMapImage->Bind();
	}

	// texture 6 is the specular lookup table
	GL_SelectTextureNoClient( 6 );
	globalImages->specularTableImage->Bind();


	for ( ; surf ; surf=surf->nextOnLight ) {
		// perform setup here that will not change over multiple interaction passes

		// set the vertex pointers
		idDrawVert	*ac = (idDrawVert *)vertexCache.Position( surf->geo->ambientCache );
		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), ac->color );
		glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
		glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[1].ToFloatPtr() );
		glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[0].ToFloatPtr() );
		glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), ac->st.ToFloatPtr() );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );

		// this may cause RB_ARB2_DrawInteraction to be exacuted multiple
		// times with different colors and images if the surface or light have multiple layers
		RB_CreateSingleDrawInteractions( surf, RB_ARB2_DrawInteraction );
	}

	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glDisableClientState( GL_COLOR_ARRAY );

	// disable features
	GL_SelectTextureNoClient( 6 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 5 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 4 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 3 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 2 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 1 );
	globalImages->BindNull();

	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );

	glDisable(GL_VERTEX_PROGRAM_ARB);
	glDisable(GL_FRAGMENT_PROGRAM_ARB);
}

/*
=============
RB_ARB2_CreateDrawInteractionsShadow
=============
*/
static void RB_ARB2_CreateDrawInteractionsShadow( const drawSurf_t *surf ) {
	if ( !surf || g_shadowMapDepthImage == NULL ) {
		return;
	}

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_INTERACTION_SHADOW );
	glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_INTERACTION_SHADOW );

	glEnable( GL_VERTEX_PROGRAM_ARB );
	glEnable( GL_FRAGMENT_PROGRAM_ARB );

	glEnableVertexAttribArrayARB( 8 );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );
	glEnableVertexAttribArrayARB( 11 );
	glEnableClientState( GL_COLOR_ARRAY );

	GL_SelectTextureNoClient( 0 );
	if ( backEnd.vLight->lightShader->IsAmbientLight() ) {
		globalImages->ambientNormalMap->Bind();
	} else {
		globalImages->normalCubeMapImage->Bind();
	}

	GL_SelectTextureNoClient( 6 );
	globalImages->specularTableImage->Bind();

	RB_ARB2_BindShadowMapSampler();

	for ( ; surf ; surf = surf->nextOnLight ) {
		if ( surf->geo == NULL || surf->geo->ambientCache == NULL ) {
			continue;
		}

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( surf->geo->ambientCache );

		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), ac->color );
		glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
		glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[1].ToFloatPtr() );
		glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[0].ToFloatPtr() );
		glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), ac->st.ToFloatPtr() );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );

		RB_CreateSingleDrawInteractions( surf, RB_ARB2_DrawInteraction_ShadowMap );
	}

	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glDisableClientState( GL_COLOR_ARRAY );

	GL_SelectTextureNoClient( 7 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 6 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 5 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 4 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 3 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 2 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 1 );
	globalImages->BindNull();

	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );

	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
}

/*
=============
RB_ARB2_CreateDrawInteractionsShadowPoint
=============
*/
static void RB_ARB2_CreateDrawInteractionsShadowPoint( const drawSurf_t *surf ) {
	if ( !surf || g_shadowMapPointDepthImage == NULL ) {
		return;
	}

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_INTERACTION_SHADOW_POINT );
	glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, FPROG_INTERACTION_SHADOW_POINT );

	glEnable( GL_VERTEX_PROGRAM_ARB );
	glEnable( GL_FRAGMENT_PROGRAM_ARB );

	glEnableVertexAttribArrayARB( 8 );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );
	glEnableVertexAttribArrayARB( 11 );
	glEnableClientState( GL_COLOR_ARRAY );

	GL_SelectTextureNoClient( 0 );
	if ( backEnd.vLight->lightShader->IsAmbientLight() ) {
		globalImages->ambientNormalMap->Bind();
	} else {
		globalImages->normalCubeMapImage->Bind();
	}

	GL_SelectTextureNoClient( 6 );
	globalImages->specularTableImage->Bind();

	RB_ARB2_BindShadowCubeSampler();

	for ( ; surf ; surf = surf->nextOnLight ) {
		if ( surf->geo == NULL || surf->geo->ambientCache == NULL ) {
			continue;
		}

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( surf->geo->ambientCache );

		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), ac->color );
		glVertexAttribPointerARB( 11, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
		glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[1].ToFloatPtr() );
		glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[0].ToFloatPtr() );
		glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), ac->st.ToFloatPtr() );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );

		RB_CreateSingleDrawInteractions( surf, RB_ARB2_DrawInteraction_ShadowMapPoint );
	}

	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableVertexAttribArrayARB( 11 );
	glDisableClientState( GL_COLOR_ARRAY );

	GL_SelectTextureNoClient( 7 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 6 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 5 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 4 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 3 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 2 );
	globalImages->BindNull();

	GL_SelectTextureNoClient( 1 );
	globalImages->BindNull();

	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );

	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
}


/*
==================
RB_ARB2_DrawInteractions
==================
*/
void RB_ARB2_DrawInteractions( void ) {
	viewLight_t		*vLight;
	bool anyShadowMappedLight = false;
	bool anyPointLight = false;
	int debugTotalEligibleLights = 0;
	int debugPointEligibleLights = 0;
	int debugShadowMappedLights = 0;
	int debugShadowMapPasses = 0;
	int debugShadowMapPassesWithCasters = 0;

	if ( r_interactionColorMode.IsModified() ) {
		r_interactionColorMode.ClearModified();
		RB_UpdateInteractionColorMode( true );
	}

	GL_SelectTexture( 0 );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );

	//
	// for each light, perform adding and shadowing
	//
	for ( vLight = backEnd.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		backEnd.vLight = vLight;
		anyPointLight |= vLight->pointLight;

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

		if ( RB_ARB2_ShadowMapCanUseLight( vLight ) ) {
			debugTotalEligibleLights++;
			if ( vLight->pointLight ) {
				debugPointEligibleLights++;
			}
			anyShadowMappedLight = true;
			if ( !g_shadowMapReportedActive ) {
				common->Printf( "shadow mapping enabled: using ARB2 depth-map light interactions\n" );
				g_shadowMapReportedActive = true;
			}

			glStencilFunc( GL_ALWAYS, 128, 255 );
			glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
			GL_Cull( CT_FRONT_SIDED );

			bool drewLocalWithShadowMap = false;
			bool usedShadowMapForLight = false;
			if ( vLight->localInteractions && vLight->globalInteractions ) {
				const bool renderedShadowMap = vLight->pointLight
					? RB_ARB2_RenderShadowMapPoint( vLight->globalInteractions, NULL )
					: RB_ARB2_RenderShadowMap( vLight->globalInteractions, NULL );
				debugShadowMapPasses++;
				if ( renderedShadowMap ) {
					debugShadowMapPassesWithCasters++;
				}
				if ( renderedShadowMap ) {
					if ( vLight->pointLight ) {
						RB_ARB2_CreateDrawInteractionsShadowPoint( vLight->localInteractions );
					} else {
						RB_ARB2_CreateDrawInteractionsShadow( vLight->localInteractions );
					}
					drewLocalWithShadowMap = true;
					usedShadowMapForLight = true;
				}
			}
			if ( vLight->localInteractions && !drewLocalWithShadowMap ) {
				RB_ARB2_CreateDrawInteractions( vLight->localInteractions );
			}

			bool drewGlobalWithShadowMap = false;
			if ( vLight->globalInteractions ) {
				const bool renderedShadowMap = vLight->pointLight
					? RB_ARB2_RenderShadowMapPoint( vLight->globalInteractions, vLight->localInteractions )
					: RB_ARB2_RenderShadowMap( vLight->globalInteractions, vLight->localInteractions );
				debugShadowMapPasses++;
				if ( renderedShadowMap ) {
					debugShadowMapPassesWithCasters++;
				}
				if ( renderedShadowMap ) {
					if ( vLight->pointLight ) {
						RB_ARB2_CreateDrawInteractionsShadowPoint( vLight->globalInteractions );
					} else {
						RB_ARB2_CreateDrawInteractionsShadow( vLight->globalInteractions );
					}
					drewGlobalWithShadowMap = true;
					usedShadowMapForLight = true;
				}
			}
			if ( vLight->globalInteractions && !drewGlobalWithShadowMap ) {
				RB_ARB2_CreateDrawInteractions( vLight->globalInteractions );
			}

			if ( usedShadowMapForLight ) {
				debugShadowMappedLights++;
			}
		} else {
			// clear the stencil buffer if needed
			if ( vLight->globalShadows || vLight->localShadows ) {
				backEnd.currentScissor = vLight->scissorRect;
				if ( r_useScissor.GetBool() ) {
					glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
						backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
						backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
						backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
				}
				glClear( GL_STENCIL_BUFFER_BIT );
			} else {
				// no shadows, so no need to read or write the stencil buffer
				// we might in theory want to use GL_ALWAYS instead of disabling
				// completely, to satisfy the invarience rules
				glStencilFunc( GL_ALWAYS, 128, 255 );
			}

			if ( r_useShadowVertexProgram.GetBool() ) {
				glEnable( GL_VERTEX_PROGRAM_ARB );
				glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW );
				RB_StencilShadowPass( vLight->globalShadows );
				RB_ARB2_CreateDrawInteractions( vLight->localInteractions );
				glEnable( GL_VERTEX_PROGRAM_ARB );
				glBindProgramARB( GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW );
				RB_StencilShadowPass( vLight->localShadows );
				RB_ARB2_CreateDrawInteractions( vLight->globalInteractions );
				glDisable( GL_VERTEX_PROGRAM_ARB );	// if there weren't any globalInteractions, it would have stayed on
			} else {
				RB_StencilShadowPass( vLight->globalShadows );
				RB_ARB2_CreateDrawInteractions( vLight->localInteractions );
				RB_StencilShadowPass( vLight->localShadows );
				RB_ARB2_CreateDrawInteractions( vLight->globalInteractions );
			}
		}

		// translucent surfaces never get stencil shadowed
		if ( r_skipTranslucent.GetBool() ) {
			continue;
		}

		glStencilFunc( GL_ALWAYS, 128, 255 );

		backEnd.depthFunc = GLS_DEPTHFUNC_LESS;
		RB_ARB2_CreateDrawInteractions( vLight->translucentInteractions );

		backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;
	}

	if ( r_useShadowMapping.GetBool() && anyPointLight && !anyShadowMappedLight && !g_shadowMapWarnedNoEligibleLights ) {
		common->Warning( "shadow mapping is enabled, but this view had no eligible lights for the current shadow-map path; visible shadows remain stencil-based" );
		g_shadowMapWarnedNoEligibleLights = true;
	}

	if ( r_shadowMapDebugStats.GetInteger() > 0 ) {
		const bool printNow = ( r_shadowMapDebugStats.GetInteger() >= 2 ) || ( ( tr.frameCount % 60 ) == 0 );
		if ( printNow ) {
			common->Printf( "shadowMapStats: eligibleLights=%d pointEligible=%d shadowMappedLights=%d mapPasses=%d mapPassesWithCasters=%d forceVisibility=%.3f\n",
				debugTotalEligibleLights, debugPointEligibleLights, debugShadowMappedLights,
				debugShadowMapPasses, debugShadowMapPassesWithCasters,
				r_shadowMapDebugForceVisibility.GetFloat() );
		}
	}

	// disable stencil shadow test
	glStencilFunc( GL_ALWAYS, 128, 255 );

	GL_SelectTexture( 0 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
}

//===================================================================================


typedef struct {
	GLenum			target;
	GLuint			ident;
	char			name[64];
} progDef_t;

static	const int	MAX_GLPROGS = 200;

// a single file can have both a vertex program and a fragment program
static progDef_t	progs[MAX_GLPROGS] = {
	{ GL_VERTEX_PROGRAM_ARB, VPROG_TEST, "test.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_TEST, "test.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_INTERACTION, "interaction.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_INTERACTION, "interaction.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_BUMPY_ENVIRONMENT, "bumpyEnvironment.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_BUMPY_ENVIRONMENT, "bumpyEnvironment.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_AMBIENT, "ambientLight.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_AMBIENT, "ambientLight.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_STENCIL_SHADOW, "shadow.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_R200_INTERACTION, "R200_interaction.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_NV20_BUMP_AND_LIGHT, "nv20_bumpAndLight.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_NV20_DIFFUSE_COLOR, "nv20_diffuseColor.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_NV20_SPECULAR_COLOR, "nv20_specularColor.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_NV20_DIFFUSE_AND_SPECULAR_COLOR, "nv20_diffuseAndSpecularColor.vp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_ENVIRONMENT, "environment.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_ENVIRONMENT, "environment.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_GLASSWARP, "arbVP_glasswarp.txt" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_GLASSWARP, "arbFP_glasswarp.txt" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_INTERACTION_SHADOW, "interactionShadow.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_INTERACTION_SHADOW, "interactionShadow.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_INTERACTION_SHADOW_POINT, "interactionShadowPoint.vfp" },
	{ GL_FRAGMENT_PROGRAM_ARB, FPROG_INTERACTION_SHADOW_POINT, "interactionShadowPoint.vfp" },
	{ GL_VERTEX_PROGRAM_ARB, VPROG_SHADOWMAP_DEPTH, "shadowMapDepth.vp" },

	// additional programs can be dynamically specified in materials
};

/*
=================
R_LoadARBProgram
=================
*/
void R_LoadARBProgram( int progIndex ) {
	int		ofs;
	int		err;
	idStr	fullPath = "glprogs/";
	fullPath += progs[progIndex].name;
	char	*fileBuffer;
	char	*buffer;
	char	*start, *end;

	common->Printf( "%s", fullPath.c_str() );

	// load the program even if we don't support it, so
	// fs_copyfiles can generate cross-platform data dumps
	fileSystem->ReadFile( fullPath.c_str(), (void **)&fileBuffer, NULL );
	if ( !fileBuffer ) {
		common->Printf( ": File not found\n" );
		return;
	}

	// copy to stack memory and free
	buffer = (char *)_alloca( strlen( fileBuffer ) + 1 );
	strcpy( buffer, fileBuffer );
	fileSystem->FreeFile( fileBuffer );

	if ( !glConfig.isInitialized ) {
		return;
	}

	//
	// submit the program string at start to GL
	//
	if ( progs[progIndex].ident == 0 ) {
		// allocate a new identifier for this program
		progs[progIndex].ident = PROG_USER + progIndex;
	}

	// vertex and fragment programs can both be present in a single file, so
	// scan for the proper header to be the start point, and stamp a 0 in after the end

	if ( progs[progIndex].target == GL_VERTEX_PROGRAM_ARB ) {
		if ( !glConfig.ARBVertexProgramAvailable ) {
			common->Printf( ": GL_VERTEX_PROGRAM_ARB not available\n" );
			return;
		}
		start = strstr( (char *)buffer, "!!ARBvp" );
	}
	if ( progs[progIndex].target == GL_FRAGMENT_PROGRAM_ARB ) {
		if ( !glConfig.ARBFragmentProgramAvailable ) {
			common->Printf( ": GL_FRAGMENT_PROGRAM_ARB not available\n" );
			return;
		}
		start = strstr( (char *)buffer, "!!ARBfp" );
	}
	if ( !start ) {
		common->Printf( ": !!ARB not found\n" );
		return;
	}
	end = strstr( start, "END" );

	if ( !end ) {
		common->Printf( ": END not found\n" );
		return;
	}
	end[3] = 0;

	if ( progs[progIndex].ident == VPROG_INTERACTION ) {
		interactionColorMode_t detectedMode = ICM_PACKED;
		if ( !RB_DetectInteractionColorMode( start, detectedMode ) ) {
			common->Warning( "R_LoadARBProgram: failed to infer interaction color mode from %s, defaulting auto mode to %s",
				fullPath.c_str(), RB_InteractionColorModeName( ICM_PACKED ) );
			detectedMode = ICM_PACKED;
		}
		g_interactionVertexProgramAutoColorMode = detectedMode;
		RB_UpdateInteractionColorMode( true );
	}

	glBindProgramARB( progs[progIndex].target, progs[progIndex].ident );
	glGetError();

	glProgramStringARB( progs[progIndex].target, GL_PROGRAM_FORMAT_ASCII_ARB,
		strlen( start ), (unsigned char *)start );

	err = glGetError();
	glGetIntegerv( GL_PROGRAM_ERROR_POSITION_ARB, (GLint *)&ofs );
	if ( err == GL_INVALID_OPERATION ) {
		const GLubyte *str = glGetString( GL_PROGRAM_ERROR_STRING_ARB );
		common->Printf( "\nGL_PROGRAM_ERROR_STRING_ARB: %s\n", str );
		if ( ofs < 0 ) {
			common->Printf( "GL_PROGRAM_ERROR_POSITION_ARB < 0 with error\n" );
		} else if ( ofs >= (int)strlen( (char *)start ) ) {
			common->Printf( "error at end of program\n" );
		} else {
			common->Printf( "error at %i:\n%s", ofs, start + ofs );
		}
		return;
	}
	if ( ofs != -1 ) {
		common->Printf( "\nGL_PROGRAM_ERROR_POSITION_ARB != -1 without error\n" );
		return;
	}

	common->Printf( "\n" );
}

/*
==================
R_FindARBProgram

Returns a GL identifier that can be bound to the given target, parsing
a text file if it hasn't already been loaded.
==================
*/
int R_FindARBProgram( GLenum target, const char *program ) {
	int		i;
	idStr	stripped = program;

	stripped.StripFileExtension();

	// see if it is already loaded
	for ( i = 0 ; progs[i].name[0] ; i++ ) {
		if ( progs[i].target != target ) {
			continue;
		}

		idStr	compare = progs[i].name;
		compare.StripFileExtension();

		if ( !idStr::Icmp( stripped.c_str(), compare.c_str() ) ) {
			return progs[i].ident;
		}
	}

	if ( i == MAX_GLPROGS ) {
		common->Error( "R_FindARBProgram: MAX_GLPROGS" );
	}

	// add it to the list and load it
	progs[i].ident = (program_t)0;	// will be gen'd by R_LoadARBProgram
	progs[i].target = target;
	strncpy( progs[i].name, program, sizeof( progs[i].name ) - 1 );

	R_LoadARBProgram( i );

	return progs[i].ident;
}

/*
==================
R_ReloadARBPrograms_f
==================
*/
void R_ReloadARBPrograms_f( const idCmdArgs &args ) {
	int		i;

	common->Printf( "----- R_ReloadARBPrograms -----\n" );
	for ( i = 0 ; progs[i].name[0] ; i++ ) {
		R_LoadARBProgram( i );
	}
	common->Printf( "-------------------------------\n" );
}

/*
==================
R_ARB2_Init

==================
*/
void R_ARB2_Init( void ) {
	glConfig.allowARB2Path = false;

	common->Printf( "---------- R_ARB2_Init ----------\n" );

	if ( !glConfig.ARBVertexProgramAvailable || !glConfig.ARBFragmentProgramAvailable ) {
		common->Printf( "Not available.\n" );
		return;
	}

	common->Printf( "Available.\n" );

	common->Printf( "---------------------------------\n" );

	glConfig.allowARB2Path = true;
}

