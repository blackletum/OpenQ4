// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ModernClusteredLighting.h"
#include "GLDebugScope.h"
#include "GLStateCache.h"
#include "RendererMetrics.h"

const int MODERN_CLUSTER_MAX_GRIDS = 8;
const int MODERN_CLUSTER_MAX_LIGHTS = 128;
const int MODERN_CLUSTER_MAX_TILES_X = 8;
const int MODERN_CLUSTER_MAX_TILES_Y = 6;
const int MODERN_CLUSTER_MAX_SLICES_Z = 16;
const int MODERN_CLUSTER_MAX_LIGHTS_PER_CLUSTER = 4;
const int MODERN_CLUSTER_MAX_CLUSTERS = MODERN_CLUSTER_MAX_TILES_X * MODERN_CLUSTER_MAX_TILES_Y * MODERN_CLUSTER_MAX_SLICES_Z;
const int MODERN_CLUSTER_INDEX_GROUPS_PER_CLUSTER = ( MODERN_CLUSTER_MAX_LIGHTS_PER_CLUSTER + 3 ) / 4;
const int MODERN_CLUSTER_UBO_BINDING_PARAMS = 3;
const int MODERN_CLUSTER_UBO_BINDING_LIGHTS = 4;
const int MODERN_CLUSTER_UBO_BINDING_INDICES = 5;

enum modernClusterLightType_t {
	MODERN_CLUSTER_LIGHT_POINT = 0,
	MODERN_CLUSTER_LIGHT_PROJECTED,
	MODERN_CLUSTER_LIGHT_FOG,
	MODERN_CLUSTER_LIGHT_AMBIENT,
	MODERN_CLUSTER_LIGHT_SPECIAL
};

typedef struct modernClusterGridRecord_s {
	const viewDef_t *	viewDef;
	char				debugName[64];
	int					sceneIndex;
	int					tileCountX;
	int					tileCountY;
	int					sliceCountZ;
	int					clusterOffset;
	int					clusterCount;
	int					firstLight;
	int					lightCount;
	int					width;
	int					height;
	float				nearZ;
	float				farZ;
} modernClusterGridRecord_t;

typedef struct modernClusterLightRecord_s {
	char				debugName[64];
	modernClusterLightType_t type;
	int					sceneIndex;
	int					lightDefIndex;
	int					areaNum;
	int					flags;
	idVec3				worldOrigin;
	idVec3				cameraOrigin;
	idVec3				color;
	float				radius;
	float				depthMin;
	float				depthMax;
	idScreenRect		scissor;
	bool				fullDepthRange;
} modernClusterLightRecord_t;

typedef struct modernClusterRecord_s {
	unsigned short		lightIndices[MODERN_CLUSTER_MAX_LIGHTS_PER_CLUSTER];
	unsigned char		lightCount;
	bool				overflow;
} modernClusterRecord_t;

typedef struct modernClusterGridGpuParams_s {
	float				grid[4];
	float				depth[4];
	float				viewport[4];
	float				counts[4];
} modernClusterGridGpuParams_t;

typedef struct modernClusterLightGpuRecord_s {
	float				positionRadius[4];
	float				colorType[4];
	float				scissorDepth[4];
	float				flags[4];
} modernClusterLightGpuRecord_t;

typedef struct modernClusterIndexGpuRecord_s {
	GLuint				indices[4];
} modernClusterIndexGpuRecord_t;

typedef struct modernClusterDebugPixel_s {
	unsigned char		r;
	unsigned char		g;
	unsigned char		b;
	unsigned char		a;
} modernClusterDebugPixel_t;

typedef struct modernClusterFrameData_s {
	modernClusterGridRecord_t grids[MODERN_CLUSTER_MAX_GRIDS];
	modernClusterLightRecord_t lights[MODERN_CLUSTER_MAX_LIGHTS];
	modernClusterRecord_t clusters[MODERN_CLUSTER_MAX_CLUSTERS];
	int					gridCount;
	int					lightCount;
	int					clusterCount;
} modernClusterFrameData_t;

static rendererClusteredLightingStats_t rg_clusteredLightingStats;
static modernClusterFrameData_t rg_clusteredLightingFrame;
static renderBackendCaps_t rg_clusteredLightingCaps;
static renderFeatureSet_t rg_clusteredLightingFeatures;
static GLuint rg_clusteredLightingParamsUBO = 0;
static GLuint rg_clusteredLightingLightsUBO = 0;
static GLuint rg_clusteredLightingIndicesUBO = 0;
static GLuint rg_clusteredLightingDebugTexture = 0;
static GLuint rg_clusteredLightingDebugProgram = 0;
static GLuint rg_clusteredLightingDebugVAO = 0;
static GLint rg_clusteredLightingDebugTextureLocation = -1;
static GLint rg_clusteredLightingDebugParamsLocation = -1;
static int rg_clusteredLightingDebugTextureWidth = 0;
static int rg_clusteredLightingDebugTextureHeight = 0;
static bool rg_clusteredLightingInitialized = false;
static bool rg_clusteredLightingAvailable = false;

static void R_ModernClusteredLighting_SetStatus( rendererClusteredLightingStats_t &stats, const char *status ) {
	idStr::snPrintf( stats.status, sizeof( stats.status ), "%s", status ? status : "unknown" );
}

static const char *R_ModernClusteredLighting_TypeName( modernClusterLightType_t type ) {
	switch ( type ) {
	case MODERN_CLUSTER_LIGHT_POINT: return "point";
	case MODERN_CLUSTER_LIGHT_PROJECTED: return "projected";
	case MODERN_CLUSTER_LIGHT_FOG: return "fog";
	case MODERN_CLUSTER_LIGHT_AMBIENT: return "ambient";
	case MODERN_CLUSTER_LIGHT_SPECIAL: return "special";
	default: return "unknown";
	}
}

static int R_ModernClusteredLighting_ViewWidth( const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return Max( 1, glConfig.vidWidth );
	}
	const int viewportWidth = viewDef->viewport.x2 >= viewDef->viewport.x1 ? viewDef->viewport.x2 + 1 - viewDef->viewport.x1 : 0;
	if ( viewportWidth > 0 ) {
		return viewportWidth;
	}
	return Max( 1, viewDef->renderView.width );
}

static int R_ModernClusteredLighting_ViewHeight( const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return Max( 1, glConfig.vidHeight );
	}
	const int viewportHeight = viewDef->viewport.y2 >= viewDef->viewport.y1 ? viewDef->viewport.y2 + 1 - viewDef->viewport.y1 : 0;
	if ( viewportHeight > 0 ) {
		return viewportHeight;
	}
	return Max( 1, viewDef->renderView.height );
}

static void R_ModernClusteredLighting_ClampRectToView( idScreenRect &rect, const viewDef_t *viewDef ) {
	const int width = R_ModernClusteredLighting_ViewWidth( viewDef );
	const int height = R_ModernClusteredLighting_ViewHeight( viewDef );
	if ( rect.IsEmpty() ) {
		rect.x1 = 0;
		rect.y1 = 0;
		rect.x2 = static_cast<short>( width - 1 );
		rect.y2 = static_cast<short>( height - 1 );
		return;
	}
	rect.x1 = static_cast<short>( idMath::ClampInt( 0, width - 1, rect.x1 ) );
	rect.x2 = static_cast<short>( idMath::ClampInt( 0, width - 1, rect.x2 ) );
	rect.y1 = static_cast<short>( idMath::ClampInt( 0, height - 1, rect.y1 ) );
	rect.y2 = static_cast<short>( idMath::ClampInt( 0, height - 1, rect.y2 ) );
}

static int R_ModernClusteredLighting_CeilDiv( int value, int divisor ) {
	return divisor > 0 ? ( value + divisor - 1 ) / divisor : value;
}

static int R_ModernClusteredLighting_DepthSliceForZ( const modernClusterGridRecord_t &grid, float z ) {
	if ( grid.sliceCountZ <= 1 ) {
		return 0;
	}
	const float nearZ = Max( 0.01f, grid.nearZ );
	const float farZ = Max( nearZ + 1.0f, grid.farZ );
	const float clampedZ = idMath::ClampFloat( nearZ, farZ, z );
	const float denom = Max( 0.0001f, idMath::Log( farZ / nearZ ) );
	const float normalized = idMath::ClampFloat( 0.0f, 1.0f, idMath::Log( clampedZ / nearZ ) / denom );
	return idMath::ClampInt( 0, grid.sliceCountZ - 1, idMath::FtoiFast( normalized * static_cast<float>( grid.sliceCountZ ) ) );
}

static void R_ModernClusteredLighting_CameraPoint( const viewDef_t *viewDef, const idVec3 &worldPoint, idVec3 &cameraPoint ) {
	if ( viewDef == NULL ) {
		cameraPoint = worldPoint;
		return;
	}
	const idVec3 delta = worldPoint - viewDef->renderView.vieworg;
	cameraPoint.x = delta * viewDef->renderView.viewaxis[1];
	cameraPoint.y = delta * viewDef->renderView.viewaxis[2];
	cameraPoint.z = delta * viewDef->renderView.viewaxis[0];
}

static float R_ModernClusteredLighting_LightRadius( const viewLight_t *vLight ) {
	if ( vLight == NULL ) {
		return 1.0f;
	}
	float radius = vLight->lightRadius.Length();
	if ( radius <= 1.0f && vLight->lightDef != NULL ) {
		radius = vLight->lightDef->parms.lightRadius.Length();
	}
	if ( radius <= 1.0f ) {
		radius = vLight->pointLight ? 256.0f : 1024.0f;
	}
	return idMath::ClampFloat( 1.0f, 32768.0f, radius );
}

static modernClusterLightType_t R_ModernClusteredLighting_ClassifyLight( const viewLight_t *vLight ) {
	if ( vLight == NULL ) {
		return MODERN_CLUSTER_LIGHT_SPECIAL;
	}
	const idMaterial *shader = vLight->lightShader;
	if ( shader == NULL && vLight->lightDef != NULL ) {
		shader = vLight->lightDef->lightShader;
	}
	if ( shader != NULL ) {
		if ( shader->IsFogLight() ) {
			return MODERN_CLUSTER_LIGHT_FOG;
		}
		if ( shader->IsAmbientLight() ) {
			return MODERN_CLUSTER_LIGHT_AMBIENT;
		}
		if ( shader->IsBlendLight() ) {
			return MODERN_CLUSTER_LIGHT_SPECIAL;
		}
	}
	if ( vLight->pointLight ) {
		return MODERN_CLUSTER_LIGHT_POINT;
	}
	if ( vLight->parallel ) {
		return MODERN_CLUSTER_LIGHT_SPECIAL;
	}
	return MODERN_CLUSTER_LIGHT_PROJECTED;
}

static idVec3 R_ModernClusteredLighting_LightColor( const viewLight_t *vLight ) {
	idVec3 color( 1.0f, 1.0f, 1.0f );
	if ( vLight != NULL && vLight->lightDef != NULL ) {
		color.x = vLight->lightDef->parms.shaderParms[SHADERPARM_RED];
		color.y = vLight->lightDef->parms.shaderParms[SHADERPARM_GREEN];
		color.z = vLight->lightDef->parms.shaderParms[SHADERPARM_BLUE];
	}
	if ( color.x == 0.0f && color.y == 0.0f && color.z == 0.0f ) {
		color.Set( 1.0f, 1.0f, 1.0f );
	}
	return color;
}

static bool R_ModernClusteredLighting_LightAreaVisible( const viewDef_t *viewDef, const viewLight_t *vLight ) {
	if ( viewDef == NULL || vLight == NULL || vLight->lightDef == NULL || viewDef->connectedAreas == NULL ) {
		return true;
	}
	const int areaNum = vLight->lightDef->areaNum;
	if ( areaNum < 0 ) {
		return true;
	}
	return viewDef->connectedAreas[areaNum];
}

static void R_ModernClusteredLighting_CountLightType( modernClusterLightType_t type, rendererClusteredLightingStats_t &stats ) {
	switch ( type ) {
	case MODERN_CLUSTER_LIGHT_POINT: stats.pointLights++; break;
	case MODERN_CLUSTER_LIGHT_PROJECTED: stats.projectedLights++; break;
	case MODERN_CLUSTER_LIGHT_FOG: stats.fogLights++; break;
	case MODERN_CLUSTER_LIGHT_AMBIENT: stats.ambientLights++; break;
	case MODERN_CLUSTER_LIGHT_SPECIAL: stats.specialLights++; break;
	default: stats.specialLights++; break;
	}
}

static void R_ModernClusteredLighting_InitGrid( modernClusterGridRecord_t &grid, const viewDef_t *viewDef, int sceneIndex, int clusterOffset ) {
	memset( &grid, 0, sizeof( grid ) );
	grid.viewDef = viewDef;
	grid.sceneIndex = sceneIndex;
	grid.width = R_ModernClusteredLighting_ViewWidth( viewDef );
	grid.height = R_ModernClusteredLighting_ViewHeight( viewDef );
	const int targetTileWidth = Max( 1, grid.width / MODERN_CLUSTER_MAX_TILES_X );
	const int targetTileHeight = Max( 1, grid.height / MODERN_CLUSTER_MAX_TILES_Y );
	grid.tileCountX = idMath::ClampInt( 1, MODERN_CLUSTER_MAX_TILES_X, R_ModernClusteredLighting_CeilDiv( grid.width, targetTileWidth ) );
	grid.tileCountY = idMath::ClampInt( 1, MODERN_CLUSTER_MAX_TILES_Y, R_ModernClusteredLighting_CeilDiv( grid.height, targetTileHeight ) );
	grid.sliceCountZ = MODERN_CLUSTER_MAX_SLICES_Z;
	grid.clusterOffset = clusterOffset;
	grid.clusterCount = grid.tileCountX * grid.tileCountY * grid.sliceCountZ;
	grid.firstLight = rg_clusteredLightingFrame.lightCount;
	grid.lightCount = 0;
	grid.nearZ = viewDef != NULL && viewDef->renderView.cramZNear ? 0.25f : Max( 0.01f, r_znear.GetFloat() );
	grid.farZ = 4096.0f;
	idStr::snPrintf( grid.debugName, sizeof( grid.debugName ), "scene%d_%dx%dx%d", sceneIndex, grid.tileCountX, grid.tileCountY, grid.sliceCountZ );
}

static int R_ModernClusteredLighting_ClusterIndex( const modernClusterGridRecord_t &grid, int tileX, int tileY, int sliceZ ) {
	return grid.clusterOffset + ( sliceZ * grid.tileCountY + tileY ) * grid.tileCountX + tileX;
}

static void R_ModernClusteredLighting_AddClusterReference( modernClusterRecord_t &cluster, int lightIndex, rendererClusteredLightingStats_t &stats ) {
	if ( cluster.lightCount < MODERN_CLUSTER_MAX_LIGHTS_PER_CLUSTER ) {
		cluster.lightIndices[cluster.lightCount++] = static_cast<unsigned short>( lightIndex );
		stats.lightReferences++;
		stats.maxLightsInCluster = Max( stats.maxLightsInCluster, static_cast<int>( cluster.lightCount ) );
		return;
	}
	cluster.overflow = true;
	stats.overflow = true;
	stats.overflowReferences++;
}

static void R_ModernClusteredLighting_BinLight( const modernClusterGridRecord_t &grid, int lightIndex, const modernClusterLightRecord_t &light, rendererClusteredLightingStats_t &stats ) {
	const int tileMinX = idMath::ClampInt( 0, grid.tileCountX - 1, ( light.scissor.x1 * grid.tileCountX ) / Max( 1, grid.width ) );
	const int tileMaxX = idMath::ClampInt( 0, grid.tileCountX - 1, ( light.scissor.x2 * grid.tileCountX ) / Max( 1, grid.width ) );
	const int tileMinY = idMath::ClampInt( 0, grid.tileCountY - 1, ( light.scissor.y1 * grid.tileCountY ) / Max( 1, grid.height ) );
	const int tileMaxY = idMath::ClampInt( 0, grid.tileCountY - 1, ( light.scissor.y2 * grid.tileCountY ) / Max( 1, grid.height ) );
	const int sliceMinZ = light.fullDepthRange ? 0 : R_ModernClusteredLighting_DepthSliceForZ( grid, light.depthMin );
	const int sliceMaxZ = light.fullDepthRange ? grid.sliceCountZ - 1 : R_ModernClusteredLighting_DepthSliceForZ( grid, light.depthMax );

	for ( int z = sliceMinZ; z <= sliceMaxZ; ++z ) {
		for ( int y = tileMinY; y <= tileMaxY; ++y ) {
			for ( int x = tileMinX; x <= tileMaxX; ++x ) {
				const int clusterIndex = R_ModernClusteredLighting_ClusterIndex( grid, x, y, z );
				if ( clusterIndex >= 0 && clusterIndex < MODERN_CLUSTER_MAX_CLUSTERS ) {
					R_ModernClusteredLighting_AddClusterReference( rg_clusteredLightingFrame.clusters[clusterIndex], lightIndex, stats );
				}
			}
		}
	}
}

static bool R_ModernClusteredLighting_AddLight( modernClusterGridRecord_t &grid, const viewLight_t *vLight, rendererClusteredLightingStats_t &stats ) {
	if ( vLight == NULL ) {
		return false;
	}
	if ( !R_ModernClusteredLighting_LightAreaVisible( grid.viewDef, vLight ) ) {
		stats.culledLights++;
		return false;
	}
	idScreenRect scissor = vLight->scissorRect;
	if ( scissor.IsEmpty() ) {
		stats.clippedLights++;
		return false;
	}
	R_ModernClusteredLighting_ClampRectToView( scissor, grid.viewDef );
	if ( scissor.IsEmpty() ) {
		stats.clippedLights++;
		return false;
	}
	if ( rg_clusteredLightingFrame.lightCount >= MODERN_CLUSTER_MAX_LIGHTS ) {
		stats.overflow = true;
		stats.overflowLights++;
		return false;
	}

	modernClusterLightRecord_t &record = rg_clusteredLightingFrame.lights[rg_clusteredLightingFrame.lightCount];
	memset( &record, 0, sizeof( record ) );
	record.type = R_ModernClusteredLighting_ClassifyLight( vLight );
	record.sceneIndex = grid.sceneIndex;
	record.lightDefIndex = vLight->lightDef != NULL ? vLight->lightDef->index : -1;
	record.areaNum = vLight->lightDef != NULL ? vLight->lightDef->areaNum : -1;
	record.worldOrigin = vLight->globalLightOrigin;
	record.color = R_ModernClusteredLighting_LightColor( vLight );
	record.radius = R_ModernClusteredLighting_LightRadius( vLight );
	record.scissor = scissor;
	R_ModernClusteredLighting_CameraPoint( grid.viewDef, record.worldOrigin, record.cameraOrigin );
	record.fullDepthRange = record.type == MODERN_CLUSTER_LIGHT_FOG || record.type == MODERN_CLUSTER_LIGHT_AMBIENT || record.type == MODERN_CLUSTER_LIGHT_SPECIAL;
	record.depthMin = record.fullDepthRange ? grid.nearZ : Max( grid.nearZ, record.cameraOrigin.z - record.radius );
	record.depthMax = record.fullDepthRange ? grid.farZ : Min( grid.farZ, record.cameraOrigin.z + record.radius );
	if ( record.depthMax < grid.nearZ || record.depthMin > grid.farZ ) {
		stats.culledLights++;
		return false;
	}
	record.flags =
		( vLight->viewInsideLight ? 1 : 0 ) |
		( vLight->viewSeesGlobalLightOrigin ? 2 : 0 ) |
		( vLight->parallel ? 4 : 0 ) |
		( record.fullDepthRange ? 8 : 0 );
	const char *shaderName = vLight->lightShader != NULL ? vLight->lightShader->GetName() : ( vLight->lightDef != NULL && vLight->lightDef->lightShader != NULL ? vLight->lightDef->lightShader->GetName() : "<light>" );
	idStr::snPrintf( record.debugName, sizeof( record.debugName ), "%s:%d:%s", R_ModernClusteredLighting_TypeName( record.type ), record.lightDefIndex, shaderName != NULL ? shaderName : "<null>" );

	const int lightIndex = rg_clusteredLightingFrame.lightCount++;
	grid.lightCount++;
	stats.lightCount++;
	R_ModernClusteredLighting_CountLightType( record.type, stats );
	if ( record.depthMax > grid.farZ ) {
		grid.farZ = idMath::ClampFloat( grid.nearZ + 1.0f, 32768.0f, record.depthMax );
	}
	R_ModernClusteredLighting_BinLight( grid, lightIndex, record, stats );
	return true;
}

static void R_ModernClusteredLighting_FinalizeClusterStats( rendererClusteredLightingStats_t &stats ) {
	stats.clusterCount = rg_clusteredLightingFrame.clusterCount;
	stats.maxLightsPerCluster = MODERN_CLUSTER_MAX_LIGHTS_PER_CLUSTER;
	for ( int i = 0; i < rg_clusteredLightingFrame.clusterCount; ++i ) {
		const modernClusterRecord_t &cluster = rg_clusteredLightingFrame.clusters[i];
		if ( cluster.lightCount > 0 ) {
			stats.activeClusters++;
		}
		if ( cluster.overflow ) {
			stats.overflowClusters++;
		}
	}
	stats.frameValid = true;
}

static void R_ModernClusteredLighting_BuildFrame( const idScenePacketFrame &packetFrame, bool requested, rendererClusteredLightingStats_t &stats ) {
	memset( &rg_clusteredLightingFrame, 0, sizeof( rg_clusteredLightingFrame ) );
	memset( &stats, 0, sizeof( stats ) );
	stats.available = rg_clusteredLightingAvailable;
	stats.initialized = rg_clusteredLightingInitialized;
	stats.requested = requested;
	stats.debugMode = r_rendererClusterDebug.GetInteger();
	stats.debugOverlayReady = rg_clusteredLightingDebugProgram != 0 && rg_clusteredLightingDebugVAO != 0;
	stats.debugTextureReady = rg_clusteredLightingDebugTexture != 0;
	stats.maxLightsPerCluster = MODERN_CLUSTER_MAX_LIGHTS_PER_CLUSTER;
	stats.sceneCount = packetFrame.NumScenes();
	R_ModernClusteredLighting_SetStatus( stats, requested ? "unavailable" : "off" );

	if ( !requested ) {
		return;
	}
	if ( !rg_clusteredLightingAvailable ) {
		return;
	}
	if ( !rg_clusteredLightingInitialized ) {
		R_ModernClusteredLighting_SetStatus( stats, "not-initialized" );
		return;
	}

	const int startMsec = Sys_Milliseconds();
	for ( int sceneIndex = 0; sceneIndex < packetFrame.NumScenes(); ++sceneIndex ) {
		const scenePacket_t &scene = packetFrame.Scene( sceneIndex );
		if ( scene.viewDef == NULL ) {
			continue;
		}
		if ( rg_clusteredLightingFrame.gridCount >= MODERN_CLUSTER_MAX_GRIDS ) {
			stats.overflow = true;
			break;
		}
		if ( rg_clusteredLightingFrame.clusterCount >= MODERN_CLUSTER_MAX_CLUSTERS ) {
			stats.overflow = true;
			break;
		}
		modernClusterGridRecord_t &grid = rg_clusteredLightingFrame.grids[rg_clusteredLightingFrame.gridCount];
		R_ModernClusteredLighting_InitGrid( grid, scene.viewDef, sceneIndex, rg_clusteredLightingFrame.clusterCount );
		if ( rg_clusteredLightingFrame.clusterCount + grid.clusterCount > MODERN_CLUSTER_MAX_CLUSTERS ) {
			stats.overflow = true;
			break;
		}
		rg_clusteredLightingFrame.gridCount++;
		rg_clusteredLightingFrame.clusterCount += grid.clusterCount;
		stats.gridCount++;
		if ( stats.gridCount == 1 ) {
			stats.tileCountX = grid.tileCountX;
			stats.tileCountY = grid.tileCountY;
			stats.sliceCountZ = grid.sliceCountZ;
			stats.nearZ = idMath::FtoiFast( grid.nearZ );
			stats.farZ = idMath::FtoiFast( grid.farZ );
		}
		for ( const viewLight_t *vLight = scene.viewDef->viewLights; vLight != NULL; vLight = vLight->next ) {
			R_ModernClusteredLighting_AddLight( grid, vLight, stats );
		}
		if ( grid.lightCount > 0 ) {
			stats.scenesWithLights++;
		}
		if ( stats.gridCount == 1 ) {
			stats.farZ = idMath::FtoiFast( grid.farZ );
		}
	}
	stats.buildMsec = Sys_Milliseconds() - startMsec;
	R_ModernClusteredLighting_FinalizeClusterStats( stats );
	R_ModernClusteredLighting_SetStatus( stats, stats.overflow ? "prepared-overflow" : "prepared" );
}

static GLuint R_ModernClusteredLighting_CompileShaderStage( GLenum stage, const char *source, const char *label ) {
	GLuint shader = glCreateShader( stage );
	if ( shader == 0 ) {
		return 0;
	}
	glShaderSource( shader, 1, &source, NULL );
	glCompileShader( shader );
	GLint compiled = GL_FALSE;
	glGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );
	if ( compiled != GL_TRUE ) {
		char log[2048];
		memset( log, 0, sizeof( log ) );
		if ( glGetShaderInfoLog != NULL ) {
			glGetShaderInfoLog( shader, sizeof( log ) - 1, NULL, log );
		}
		common->Printf( "Modern clustered lighting: %s shader compile failed: %s\n", label ? label : "debug", log );
		glDeleteShader( shader );
		return 0;
	}
	return shader;
}

static GLuint R_ModernClusteredLighting_CompileDebugProgram( void ) {
	static const char *vertexSource =
		"#version 330\n"
		"out vec2 vTexCoord;\n"
		"void main() {\n"
		"	vec2 positions[4] = vec2[]( vec2(-0.96, -0.46), vec2(-0.46, -0.46), vec2(-0.96, -0.96), vec2(-0.46, -0.96) );\n"
		"	vec2 texcoords[4] = vec2[]( vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0, 0.0), vec2(1.0, 0.0) );\n"
		"	vTexCoord = texcoords[gl_VertexID];\n"
		"	gl_Position = vec4( positions[gl_VertexID], 0.0, 1.0 );\n"
		"}\n";
	static const char *fragmentSource =
		"#version 330\n"
		"in vec2 vTexCoord;\n"
		"layout(location = 0) out vec4 out_Color;\n"
		"uniform sampler2D uClusterTexture;\n"
		"uniform vec4 uParams;\n"
		"void main() {\n"
		"	vec4 value = texture( uClusterTexture, vTexCoord );\n"
		"	out_Color = vec4( value.rgb, 1.0 );\n"
		"}\n";

	if ( glCreateShader == NULL || glShaderSource == NULL || glCompileShader == NULL || glCreateProgram == NULL || glAttachShader == NULL || glLinkProgram == NULL || glGetProgramiv == NULL ) {
		return 0;
	}
	GLuint vertexShader = R_ModernClusteredLighting_CompileShaderStage( GL_VERTEX_SHADER, vertexSource, "cluster debug vertex" );
	if ( vertexShader == 0 ) {
		return 0;
	}
	GLuint fragmentShader = R_ModernClusteredLighting_CompileShaderStage( GL_FRAGMENT_SHADER, fragmentSource, "cluster debug fragment" );
	if ( fragmentShader == 0 ) {
		glDeleteShader( vertexShader );
		return 0;
	}
	GLuint program = glCreateProgram();
	if ( program == 0 ) {
		glDeleteShader( vertexShader );
		glDeleteShader( fragmentShader );
		return 0;
	}
	glAttachShader( program, vertexShader );
	glAttachShader( program, fragmentShader );
	glLinkProgram( program );
	glDetachShader( program, vertexShader );
	glDetachShader( program, fragmentShader );
	glDeleteShader( vertexShader );
	glDeleteShader( fragmentShader );
	GLint linked = GL_FALSE;
	glGetProgramiv( program, GL_LINK_STATUS, &linked );
	if ( linked != GL_TRUE ) {
		char log[2048];
		memset( log, 0, sizeof( log ) );
		if ( glGetProgramInfoLog != NULL ) {
			glGetProgramInfoLog( program, sizeof( log ) - 1, NULL, log );
		}
		common->Printf( "Modern clustered lighting: debug overlay program link failed: %s\n", log );
		glDeleteProgram( program );
		return 0;
	}
	rg_clusteredLightingDebugTextureLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "uClusterTexture" ) : -1;
	rg_clusteredLightingDebugParamsLocation = glGetUniformLocation != NULL ? glGetUniformLocation( program, "uParams" ) : -1;
	return program;
}

static bool R_ModernClusteredLighting_CreateBuffer( GLenum target, int bytes, GLuint &buffer, const char *label ) {
	buffer = 0;
	if ( bytes <= 0 || glGenBuffers == NULL || glBindBuffer == NULL || glBufferData == NULL ) {
		return false;
	}
	glGenBuffers( 1, &buffer );
	if ( buffer == 0 ) {
		return false;
	}
	R_GLStateCache().BindBuffer( target, buffer );
	glBufferData( target, bytes, NULL, GL_DYNAMIC_DRAW );
	R_GLStateCache().BindBuffer( target, 0 );
	R_GLDebug_LabelBuffer( buffer, label );
	return true;
}

static bool R_ModernClusteredLighting_EnsureDebugTexture( int width, int height ) {
	if ( width <= 0 || height <= 0 || glGenTextures == NULL || glBindTexture == NULL || glTexImage2D == NULL ) {
		return false;
	}
	if ( rg_clusteredLightingDebugTexture == 0 ) {
		glGenTextures( 1, &rg_clusteredLightingDebugTexture );
		R_GLDebug_LabelTexture( rg_clusteredLightingDebugTexture, "Modern clustered lighting debug texture" );
	}
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, rg_clusteredLightingDebugTexture );
	if ( rg_clusteredLightingDebugTextureWidth != width || rg_clusteredLightingDebugTextureHeight != height ) {
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		rg_clusteredLightingDebugTextureWidth = width;
		rg_clusteredLightingDebugTextureHeight = height;
	}
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, 0 );
	return rg_clusteredLightingDebugTexture != 0;
}

static bool R_ModernClusteredLighting_UploadBuffers( rendererClusteredLightingStats_t &stats ) {
	stats.paramsUBOBytes = sizeof( modernClusterGridGpuParams_t );
	stats.lightsUBOBytes = sizeof( modernClusterLightGpuRecord_t ) * MODERN_CLUSTER_MAX_LIGHTS;
	stats.indicesUBOBytes = sizeof( modernClusterIndexGpuRecord_t ) * MODERN_CLUSTER_MAX_CLUSTERS * MODERN_CLUSTER_INDEX_GROUPS_PER_CLUSTER;
	if ( !stats.requested || !stats.frameValid || rg_clusteredLightingParamsUBO == 0 || rg_clusteredLightingLightsUBO == 0 || rg_clusteredLightingIndicesUBO == 0 || glBufferSubData == NULL || glBindBufferBase == NULL ) {
		return false;
	}

	modernClusterGridGpuParams_t params;
	memset( &params, 0, sizeof( params ) );
	if ( rg_clusteredLightingFrame.gridCount > 0 ) {
		const modernClusterGridRecord_t &grid = rg_clusteredLightingFrame.grids[0];
		params.grid[0] = static_cast<float>( grid.tileCountX );
		params.grid[1] = static_cast<float>( grid.tileCountY );
		params.grid[2] = static_cast<float>( grid.sliceCountZ );
		params.grid[3] = static_cast<float>( MODERN_CLUSTER_MAX_LIGHTS_PER_CLUSTER );
		params.depth[0] = grid.nearZ;
		params.depth[1] = grid.farZ;
		params.depth[2] = grid.farZ > grid.nearZ ? 1.0f / ( grid.farZ - grid.nearZ ) : 1.0f;
		params.depth[3] = Max( 0.0001f, idMath::Log( grid.farZ / Max( 0.01f, grid.nearZ ) ) );
		params.viewport[0] = static_cast<float>( grid.width );
		params.viewport[1] = static_cast<float>( grid.height );
		params.viewport[2] = grid.height > 0 ? static_cast<float>( grid.width ) / static_cast<float>( grid.height ) : 1.0f;
		params.viewport[3] = 1.0f;
	}
	params.counts[0] = static_cast<float>( stats.lightCount );
	params.counts[1] = static_cast<float>( stats.clusterCount );
	params.counts[2] = static_cast<float>( stats.activeClusters );
	params.counts[3] = static_cast<float>( stats.overflowClusters );

	modernClusterLightGpuRecord_t lightRecords[MODERN_CLUSTER_MAX_LIGHTS];
	memset( lightRecords, 0, sizeof( lightRecords ) );
	for ( int i = 0; i < rg_clusteredLightingFrame.lightCount; ++i ) {
		const modernClusterLightRecord_t &src = rg_clusteredLightingFrame.lights[i];
		modernClusterLightGpuRecord_t &dst = lightRecords[i];
		dst.positionRadius[0] = src.cameraOrigin.x;
		dst.positionRadius[1] = src.cameraOrigin.y;
		dst.positionRadius[2] = src.cameraOrigin.z;
		dst.positionRadius[3] = src.radius;
		dst.colorType[0] = src.color.x;
		dst.colorType[1] = src.color.y;
		dst.colorType[2] = src.color.z;
		dst.colorType[3] = static_cast<float>( src.type );
		dst.scissorDepth[0] = static_cast<float>( src.scissor.x1 );
		dst.scissorDepth[1] = static_cast<float>( src.scissor.y1 );
		dst.scissorDepth[2] = src.depthMin;
		dst.scissorDepth[3] = src.depthMax;
		dst.flags[0] = static_cast<float>( src.flags );
		dst.flags[1] = static_cast<float>( src.lightDefIndex );
		dst.flags[2] = static_cast<float>( src.areaNum );
		dst.flags[3] = static_cast<float>( src.sceneIndex );
	}

	modernClusterIndexGpuRecord_t indexRecords[MODERN_CLUSTER_MAX_CLUSTERS * MODERN_CLUSTER_INDEX_GROUPS_PER_CLUSTER];
	for ( int i = 0; i < static_cast<int>( sizeof( indexRecords ) / sizeof( indexRecords[0] ) ); ++i ) {
		indexRecords[i].indices[0] = 0xffffffffu;
		indexRecords[i].indices[1] = 0xffffffffu;
		indexRecords[i].indices[2] = 0xffffffffu;
		indexRecords[i].indices[3] = 0xffffffffu;
	}
	for ( int clusterIndex = 0; clusterIndex < rg_clusteredLightingFrame.clusterCount; ++clusterIndex ) {
		const modernClusterRecord_t &cluster = rg_clusteredLightingFrame.clusters[clusterIndex];
		for ( int i = 0; i < cluster.lightCount; ++i ) {
			const int groupIndex = clusterIndex * MODERN_CLUSTER_INDEX_GROUPS_PER_CLUSTER + i / 4;
			indexRecords[groupIndex].indices[i & 3] = cluster.lightIndices[i];
		}
	}

	R_GLStateCache().BindBuffer( GL_UNIFORM_BUFFER, rg_clusteredLightingParamsUBO );
	glBufferSubData( GL_UNIFORM_BUFFER, 0, sizeof( params ), &params );
	R_GLStateCache().BindBuffer( GL_UNIFORM_BUFFER, rg_clusteredLightingLightsUBO );
	glBufferSubData( GL_UNIFORM_BUFFER, 0, sizeof( lightRecords ), lightRecords );
	R_GLStateCache().BindBuffer( GL_UNIFORM_BUFFER, rg_clusteredLightingIndicesUBO );
	glBufferSubData( GL_UNIFORM_BUFFER, 0, sizeof( indexRecords ), indexRecords );
	R_GLStateCache().BindBuffer( GL_UNIFORM_BUFFER, 0 );
	R_GLStateCache().BindBufferBase( GL_UNIFORM_BUFFER, MODERN_CLUSTER_UBO_BINDING_PARAMS, rg_clusteredLightingParamsUBO );
	R_GLStateCache().BindBufferBase( GL_UNIFORM_BUFFER, MODERN_CLUSTER_UBO_BINDING_LIGHTS, rg_clusteredLightingLightsUBO );
	R_GLStateCache().BindBufferBase( GL_UNIFORM_BUFFER, MODERN_CLUSTER_UBO_BINDING_INDICES, rg_clusteredLightingIndicesUBO );

	stats.bufferUploads += 3;
	stats.buffersReady = true;
	stats.uboFallbackReady = true;
	return true;
}

static void R_ModernClusteredLighting_ColorForCluster( const modernClusterRecord_t &cluster, int debugMode, modernClusterDebugPixel_t &pixel ) {
	const int count = cluster.lightCount;
	pixel.r = pixel.g = pixel.b = 0;
	pixel.a = 255;
	if ( debugMode == RENDERER_CLUSTER_DEBUG_OVERFLOW ) {
		pixel.r = cluster.overflow ? 255 : 18;
		pixel.g = cluster.overflow ? 32 : 18;
		pixel.b = cluster.overflow ? 32 : 18;
		return;
	}
	if ( debugMode == RENDERER_CLUSTER_DEBUG_OCCUPANCY ) {
		const unsigned char v = static_cast<unsigned char>( idMath::ClampInt( 0, 255, count * 64 ) );
		pixel.r = count > 0 ? 16 : 0;
		pixel.g = v;
		pixel.b = count > 0 ? 80 : 0;
		return;
	}
	const int scaled = idMath::ClampInt( 0, 255, count * 64 );
	pixel.r = static_cast<unsigned char>( scaled );
	pixel.g = static_cast<unsigned char>( count > 1 ? Min( 255, 64 + scaled / 2 ) : scaled / 2 );
	pixel.b = static_cast<unsigned char>( count == 0 ? 32 : Max( 0, 192 - scaled ) );
}

static bool R_ModernClusteredLighting_UpdateDebugTexture( rendererClusteredLightingStats_t &stats ) {
	if ( r_rendererClusterDebug.GetInteger() <= 0 || rg_clusteredLightingFrame.gridCount <= 0 ) {
		return false;
	}
	const modernClusterGridRecord_t &grid = rg_clusteredLightingFrame.grids[0];
	const int width = grid.tileCountX * grid.sliceCountZ;
	const int height = grid.tileCountY;
	if ( width <= 0 || height <= 0 || !R_ModernClusteredLighting_EnsureDebugTexture( width, height ) ) {
		return false;
	}
	modernClusterDebugPixel_t pixels[MODERN_CLUSTER_MAX_TILES_X * MODERN_CLUSTER_MAX_SLICES_Z * MODERN_CLUSTER_MAX_TILES_Y];
	memset( pixels, 0, sizeof( pixels ) );
	const int debugMode = idMath::ClampInt( RENDERER_CLUSTER_DEBUG_OCCUPANCY, RENDERER_CLUSTER_DEBUG_OVERFLOW, r_rendererClusterDebug.GetInteger() );
	for ( int z = 0; z < grid.sliceCountZ; ++z ) {
		for ( int y = 0; y < grid.tileCountY; ++y ) {
			for ( int x = 0; x < grid.tileCountX; ++x ) {
				const int clusterIndex = R_ModernClusteredLighting_ClusterIndex( grid, x, y, z );
				const int pixelIndex = y * width + z * grid.tileCountX + x;
				R_ModernClusteredLighting_ColorForCluster( rg_clusteredLightingFrame.clusters[clusterIndex], debugMode, pixels[pixelIndex] );
			}
		}
	}
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, rg_clusteredLightingDebugTexture );
	glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels );
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, 0 );
	stats.debugTextureReady = true;
	return true;
}

void R_ModernClusteredLighting_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	rg_clusteredLightingCaps = caps;
	rg_clusteredLightingFeatures = features;
	rg_clusteredLightingAvailable = features.modernBaseline && caps.hasUBO && caps.hasVAO && glBindBufferBase != NULL;
	if ( !rg_clusteredLightingAvailable ) {
		memset( &rg_clusteredLightingStats, 0, sizeof( rg_clusteredLightingStats ) );
		R_ModernClusteredLighting_SetStatus( rg_clusteredLightingStats, "unavailable" );
		return;
	}

	const int paramsBytes = sizeof( modernClusterGridGpuParams_t );
	const int lightBytes = sizeof( modernClusterLightGpuRecord_t ) * MODERN_CLUSTER_MAX_LIGHTS;
	const int indexBytes = sizeof( modernClusterIndexGpuRecord_t ) * MODERN_CLUSTER_MAX_CLUSTERS * MODERN_CLUSTER_INDEX_GROUPS_PER_CLUSTER;
	if ( !R_ModernClusteredLighting_CreateBuffer( GL_UNIFORM_BUFFER, paramsBytes, rg_clusteredLightingParamsUBO, "Modern clustered lighting grid params UBO" ) ||
		!R_ModernClusteredLighting_CreateBuffer( GL_UNIFORM_BUFFER, lightBytes, rg_clusteredLightingLightsUBO, "Modern clustered lighting light records UBO" ) ||
		!R_ModernClusteredLighting_CreateBuffer( GL_UNIFORM_BUFFER, indexBytes, rg_clusteredLightingIndicesUBO, "Modern clustered lighting indices UBO" ) ) {
		R_ModernClusteredLighting_Shutdown();
		R_ModernClusteredLighting_SetStatus( rg_clusteredLightingStats, "buffer-init-failed" );
		return;
	}
	if ( glGenVertexArrays != NULL ) {
		glGenVertexArrays( 1, &rg_clusteredLightingDebugVAO );
		if ( rg_clusteredLightingDebugVAO != 0 ) {
			R_GLDebug_LabelVertexArray( rg_clusteredLightingDebugVAO, "Modern clustered lighting debug VAO" );
		}
	}
	rg_clusteredLightingDebugProgram = R_ModernClusteredLighting_CompileDebugProgram();
	if ( rg_clusteredLightingDebugProgram != 0 ) {
		R_GLDebug_LabelProgram( rg_clusteredLightingDebugProgram, "Modern clustered lighting debug overlay" );
	}
	rg_clusteredLightingInitialized = true;
	memset( &rg_clusteredLightingStats, 0, sizeof( rg_clusteredLightingStats ) );
	rg_clusteredLightingStats.available = true;
	rg_clusteredLightingStats.initialized = true;
	rg_clusteredLightingStats.buffersReady = true;
	rg_clusteredLightingStats.uboFallbackReady = true;
	rg_clusteredLightingStats.debugOverlayReady = rg_clusteredLightingDebugProgram != 0 && rg_clusteredLightingDebugVAO != 0;
	rg_clusteredLightingStats.paramsUBOBytes = paramsBytes;
	rg_clusteredLightingStats.lightsUBOBytes = lightBytes;
	rg_clusteredLightingStats.indicesUBOBytes = indexBytes;
	R_ModernClusteredLighting_SetStatus( rg_clusteredLightingStats, "initialized" );
}

void R_ModernClusteredLighting_Shutdown( void ) {
	GLuint buffers[3];
	int numBuffers = 0;
	if ( rg_clusteredLightingParamsUBO != 0 ) {
		buffers[numBuffers++] = rg_clusteredLightingParamsUBO;
	}
	if ( rg_clusteredLightingLightsUBO != 0 ) {
		buffers[numBuffers++] = rg_clusteredLightingLightsUBO;
	}
	if ( rg_clusteredLightingIndicesUBO != 0 ) {
		buffers[numBuffers++] = rg_clusteredLightingIndicesUBO;
	}
	if ( numBuffers > 0 && glDeleteBuffers != NULL ) {
		glDeleteBuffers( numBuffers, buffers );
	}
	if ( rg_clusteredLightingDebugTexture != 0 && glDeleteTextures != NULL ) {
		glDeleteTextures( 1, &rg_clusteredLightingDebugTexture );
	}
	if ( rg_clusteredLightingDebugProgram != 0 && glDeleteProgram != NULL ) {
		glDeleteProgram( rg_clusteredLightingDebugProgram );
	}
	if ( rg_clusteredLightingDebugVAO != 0 && glDeleteVertexArrays != NULL ) {
		glDeleteVertexArrays( 1, &rg_clusteredLightingDebugVAO );
	}
	rg_clusteredLightingParamsUBO = 0;
	rg_clusteredLightingLightsUBO = 0;
	rg_clusteredLightingIndicesUBO = 0;
	rg_clusteredLightingDebugTexture = 0;
	rg_clusteredLightingDebugProgram = 0;
	rg_clusteredLightingDebugVAO = 0;
	rg_clusteredLightingDebugTextureWidth = 0;
	rg_clusteredLightingDebugTextureHeight = 0;
	rg_clusteredLightingInitialized = false;
	rg_clusteredLightingAvailable = false;
	memset( &rg_clusteredLightingStats, 0, sizeof( rg_clusteredLightingStats ) );
	R_ModernClusteredLighting_SetStatus( rg_clusteredLightingStats, "off" );
}

void R_ModernClusteredLighting_PrepareFrame( const idScenePacketFrame &packetFrame, bool requested ) {
	R_ModernClusteredLighting_BuildFrame( packetFrame, requested || r_rendererClusterDebug.GetInteger() > 0, rg_clusteredLightingStats );
	R_ModernClusteredLighting_UploadBuffers( rg_clusteredLightingStats );
	R_ModernClusteredLighting_UpdateDebugTexture( rg_clusteredLightingStats );
	R_RendererMetrics_RecordClusteredLighting( rg_clusteredLightingStats );
	if ( r_rendererMetrics.GetInteger() >= 2 && rg_clusteredLightingStats.requested ) {
		common->Printf(
			"clusteredLighting status=%s requested=%d valid=%d grids=%d scenes=%d lights=%d point=%d projected=%d fog=%d ambient=%d special=%d clusters=%d active=%d refs=%d overflow=%d/%d overflowRefs=%d maxCluster=%d grid=%dx%dx%d z=%d..%d ubo=%d buffers=%d bytes(params=%d lights=%d indices=%d) debug=%d/%d build=%dms uploads=%d\n",
			rg_clusteredLightingStats.status,
			rg_clusteredLightingStats.requested ? 1 : 0,
			rg_clusteredLightingStats.frameValid ? 1 : 0,
			rg_clusteredLightingStats.gridCount,
			rg_clusteredLightingStats.sceneCount,
			rg_clusteredLightingStats.lightCount,
			rg_clusteredLightingStats.pointLights,
			rg_clusteredLightingStats.projectedLights,
			rg_clusteredLightingStats.fogLights,
			rg_clusteredLightingStats.ambientLights,
			rg_clusteredLightingStats.specialLights,
			rg_clusteredLightingStats.clusterCount,
			rg_clusteredLightingStats.activeClusters,
			rg_clusteredLightingStats.lightReferences,
			rg_clusteredLightingStats.overflow ? 1 : 0,
			rg_clusteredLightingStats.overflowClusters,
			rg_clusteredLightingStats.overflowReferences,
			rg_clusteredLightingStats.maxLightsInCluster,
			rg_clusteredLightingStats.tileCountX,
			rg_clusteredLightingStats.tileCountY,
			rg_clusteredLightingStats.sliceCountZ,
			rg_clusteredLightingStats.nearZ,
			rg_clusteredLightingStats.farZ,
			rg_clusteredLightingStats.uboFallbackReady ? 1 : 0,
			rg_clusteredLightingStats.buffersReady ? 1 : 0,
			rg_clusteredLightingStats.paramsUBOBytes,
			rg_clusteredLightingStats.lightsUBOBytes,
			rg_clusteredLightingStats.indicesUBOBytes,
			rg_clusteredLightingStats.debugOverlayReady ? 1 : 0,
			rg_clusteredLightingStats.debugMode,
			rg_clusteredLightingStats.buildMsec,
			rg_clusteredLightingStats.bufferUploads );
	}
}

void R_ModernClusteredLighting_DrawDebugOverlay( void ) {
	const int debugMode = r_rendererClusterDebug.GetInteger();
	if ( debugMode <= 0 || rg_clusteredLightingDebugProgram == 0 || rg_clusteredLightingDebugTexture == 0 || rg_clusteredLightingDebugVAO == 0 || !rg_clusteredLightingStats.debugTextureReady ) {
		return;
	}
	idGLDebugScope overlayScope( "Modern clustered lighting debug overlay" );
	R_GLStateCache_InvalidateAll( "modern clustered lighting debug overlay" );
	R_GLStateCache().BindFramebuffer( GL_FRAMEBUFFER, 0 );
	R_GLStateCache().BindVertexArray( rg_clusteredLightingDebugVAO );
	R_GLStateCache().SetViewport( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	R_GLStateCache().SetScissor( 0, 0, Max( 1, glConfig.vidWidth ), Max( 1, glConfig.vidHeight ) );
	R_GLStateCache().SetScissorTestEnabled( false );
	R_GLStateCache().SetDepthTestEnabled( false );
	R_GLStateCache().SetDepthMask( GL_FALSE );
	R_GLStateCache().SetStencilTestEnabled( false );
	R_GLStateCache().SetBlendEnabled( false );
	R_GLStateCache().SetCullFaceEnabled( false );
	R_GLStateCache().SetColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	R_GLStateCache().UseProgram( rg_clusteredLightingDebugProgram );
	R_GLStateCache().ActiveTextureUnit( 0 );
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, rg_clusteredLightingDebugTexture );
	if ( rg_clusteredLightingDebugTextureLocation >= 0 ) {
		glUniform1i( rg_clusteredLightingDebugTextureLocation, 0 );
	}
	if ( rg_clusteredLightingDebugParamsLocation >= 0 ) {
		glUniform4f( rg_clusteredLightingDebugParamsLocation, static_cast<float>( debugMode ), static_cast<float>( rg_clusteredLightingStats.tileCountX ), static_cast<float>( rg_clusteredLightingStats.tileCountY ), static_cast<float>( rg_clusteredLightingStats.sliceCountZ ) );
	}
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	rg_clusteredLightingStats.debugOverlayDraws++;
	R_RendererMetrics_RecordClusteredLighting( rg_clusteredLightingStats );
	R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, 0 );
	R_GLStateCache().UseProgram( 0 );
	R_GLStateCache().BindVertexArray( 0 );
	R_GLStateCache().SetDepthMask( GL_TRUE );
	GL_ClearStateDelta();
}

void R_ModernClusteredLighting_PrintGfxInfo( void ) {
	common->Printf(
		"Modern clustered lighting: %s, requested=%d, cvarDebug=%d, grids=%d scenes=%d lights=%d(point=%d projected=%d fog=%d ambient=%d special=%d) clusters=%d active=%d refs=%d overflow=%d/%d overflowRefs=%d maxCluster=%d grid=%dx%dx%d z=%d..%d ubo=%d buffers=%d bytes(params=%d lights=%d indices=%d) overlay=%d/%d texture=%d build=%dms uploads=%d\n",
		rg_clusteredLightingStats.available ? "available" : "unavailable",
		rg_clusteredLightingStats.requested ? 1 : 0,
		r_rendererClusterDebug.GetInteger(),
		rg_clusteredLightingStats.gridCount,
		rg_clusteredLightingStats.sceneCount,
		rg_clusteredLightingStats.lightCount,
		rg_clusteredLightingStats.pointLights,
		rg_clusteredLightingStats.projectedLights,
		rg_clusteredLightingStats.fogLights,
		rg_clusteredLightingStats.ambientLights,
		rg_clusteredLightingStats.specialLights,
		rg_clusteredLightingStats.clusterCount,
		rg_clusteredLightingStats.activeClusters,
		rg_clusteredLightingStats.lightReferences,
		rg_clusteredLightingStats.overflow ? 1 : 0,
		rg_clusteredLightingStats.overflowClusters,
		rg_clusteredLightingStats.overflowReferences,
		rg_clusteredLightingStats.maxLightsInCluster,
		rg_clusteredLightingStats.tileCountX,
		rg_clusteredLightingStats.tileCountY,
		rg_clusteredLightingStats.sliceCountZ,
		rg_clusteredLightingStats.nearZ,
		rg_clusteredLightingStats.farZ,
		rg_clusteredLightingStats.uboFallbackReady ? 1 : 0,
		rg_clusteredLightingStats.buffersReady ? 1 : 0,
		rg_clusteredLightingStats.paramsUBOBytes,
		rg_clusteredLightingStats.lightsUBOBytes,
		rg_clusteredLightingStats.indicesUBOBytes,
		rg_clusteredLightingStats.debugOverlayReady ? 1 : 0,
		rg_clusteredLightingStats.debugOverlayDraws,
		rg_clusteredLightingStats.debugTextureReady ? 1 : 0,
		rg_clusteredLightingStats.buildMsec,
		rg_clusteredLightingStats.bufferUploads );
}

const rendererClusteredLightingStats_t &R_ModernClusteredLighting_Stats( void ) {
	return rg_clusteredLightingStats;
}

static void R_ModernClusteredLighting_SetupSelfTestView( viewDef_t &view, viewLight_t lights[6], idRenderLightLocal lightDefs[6] ) {
	memset( &view, 0, sizeof( view ) );
	view.renderView.width = 1280;
	view.renderView.height = 720;
	view.renderView.fov_x = 90.0f;
	view.renderView.fov_y = 70.0f;
	view.renderView.vieworg.Zero();
	view.renderView.viewaxis = mat3_identity;
	view.viewport.x1 = 0;
	view.viewport.y1 = 0;
	view.viewport.x2 = 1279;
	view.viewport.y2 = 719;
	view.scissor.x1 = 0;
	view.scissor.y1 = 0;
	view.scissor.x2 = 1279;
	view.scissor.y2 = 719;
	for ( int i = 0; i < 6; ++i ) {
		memset( &lights[i], 0, sizeof( lights[i] ) );
		lightDefs[i].index = i;
		lightDefs[i].areaNum = -1;
		lightDefs[i].parms.origin.Set( 128.0f + 16.0f * i, 0.0f, 0.0f );
		lightDefs[i].parms.lightRadius.Set( 160.0f, 160.0f, 160.0f );
		lightDefs[i].parms.shaderParms[SHADERPARM_RED] = 1.0f;
		lightDefs[i].parms.shaderParms[SHADERPARM_GREEN] = 0.75f;
		lightDefs[i].parms.shaderParms[SHADERPARM_BLUE] = 0.5f;
		lights[i].lightDef = &lightDefs[i];
		lights[i].next = i < 5 ? &lights[i + 1] : NULL;
		lights[i].scissorRect.x1 = 120;
		lights[i].scissorRect.y1 = 120;
		lights[i].scissorRect.x2 = 360;
		lights[i].scissorRect.y2 = 300;
		lights[i].globalLightOrigin = lightDefs[i].parms.origin;
		lights[i].lightRadius = lightDefs[i].parms.lightRadius;
		lights[i].pointLight = ( i & 1 ) == 0;
		lights[i].parallel = false;
		lights[i].viewInsideLight = i == 0;
		lights[i].viewSeesGlobalLightOrigin = true;
	}
	view.viewLights = &lights[0];
}

bool RendererClusterGrid_RunSelfTest( void ) {
	if ( !r_rendererModernExecutor.GetBool() && r_rendererClusterDebug.GetInteger() <= 0 ) {
		common->Printf( "RendererClusterGrid self-test passed (disabled)\n" );
		return true;
	}

	viewDef_t view;
	viewLight_t lights[6];
	idRenderLightLocal lightDefs[6];
	R_ModernClusteredLighting_SetupSelfTestView( view, lights, lightDefs );

	idScenePacketFrame packetFrame;
	packetFrame.Clear();
	if ( !packetFrame.AddScene( &view, true ) ) {
		common->Printf( "RendererClusterGrid self-test failed: could not add scene\n" );
		return false;
	}
	packetFrame.FinishScene();

	rendererClusteredLightingStats_t stats;
	R_ModernClusteredLighting_BuildFrame( packetFrame, true, stats );
	if ( !stats.frameValid || stats.gridCount != 1 || stats.lightCount != 6 || stats.pointLights != 3 || stats.projectedLights != 3 || stats.clusterCount <= 0 || stats.activeClusters <= 0 || stats.maxLightsInCluster != MODERN_CLUSTER_MAX_LIGHTS_PER_CLUSTER || stats.overflowClusters <= 0 || stats.overflowReferences <= 0 ) {
		common->Printf(
			"RendererClusterGrid self-test failed: grid=%d valid=%d lights=%d point=%d projected=%d clusters=%d active=%d maxCluster=%d overflow=%d refs=%d\n",
			stats.gridCount,
			stats.frameValid ? 1 : 0,
			stats.lightCount,
			stats.pointLights,
			stats.projectedLights,
			stats.clusterCount,
			stats.activeClusters,
			stats.maxLightsInCluster,
			stats.overflowClusters,
			stats.overflowReferences );
		return false;
	}
	const int sliceA = R_ModernClusteredLighting_DepthSliceForZ( rg_clusteredLightingFrame.grids[0], 32.0f );
	const int sliceB = R_ModernClusteredLighting_DepthSliceForZ( rg_clusteredLightingFrame.grids[0], 512.0f );
	const int sliceC = R_ModernClusteredLighting_DepthSliceForZ( rg_clusteredLightingFrame.grids[0], 2048.0f );
	if ( sliceA > sliceB || sliceB > sliceC ) {
		common->Printf( "RendererClusterGrid self-test failed: depth slices not monotonic (%d %d %d)\n", sliceA, sliceB, sliceC );
		return false;
	}
	if ( rg_clusteredLightingAvailable && rg_clusteredLightingInitialized ) {
		if ( !R_ModernClusteredLighting_UploadBuffers( stats ) ) {
			common->Printf( "RendererClusterGrid self-test failed: UBO fallback upload unavailable\n" );
			return false;
		}
		if ( r_rendererClusterDebug.GetInteger() > 0 && !R_ModernClusteredLighting_UpdateDebugTexture( stats ) ) {
			common->Printf( "RendererClusterGrid self-test failed: debug texture update unavailable\n" );
			return false;
		}
	}

	common->Printf(
		"RendererClusterGrid self-test passed (grid=%dx%dx%d lights=%d point=%d projected=%d clusters=%d active=%d refs=%d overflow=%d/%d maxCluster=%d ubo=%d overlay=%d)\n",
		stats.tileCountX,
		stats.tileCountY,
		stats.sliceCountZ,
		stats.lightCount,
		stats.pointLights,
		stats.projectedLights,
		stats.clusterCount,
		stats.activeClusters,
		stats.lightReferences,
		stats.overflowClusters,
		stats.overflowReferences,
		stats.maxLightsInCluster,
		stats.uboFallbackReady ? 1 : 0,
		rg_clusteredLightingDebugProgram != 0 ? 1 : 0 );
	return true;
}
