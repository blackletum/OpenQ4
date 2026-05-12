// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ModernGLSubmitPlan.h"

idModernGLSubmitPlan::idModernGLSubmitPlan()
	: numCommands( 0 ) {
	memset( &stats, 0, sizeof( stats ) );
}

void idModernGLSubmitPlan::Clear( void ) {
	memset( commands, 0, sizeof( commands ) );
	memset( &stats, 0, sizeof( stats ) );
	idStr::snPrintf( stats.status, sizeof( stats.status ), "%s", "empty" );
	numCommands = 0;
}

static void R_ModernGLSubmitPlan_SetStatus( modernGLSubmitPlanStats_t &stats, const char *status ) {
	idStr::snPrintf( stats.status, sizeof( stats.status ), "%s", status ? status : "unknown" );
}

static bool R_ModernGLSubmitPlan_ScissorEquals( const modernGLSubmitCommand_t &a, const modernGLSubmitCommand_t &b ) {
	return a.scissorX1 == b.scissorX1
		&& a.scissorY1 == b.scissorY1
		&& a.scissorX2 == b.scissorX2
		&& a.scissorY2 == b.scissorY2;
}

static bool R_ModernGLSubmitPlan_HasVertexBuffer( const vertCache_t *cache ) {
	return cache != NULL && cache->vbo != 0 && !cache->indexBuffer;
}

static bool R_ModernGLSubmitPlan_HasIndexBuffer( const vertCache_t *cache ) {
	return cache != NULL && cache->vbo != 0 && cache->indexBuffer;
}

static bool R_ModernGLSubmitPlan_EntryNeedsIndexBuffer( const modernGLDrawPlanEntry_t &entry ) {
	return entry.indexed && entry.indexCount > 0;
}

bool idModernGLSubmitPlan::AddCommand( const modernGLDrawPlanEntry_t &entry ) {
	if ( numCommands >= MODERN_GL_SUBMIT_PLAN_MAX_COMMANDS ) {
		stats.overflow = true;
		stats.fallbackDraws++;
		return false;
	}

	const drawPacket_t *draw = entry.drawPacket;
	if ( draw == NULL ) {
		stats.missingDrawPacketDraws++;
		stats.fallbackDraws++;
		return false;
	}

	const drawSurf_t *surf = draw->legacyDrawSurf;
	const srfTriangles_t *geo = surf != NULL ? surf->geo : NULL;
	if ( geo == NULL || !draw->hasGeometry ) {
		stats.missingGeometryDraws++;
		stats.fallbackDraws++;
		return false;
	}

	bool submitReady = true;
	if ( !R_ModernGLSubmitPlan_HasVertexBuffer( geo->ambientCache ) ) {
		stats.missingAmbientCacheDraws++;
		if ( geo->ambientCache != NULL && geo->ambientCache->vbo == 0 ) {
			stats.clientVertexFallbackDraws++;
		}
		submitReady = false;
	}
	if ( R_ModernGLSubmitPlan_EntryNeedsIndexBuffer( entry ) && !R_ModernGLSubmitPlan_HasIndexBuffer( geo->indexCache ) ) {
		stats.missingIndexCacheDraws++;
		if ( geo->indexCache != NULL && geo->indexCache->vbo == 0 ) {
			stats.clientVertexFallbackDraws++;
		}
		submitReady = false;
	}
	if ( !submitReady ) {
		stats.fallbackDraws++;
		return false;
	}

	modernGLSubmitCommand_t &command = commands[numCommands];
	memset( &command, 0, sizeof( command ) );
	command.drawPlanEntry = &entry;
	command.passCategory = entry.passCategory;
	command.pipeline = entry.pipeline;
	command.program = entry.program;
	command.vertexBuffer = geo->ambientCache->vbo;
	command.indexBuffer = geo->indexCache != NULL ? geo->indexCache->vbo : 0;
	command.ambientCacheOffset = geo->ambientCache->offset;
	command.indexCacheOffset = geo->indexCache != NULL ? geo->indexCache->offset : 0;
	command.vertexStride = sizeof( idDrawVert );
	command.indexType = GL_INDEX_TYPE;
	command.indexCount = entry.indexCount;
	command.vertexCount = entry.vertexCount;
	command.materialRecordIndex = entry.materialRecordIndex;
	command.scissorX1 = draw->scissorX1;
	command.scissorY1 = draw->scissorY1;
	command.scissorX2 = draw->scissorX2;
	command.scissorY2 = draw->scissorY2;
	command.indexed = entry.indexed;

	const bool havePrevious = numCommands > 0;
	if ( !havePrevious || commands[numCommands - 1].program != command.program ) {
		stats.programBatches++;
	}
	if ( !havePrevious || commands[numCommands - 1].vertexBuffer != command.vertexBuffer ) {
		stats.vertexBufferBatches++;
	}
	if ( command.indexed && ( !havePrevious || commands[numCommands - 1].indexBuffer != command.indexBuffer ) ) {
		stats.indexBufferBatches++;
	}
	if ( !havePrevious || !R_ModernGLSubmitPlan_ScissorEquals( commands[numCommands - 1], command ) ) {
		stats.scissorBatches++;
	}
	if ( !havePrevious || commands[numCommands - 1].materialRecordIndex != command.materialRecordIndex ) {
		stats.materialBatches++;
	}

	numCommands++;
	stats.readyDraws++;
	stats.uniformUpdates++;
	stats.frameUBOBinds = 1;
	if ( command.pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_DEPTH ) {
		stats.depthReadyDraws++;
	} else if ( command.pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FLAT_MATERIAL ) {
		stats.materialReadyDraws++;
	}
	if ( command.indexed ) {
		stats.indexedReadyDraws++;
	} else {
		stats.vertexOnlyReadyDraws++;
	}
	if ( entry.glslVersion > stats.highestGLSLVersion ) {
		stats.highestGLSLVersion = entry.glslVersion;
	}
	return true;
}

bool idModernGLSubmitPlan::Build( const idModernGLDrawPlan &drawPlan ) {
	Clear();
	const modernGLDrawPlanStats_t &drawStats = drawPlan.Stats();
	stats.sourcePlanDraws = drawPlan.NumEntries();

	if ( !drawStats.available || !drawStats.valid ) {
		R_ModernGLSubmitPlan_SetStatus( stats, "draw-plan-unavailable" );
		stats.fallbackDraws = drawStats.plannedDraws;
		return false;
	}

	stats.available = true;
	for ( int i = 0; i < drawPlan.NumEntries(); ++i ) {
		AddCommand( drawPlan.Entry( i ) );
	}

	stats.valid = stats.available && !stats.overflow;
	if ( stats.overflow ) {
		R_ModernGLSubmitPlan_SetStatus( stats, "overflow" );
	} else if ( stats.readyDraws == 0 && stats.sourcePlanDraws > 0 ) {
		R_ModernGLSubmitPlan_SetStatus( stats, "all-fallback" );
	} else if ( stats.fallbackDraws > 0 ) {
		R_ModernGLSubmitPlan_SetStatus( stats, "ready-with-fallbacks" );
	} else {
		R_ModernGLSubmitPlan_SetStatus( stats, "ready" );
	}
	return stats.valid;
}

int idModernGLSubmitPlan::NumCommands( void ) const {
	return numCommands;
}

const modernGLSubmitCommand_t &idModernGLSubmitPlan::Command( int index ) const {
	return commands[index];
}

const modernGLSubmitPlanStats_t &idModernGLSubmitPlan::Stats( void ) const {
	return stats;
}

static void R_ModernGLSubmitPlan_InitCache( vertCache_t &cache, unsigned int vbo, int offset, int size, bool indexBuffer ) {
	memset( &cache, 0, sizeof( cache ) );
	cache.vbo = vbo;
	cache.offset = offset;
	cache.size = size;
	cache.indexBuffer = indexBuffer;
	cache.tag = TAG_USED;
}

static bool R_ModernGLSubmitPlan_BuildSelfTestDrawPlan( bool cacheReady, idModernGLDrawPlan &drawPlan, idScenePacketFrame &packetFrame, idRenderGraph &graph ) {
	static vertCache_t ambientCache;
	static vertCache_t indexCache;
	static srfTriangles_t geometry;
	static drawSurf_t drawSurfs[2];

	memset( &geometry, 0, sizeof( geometry ) );
	geometry.numVerts = 3;
	geometry.numIndexes = 6;
	if ( cacheReady ) {
		R_ModernGLSubmitPlan_InitCache( ambientCache, 101, 64, geometry.numVerts * static_cast<int>( sizeof( idDrawVert ) ), false );
		R_ModernGLSubmitPlan_InitCache( indexCache, 202, 128, geometry.numIndexes * static_cast<int>( sizeof( glIndex_t ) ), true );
		geometry.ambientCache = &ambientCache;
		geometry.indexCache = &indexCache;
	}

	memset( drawSurfs, 0, sizeof( drawSurfs ) );
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

	R_ScenePackets_BuildLegacyCommandStream( reinterpret_cast<const emptyCommand_t *>( &drawCmd ), packetFrame );
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	return drawPlan.Build( packetFrame, graph );
}

bool RendererModernGLSubmitPlan_RunSelfTest( void ) {
	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	if ( !shaderStats.available ) {
		common->Printf( "RendererModernGLSubmitPlan self-test passed (shader library unavailable)\n" );
		return true;
	}

	idScenePacketFrame packetFrame;
	idRenderGraph graph;
	idModernGLDrawPlan drawPlan;
	R_ModernGLSubmitPlan_BuildSelfTestDrawPlan( true, drawPlan, packetFrame, graph );

	idModernGLSubmitPlan submitPlan;
	submitPlan.Build( drawPlan );
	const modernGLSubmitPlanStats_t &readyStats = submitPlan.Stats();
	const int expectedReadyDraws = tr.defaultMaterial != NULL ? 10 : 0;
	if ( readyStats.sourcePlanDraws != expectedReadyDraws || readyStats.readyDraws != expectedReadyDraws || readyStats.fallbackDraws != 0 || readyStats.overflow ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: ready draw count mismatch\n" );
		return false;
	}
	if ( expectedReadyDraws > 0 ) {
		if ( readyStats.depthReadyDraws != 2
			|| readyStats.materialReadyDraws != 8
			|| readyStats.programBatches != 2
			|| readyStats.vertexBufferBatches != 1
			|| readyStats.indexBufferBatches != 1
			|| readyStats.scissorBatches != 1
			|| readyStats.materialBatches != 1
			|| readyStats.uniformUpdates != expectedReadyDraws
			|| readyStats.frameUBOBinds != 1
			|| submitPlan.NumCommands() != expectedReadyDraws ) {
			common->Printf( "RendererModernGLSubmitPlan self-test failed: ready state-batch mismatch\n" );
			return false;
		}
	}

	idModernGLDrawPlan missingCacheDrawPlan;
	idScenePacketFrame missingCachePacketFrame;
	idRenderGraph missingCacheGraph;
	R_ModernGLSubmitPlan_BuildSelfTestDrawPlan( false, missingCacheDrawPlan, missingCachePacketFrame, missingCacheGraph );
	idModernGLSubmitPlan missingCacheSubmitPlan;
	missingCacheSubmitPlan.Build( missingCacheDrawPlan );
	const modernGLSubmitPlanStats_t &fallbackStats = missingCacheSubmitPlan.Stats();
	if ( fallbackStats.sourcePlanDraws != expectedReadyDraws || fallbackStats.readyDraws != 0 || fallbackStats.fallbackDraws != expectedReadyDraws ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: fallback draw count mismatch\n" );
		return false;
	}
	if ( expectedReadyDraws > 0 && ( fallbackStats.missingAmbientCacheDraws != expectedReadyDraws || fallbackStats.missingIndexCacheDraws != expectedReadyDraws ) ) {
		common->Printf( "RendererModernGLSubmitPlan self-test failed: missing-cache reason mismatch\n" );
		return false;
	}

	common->Printf(
		"RendererModernGLSubmitPlan self-test passed (ready=%d fallback=%d programBatches=%d vertexBatches=%d indexBatches=%d)\n",
		readyStats.readyDraws,
		fallbackStats.fallbackDraws,
		readyStats.programBatches,
		readyStats.vertexBufferBatches,
		readyStats.indexBufferBatches );
	return true;
}
