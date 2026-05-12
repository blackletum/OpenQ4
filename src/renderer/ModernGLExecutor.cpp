// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ModernGLExecutor.h"
#include "ModernGLDrawPlan.h"
#include "ModernGLShaderLibrary.h"
#include "ModernGLSubmitPlan.h"
#include "RendererMetrics.h"

typedef struct modernGLFrameConstants_s {
	float	viewport[4];
	float	frame[4];
	float	capabilities[4];
	float	reserved[4];
} modernGLFrameConstants_t;

static modernGLExecutorStats_t rg_modernGLExecutorStats;
static idModernGLDrawPlan rg_modernGLDrawPlan;
static idModernGLSubmitPlan rg_modernGLSubmitPlan;
static renderBackendCaps_t rg_modernGLExecutorCaps;
static GLuint rg_modernGLExecutorVAO = 0;
static GLuint rg_modernGLExecutorFrameUBO = 0;
static bool rg_modernGLExecutorInitialized = false;
static bool rg_modernGLExecutorAvailable = false;
static unsigned int rg_modernGLExecutorCachedUniformBuffer = static_cast<unsigned int>( -1 );

static void R_ModernGLExecutor_SetStatus( modernGLExecutorStats_t &stats, const char *status ) {
	idStr::snPrintf( stats.status, sizeof( stats.status ), "%s", status ? status : "unknown" );
}

static void R_ModernGLExecutor_CopyDrawPlanStats( modernGLExecutorStats_t &stats, const modernGLDrawPlanStats_t &drawPlanStats ) {
	stats.drawPlanReady = drawPlanStats.available && drawPlanStats.valid;
	stats.drawPlanOverflow = drawPlanStats.overflow;
	stats.drawPlanDraws = drawPlanStats.plannedDraws;
	stats.drawPlanDepthDraws = drawPlanStats.depthDraws;
	stats.drawPlanMaterialDraws = drawPlanStats.materialDraws;
	stats.drawPlanFallbackDraws = drawPlanStats.fallbackDraws;
	stats.drawPlanIndexedDraws = drawPlanStats.indexedDraws;
	stats.drawPlanVertexOnlyDraws = drawPlanStats.vertexOnlyDraws;
	stats.drawPlanStateBatches = drawPlanStats.stateBatches;
	stats.drawPlanProgramSwitches = drawPlanStats.programSwitches;
	stats.drawPlanMaterialSwitches = drawPlanStats.materialSwitches;
	if ( drawPlanStats.highestGLSLVersion > stats.highestGLSLVersion ) {
		stats.highestGLSLVersion = drawPlanStats.highestGLSLVersion;
	}
}

static void R_ModernGLExecutor_CopySubmitPlanStats( modernGLExecutorStats_t &stats, const modernGLSubmitPlanStats_t &submitPlanStats ) {
	stats.submitPlanReady = submitPlanStats.available && submitPlanStats.valid;
	stats.submitPlanOverflow = submitPlanStats.overflow;
	stats.submitPlanDraws = submitPlanStats.readyDraws;
	stats.submitPlanFallbackDraws = submitPlanStats.fallbackDraws;
	stats.submitPlanDepthDraws = submitPlanStats.depthReadyDraws;
	stats.submitPlanMaterialDraws = submitPlanStats.materialReadyDraws;
	stats.submitPlanMissingAmbientDraws = submitPlanStats.missingAmbientCacheDraws;
	stats.submitPlanMissingIndexDraws = submitPlanStats.missingIndexCacheDraws;
	stats.submitPlanProgramBatches = submitPlanStats.programBatches;
	stats.submitPlanVertexBufferBatches = submitPlanStats.vertexBufferBatches;
	stats.submitPlanIndexBufferBatches = submitPlanStats.indexBufferBatches;
	stats.submitPlanScissorBatches = submitPlanStats.scissorBatches;
	stats.submitPlanMaterialBatches = submitPlanStats.materialBatches;
	stats.submitPlanUniformUpdates = submitPlanStats.uniformUpdates;
	stats.submitPlanFrameUBOBinds = submitPlanStats.frameUBOBinds;
	if ( submitPlanStats.highestGLSLVersion > stats.highestGLSLVersion ) {
		stats.highestGLSLVersion = submitPlanStats.highestGLSLVersion;
	}
}

static bool R_ModernGLExecutor_CanCreateObjects( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	if ( !features.modernBaseline ) {
		return false;
	}
	if ( !caps.hasVAO || !caps.hasUBO || !caps.hasVBO ) {
		return false;
	}
	if ( glGenVertexArrays == NULL || glBindVertexArray == NULL || glDeleteVertexArrays == NULL ) {
		return false;
	}
	if ( glGenBuffers == NULL || glBindBuffer == NULL || glBufferData == NULL || glBufferSubData == NULL || glDeleteBuffers == NULL ) {
		return false;
	}
	return true;
}

static void R_ModernGLExecutor_ResetStats( modernGLExecutorStats_t &stats, bool enabled ) {
	memset( &stats, 0, sizeof( stats ) );
	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	stats.available = rg_modernGLExecutorAvailable;
	stats.enabled = enabled;
	stats.initialized = rg_modernGLExecutorInitialized;
	stats.vaoReady = rg_modernGLExecutorVAO != 0;
	stats.frameUBOReady = rg_modernGLExecutorFrameUBO != 0;
	stats.shaderLibraryReady = shaderStats.available;
	stats.shaderProgramCount = shaderStats.programCount;
	stats.shaderFailureCount = shaderStats.failedProgramCount;
	stats.highestGLSLVersion = shaderStats.highestGLSLVersion;
	R_ModernGLExecutor_SetStatus( stats, enabled ? "unavailable" : "off" );
}

static bool R_ModernGLExecutor_IsWorldPass( renderPassCategory_t category ) {
	return category != RENDER_PASS_GUI && category != RENDER_PASS_PRESENT;
}

static void R_ModernGLExecutor_AnalyzeFrame(
	const idScenePacketFrame &packetFrame,
	const idRenderGraph &graph,
	bool enabled,
	bool available,
	bool initialized,
	bool vaoReady,
	bool frameUBOReady,
	modernGLExecutorStats_t &stats ) {
	memset( &stats, 0, sizeof( stats ) );
	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	stats.available = available;
	stats.enabled = enabled;
	stats.initialized = initialized;
	stats.vaoReady = vaoReady;
	stats.frameUBOReady = frameUBOReady;
	stats.shaderLibraryReady = shaderStats.available;
	stats.shaderProgramCount = shaderStats.programCount;
	stats.shaderFailureCount = shaderStats.failedProgramCount;
	stats.highestGLSLVersion = shaderStats.highestGLSLVersion;
	stats.graphPasses = graph.NumPasses();
	stats.drawPackets = packetFrame.NumDrawPackets();

	if ( !enabled ) {
		R_ModernGLExecutor_SetStatus( stats, "off" );
		return;
	}
	if ( !available ) {
		R_ModernGLExecutor_SetStatus( stats, "unavailable" );
		return;
	}
	if ( !initialized || !vaoReady || !frameUBOReady ) {
		R_ModernGLExecutor_SetStatus( stats, "not-initialized" );
		return;
	}
	if ( !stats.shaderLibraryReady ) {
		R_ModernGLExecutor_SetStatus( stats, "shader-library-unavailable" );
		return;
	}

	for ( int i = 0; i < graph.NumPasses(); ++i ) {
		const renderGraphPass_t &pass = graph.Pass( i );
		if ( pass.enabled && pass.packetBacked ) {
			stats.preparedPasses++;
		} else {
			stats.fallbackPasses++;
		}
	}

	for ( int i = 0; i < packetFrame.NumDrawPackets(); ++i ) {
		const drawPacket_t &draw = packetFrame.DrawPacket( i );
		if ( draw.hasGeometry ) {
			stats.geometryDrawPackets++;
		}
		if ( draw.materialRecord != NULL ) {
			stats.materialDrawPackets++;
		}
		if ( draw.materialRecordIndex >= 0 ) {
			stats.resourceDrawPackets++;
		}
		if ( draw.passCategory == RENDER_PASS_GUI ) {
			stats.guiDrawPackets++;
		}
		if ( R_ModernGLExecutor_IsWorldPass( draw.passCategory ) ) {
			stats.worldDrawPackets++;
		}
		if ( draw.hasGeometry && draw.materialRecordIndex >= 0 ) {
			stats.preparedDrawPackets++;
		}
	}

	stats.legacyFallback = true;
	R_ModernGLExecutor_SetStatus( stats, "prepared-legacy-fallback" );
}

static void R_ModernGLExecutor_BindUniformBuffer( GLuint buffer ) {
	if ( rg_modernGLExecutorCachedUniformBuffer == buffer ) {
		return;
	}
	glBindBuffer( GL_UNIFORM_BUFFER, buffer );
	rg_modernGLExecutorCachedUniformBuffer = buffer;
}

static void R_ModernGLExecutor_UpdateFrameUBO( const modernGLExecutorStats_t &stats ) {
	if ( !stats.enabled || !stats.available || !stats.initialized || !stats.frameUBOReady ) {
		return;
	}

	modernGLFrameConstants_t constants;
	memset( &constants, 0, sizeof( constants ) );
	constants.viewport[0] = static_cast<float>( glConfig.vidWidth );
	constants.viewport[1] = static_cast<float>( glConfig.vidHeight );
	constants.viewport[2] = glConfig.vidHeight > 0 ? static_cast<float>( glConfig.vidWidth ) / static_cast<float>( glConfig.vidHeight ) : 1.0f;
	constants.viewport[3] = 1.0f;
	constants.frame[0] = static_cast<float>( tr.frameCount );
	constants.frame[1] = static_cast<float>( stats.preparedPasses );
	constants.frame[2] = static_cast<float>( stats.submitPlanReady ? stats.submitPlanDraws : ( stats.drawPlanReady ? stats.drawPlanDraws : stats.preparedDrawPackets ) );
	constants.frame[3] = static_cast<float>( stats.submitPlanReady ? stats.submitPlanProgramBatches : ( stats.drawPlanReady ? stats.drawPlanStateBatches : stats.resourceDrawPackets ) );
	constants.capabilities[0] = static_cast<float>( rg_modernGLExecutorCaps.glMajor );
	constants.capabilities[1] = static_cast<float>( rg_modernGLExecutorCaps.glMinor );
	constants.capabilities[2] = rg_modernGLExecutorCaps.hasUBO ? 1.0f : 0.0f;
	constants.capabilities[3] = rg_modernGLExecutorCaps.hasVAO ? 1.0f : 0.0f;

	R_ModernGLExecutor_BindUniformBuffer( rg_modernGLExecutorFrameUBO );
	glBufferSubData( GL_UNIFORM_BUFFER, 0, sizeof( constants ), &constants );
	R_ModernGLExecutor_BindUniformBuffer( 0 );

	if ( glBindBufferBase != NULL ) {
		glBindBufferBase( GL_UNIFORM_BUFFER, 0, 0 );
	}
}

static void R_ModernGLExecutor_RecordMetrics( const modernGLExecutorStats_t &stats ) {
	rendererModernExecutorMetricsMode_t mode = RENDERER_MODERN_EXECUTOR_METRICS_OFF;
	if ( stats.enabled && !stats.available ) {
		mode = RENDERER_MODERN_EXECUTOR_METRICS_UNAVAILABLE;
	} else if ( stats.enabled && stats.legacyFallback ) {
		mode = RENDERER_MODERN_EXECUTOR_METRICS_LEGACY_FALLBACK;
	} else if ( stats.enabled ) {
		mode = RENDERER_MODERN_EXECUTOR_METRICS_PREPARED;
	}

	R_RendererMetrics_RecordModernExecutor(
		mode,
		stats.graphPasses,
		stats.preparedPasses,
		stats.fallbackPasses,
		stats.preparedDrawPackets,
		stats.materialDrawPackets,
		stats.resourceDrawPackets,
		stats.geometryDrawPackets,
		stats.vaoReady,
		stats.frameUBOReady,
		stats.shaderLibraryReady,
		stats.shaderProgramCount,
		stats.shaderFailureCount,
		stats.drawPlanReady,
		stats.drawPlanOverflow,
		stats.drawPlanDraws,
		stats.drawPlanDepthDraws,
		stats.drawPlanMaterialDraws,
		stats.drawPlanFallbackDraws,
		stats.drawPlanStateBatches,
		stats.drawPlanProgramSwitches,
		stats.drawPlanMaterialSwitches,
		stats.submitPlanReady,
		stats.submitPlanOverflow,
		stats.submitPlanDraws,
		stats.submitPlanFallbackDraws,
		stats.submitPlanDepthDraws,
		stats.submitPlanMaterialDraws,
		stats.submitPlanMissingAmbientDraws,
		stats.submitPlanMissingIndexDraws,
		stats.submitPlanProgramBatches,
		stats.submitPlanVertexBufferBatches,
		stats.submitPlanIndexBufferBatches,
		stats.submitPlanScissorBatches,
		stats.submitPlanMaterialBatches,
		stats.submitPlanUniformUpdates,
		stats.submitPlanFrameUBOBinds );
}

void R_ModernGLExecutor_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	R_ModernGLExecutor_Shutdown();
	rg_modernGLExecutorCaps = caps;
	R_ModernGLExecutor_ResetStats( rg_modernGLExecutorStats, r_rendererModernExecutor.GetBool() );

	if ( !R_ModernGLExecutor_CanCreateObjects( caps, features ) ) {
		rg_modernGLExecutorAvailable = false;
		R_ModernGLExecutor_SetStatus( rg_modernGLExecutorStats, "unavailable" );
		return;
	}

	glGenVertexArrays( 1, &rg_modernGLExecutorVAO );
	glGenBuffers( 1, &rg_modernGLExecutorFrameUBO );
	if ( rg_modernGLExecutorVAO == 0 || rg_modernGLExecutorFrameUBO == 0 ) {
		R_ModernGLExecutor_Shutdown();
		R_ModernGLExecutor_SetStatus( rg_modernGLExecutorStats, "object-create-failed" );
		return;
	}

	modernGLFrameConstants_t constants;
	memset( &constants, 0, sizeof( constants ) );
	glBindVertexArray( rg_modernGLExecutorVAO );
	glBindBuffer( GL_UNIFORM_BUFFER, rg_modernGLExecutorFrameUBO );
	glBufferData( GL_UNIFORM_BUFFER, sizeof( constants ), &constants, GL_DYNAMIC_DRAW );
	glBindBuffer( GL_UNIFORM_BUFFER, 0 );
	glBindVertexArray( 0 );
	rg_modernGLExecutorCachedUniformBuffer = 0;

	R_ModernGLShaderLibrary_Init( caps, features );
	if ( !R_ModernGLShaderLibrary_Stats().available ) {
		R_ModernGLExecutor_Shutdown();
		R_ModernGLExecutor_SetStatus( rg_modernGLExecutorStats, "shader-library-unavailable" );
		return;
	}

	rg_modernGLExecutorInitialized = true;
	rg_modernGLExecutorAvailable = true;
	R_ModernGLExecutor_ResetStats( rg_modernGLExecutorStats, r_rendererModernExecutor.GetBool() );
	R_ModernGLExecutor_SetStatus( rg_modernGLExecutorStats, "available" );
}

void R_ModernGLExecutor_Shutdown( void ) {
	rg_modernGLDrawPlan.Clear();
	rg_modernGLSubmitPlan.Clear();
	if ( rg_modernGLExecutorFrameUBO != 0 && glDeleteBuffers != NULL ) {
		glDeleteBuffers( 1, &rg_modernGLExecutorFrameUBO );
	}
	if ( rg_modernGLExecutorVAO != 0 && glDeleteVertexArrays != NULL ) {
		glDeleteVertexArrays( 1, &rg_modernGLExecutorVAO );
	}
	R_ModernGLShaderLibrary_Shutdown();

	rg_modernGLExecutorVAO = 0;
	rg_modernGLExecutorFrameUBO = 0;
	rg_modernGLExecutorInitialized = false;
	rg_modernGLExecutorAvailable = false;
	rg_modernGLExecutorCachedUniformBuffer = static_cast<unsigned int>( -1 );
	memset( &rg_modernGLExecutorCaps, 0, sizeof( rg_modernGLExecutorCaps ) );
	R_ModernGLExecutor_ResetStats( rg_modernGLExecutorStats, false );
}

void R_ModernGLExecutor_PrepareFrame( const idScenePacketFrame &packetFrame, const idRenderGraph &graph ) {
	const bool enabled = r_rendererModernExecutor.GetBool();
	R_ModernGLExecutor_AnalyzeFrame(
		packetFrame,
		graph,
		enabled,
		rg_modernGLExecutorAvailable,
		rg_modernGLExecutorInitialized,
		rg_modernGLExecutorVAO != 0,
		rg_modernGLExecutorFrameUBO != 0,
		rg_modernGLExecutorStats );

	if ( rg_modernGLExecutorStats.enabled
		&& rg_modernGLExecutorStats.available
		&& rg_modernGLExecutorStats.initialized
		&& rg_modernGLExecutorStats.vaoReady
		&& rg_modernGLExecutorStats.frameUBOReady
		&& rg_modernGLExecutorStats.shaderLibraryReady ) {
		rg_modernGLDrawPlan.Build( packetFrame, graph );
		R_ModernGLExecutor_CopyDrawPlanStats( rg_modernGLExecutorStats, rg_modernGLDrawPlan.Stats() );
		if ( rg_modernGLExecutorStats.drawPlanReady ) {
			rg_modernGLSubmitPlan.Build( rg_modernGLDrawPlan );
			R_ModernGLExecutor_CopySubmitPlanStats( rg_modernGLExecutorStats, rg_modernGLSubmitPlan.Stats() );
		} else {
			rg_modernGLSubmitPlan.Clear();
		}
	} else {
		rg_modernGLDrawPlan.Clear();
		rg_modernGLSubmitPlan.Clear();
	}

	R_ModernGLExecutor_UpdateFrameUBO( rg_modernGLExecutorStats );
	R_ModernGLExecutor_RecordMetrics( rg_modernGLExecutorStats );

	if ( r_rendererMetrics.GetInteger() >= 2 && enabled ) {
		common->Printf(
			"modernGLExecutor status=%s passes=%d/%d fallback=%d draws=%d prepared=%d material=%d resources=%d geometry=%d gui=%d world=%d plan=%d planDraws=%d depth=%d flat=%d planFallback=%d batches=%d programSwitches=%d materialSwitches=%d planOverflow=%d submit=%d submitDraws=%d submitFallback=%d submitMissing(vbo=%d ibo=%d) submitBatches(program=%d vbo=%d ibo=%d scissor=%d material=%d) uniforms=%d frameUBO=%d submitOverflow=%d vao=%d ubo=%d shaders=%d shaderFails=%d glsl=%d\n",
			rg_modernGLExecutorStats.status,
			rg_modernGLExecutorStats.preparedPasses,
			rg_modernGLExecutorStats.graphPasses,
			rg_modernGLExecutorStats.fallbackPasses,
			rg_modernGLExecutorStats.drawPackets,
			rg_modernGLExecutorStats.preparedDrawPackets,
			rg_modernGLExecutorStats.materialDrawPackets,
			rg_modernGLExecutorStats.resourceDrawPackets,
			rg_modernGLExecutorStats.geometryDrawPackets,
			rg_modernGLExecutorStats.guiDrawPackets,
			rg_modernGLExecutorStats.worldDrawPackets,
			rg_modernGLExecutorStats.drawPlanReady ? 1 : 0,
			rg_modernGLExecutorStats.drawPlanDraws,
			rg_modernGLExecutorStats.drawPlanDepthDraws,
			rg_modernGLExecutorStats.drawPlanMaterialDraws,
			rg_modernGLExecutorStats.drawPlanFallbackDraws,
			rg_modernGLExecutorStats.drawPlanStateBatches,
			rg_modernGLExecutorStats.drawPlanProgramSwitches,
			rg_modernGLExecutorStats.drawPlanMaterialSwitches,
			rg_modernGLExecutorStats.drawPlanOverflow ? 1 : 0,
			rg_modernGLExecutorStats.submitPlanReady ? 1 : 0,
			rg_modernGLExecutorStats.submitPlanDraws,
			rg_modernGLExecutorStats.submitPlanFallbackDraws,
			rg_modernGLExecutorStats.submitPlanMissingAmbientDraws,
			rg_modernGLExecutorStats.submitPlanMissingIndexDraws,
			rg_modernGLExecutorStats.submitPlanProgramBatches,
			rg_modernGLExecutorStats.submitPlanVertexBufferBatches,
			rg_modernGLExecutorStats.submitPlanIndexBufferBatches,
			rg_modernGLExecutorStats.submitPlanScissorBatches,
			rg_modernGLExecutorStats.submitPlanMaterialBatches,
			rg_modernGLExecutorStats.submitPlanUniformUpdates,
			rg_modernGLExecutorStats.submitPlanFrameUBOBinds,
			rg_modernGLExecutorStats.submitPlanOverflow ? 1 : 0,
			rg_modernGLExecutorStats.vaoReady ? 1 : 0,
			rg_modernGLExecutorStats.frameUBOReady ? 1 : 0,
			rg_modernGLExecutorStats.shaderProgramCount,
			rg_modernGLExecutorStats.shaderFailureCount,
			rg_modernGLExecutorStats.highestGLSLVersion );
	}
}

const modernGLExecutorStats_t &R_ModernGLExecutor_Stats( void ) {
	return rg_modernGLExecutorStats;
}

void R_ModernGLExecutor_PrintGfxInfo( void ) {
	common->Printf(
		"Modern GL executor: %s, cvar=%d, VAO=%d, frameUBO=%d, shaderLibrary=%d, shaderPrograms=%d, highestGLSL=%d, drawPlan=%d, planDraws=%d, depth=%d, flat=%d, planFallback=%d, batches=%d, submitPlan=%d, submitDraws=%d, submitFallback=%d, missingVBO=%d, missingIBO=%d, submitBatches(program=%d vbo=%d ibo=%d), legacyFallback=%d\n",
		rg_modernGLExecutorStats.available ? "available" : "unavailable",
		r_rendererModernExecutor.GetBool() ? 1 : 0,
		rg_modernGLExecutorStats.vaoReady ? 1 : 0,
		rg_modernGLExecutorStats.frameUBOReady ? 1 : 0,
		rg_modernGLExecutorStats.shaderLibraryReady ? 1 : 0,
		rg_modernGLExecutorStats.shaderProgramCount,
		rg_modernGLExecutorStats.highestGLSLVersion,
		rg_modernGLExecutorStats.drawPlanReady ? 1 : 0,
		rg_modernGLExecutorStats.drawPlanDraws,
		rg_modernGLExecutorStats.drawPlanDepthDraws,
		rg_modernGLExecutorStats.drawPlanMaterialDraws,
		rg_modernGLExecutorStats.drawPlanFallbackDraws,
		rg_modernGLExecutorStats.drawPlanStateBatches,
		rg_modernGLExecutorStats.submitPlanReady ? 1 : 0,
		rg_modernGLExecutorStats.submitPlanDraws,
		rg_modernGLExecutorStats.submitPlanFallbackDraws,
		rg_modernGLExecutorStats.submitPlanMissingAmbientDraws,
		rg_modernGLExecutorStats.submitPlanMissingIndexDraws,
		rg_modernGLExecutorStats.submitPlanProgramBatches,
		rg_modernGLExecutorStats.submitPlanVertexBufferBatches,
		rg_modernGLExecutorStats.submitPlanIndexBufferBatches,
		rg_modernGLExecutorStats.legacyFallback ? 1 : 0 );
	R_ModernGLShaderLibrary_PrintGfxInfo();
}

bool RendererModernGLExecutor_RunSelfTest( void ) {
	drawSurf_t drawSurfs[2];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
	srfTriangles_t geometry;
	memset( &geometry, 0, sizeof( geometry ) );
	geometry.numVerts = 3;
	geometry.numIndexes = 6;
	for ( int i = 0; i < 2; ++i ) {
		drawSurfs[i].geo = &geometry;
		if ( tr.defaultMaterial != NULL ) {
			drawSurfs[i].material = tr.defaultMaterial;
			drawSurfs[i].sort = tr.defaultMaterial->GetSort();
		}
	}

	drawSurf_t *drawSurfPtrs[2] = { &drawSurfs[0], &drawSurfs[1] };
	viewEntity_t viewEntity;
	memset( &viewEntity, 0, sizeof( viewEntity ) );
	viewDef_t worldView;
	memset( &worldView, 0, sizeof( worldView ) );
	worldView.viewEntitys = &viewEntity;
	worldView.drawSurfs = drawSurfPtrs;
	worldView.numDrawSurfs = 2;

	drawSurfsCommand_t drawCmd;
	memset( &drawCmd, 0, sizeof( drawCmd ) );
	drawCmd.commandId = RC_DRAW_VIEW;
	drawCmd.viewDef = &worldView;
	emptyCommand_t swapCmd;
	memset( &swapCmd, 0, sizeof( swapCmd ) );
	swapCmd.commandId = RC_SWAP_BUFFERS;
	drawCmd.next = &swapCmd.commandId;
	swapCmd.next = NULL;

	idScenePacketFrame packetFrame;
	R_ScenePackets_BuildLegacyCommandStream( reinterpret_cast<const emptyCommand_t *>( &drawCmd ), packetFrame );
	idRenderGraph graph;
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );

	modernGLExecutorStats_t stats;
	R_ModernGLExecutor_AnalyzeFrame(
		packetFrame,
		graph,
		true,
		true,
		true,
		true,
		true,
		stats );

	idModernGLDrawPlan drawPlan;
	drawPlan.Build( packetFrame, graph );
	R_ModernGLExecutor_CopyDrawPlanStats( stats, drawPlan.Stats() );
	idModernGLSubmitPlan submitPlan;
	submitPlan.Build( drawPlan );
	R_ModernGLExecutor_CopySubmitPlanStats( stats, submitPlan.Stats() );

	if ( !stats.legacyFallback || stats.preparedPasses != graph.NumPasses() || stats.fallbackPasses != 0 ) {
		common->Printf( "RendererModernGLExecutor self-test failed: pass preparation mismatch\n" );
		return false;
	}
	const int expectedResourceDraws = tr.defaultMaterial != NULL ? packetFrame.NumDrawPackets() : 0;
	if ( stats.drawPackets != packetFrame.NumDrawPackets() || stats.preparedDrawPackets != expectedResourceDraws ) {
		common->Printf( "RendererModernGLExecutor self-test failed: draw preparation mismatch\n" );
		return false;
	}
	if ( stats.materialDrawPackets != expectedResourceDraws || stats.resourceDrawPackets != expectedResourceDraws || stats.geometryDrawPackets != packetFrame.NumDrawPackets() ) {
		common->Printf( "RendererModernGLExecutor self-test failed: material/resource/geometry coverage mismatch\n" );
		return false;
	}
	if ( !rg_modernGLExecutorAvailable ) {
		common->Printf( "RendererModernGLExecutor self-test passed (analysis only; live GL3 VAO/UBO objects unavailable)\n" );
		return true;
	}
	if ( rg_modernGLExecutorVAO == 0 || rg_modernGLExecutorFrameUBO == 0 ) {
		common->Printf( "RendererModernGLExecutor self-test failed: live GL object state mismatch\n" );
		return false;
	}
	if ( !RendererModernGLShaderLibrary_RunSelfTest() ) {
		return false;
	}
	if ( !RendererModernGLDrawPlan_RunSelfTest() ) {
		return false;
	}
	if ( !RendererModernGLSubmitPlan_RunSelfTest() ) {
		return false;
	}
	if ( tr.defaultMaterial != NULL && ( !stats.drawPlanReady || stats.drawPlanDraws <= 0 || stats.drawPlanStateBatches <= 0 ) ) {
		common->Printf( "RendererModernGLExecutor self-test failed: draw-plan readiness mismatch\n" );
		return false;
	}

	common->Printf( "RendererModernGLExecutor self-test passed\n" );
	return true;
}
