// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ModernGLDrawPlan.h"

idModernGLDrawPlan::idModernGLDrawPlan()
	: numEntries( 0 ) {
	memset( &stats, 0, sizeof( stats ) );
}

void idModernGLDrawPlan::Clear( void ) {
	memset( entries, 0, sizeof( entries ) );
	memset( &stats, 0, sizeof( stats ) );
	idStr::snPrintf( stats.status, sizeof( stats.status ), "%s", "empty" );
	numEntries = 0;
}

const char *ModernGLDrawPlanPipeline_Name( modernGLDrawPlanPipeline_t pipeline ) {
	switch ( pipeline ) {
	case MODERN_GL_DRAW_PLAN_PIPELINE_DEPTH:
		return "depth";
	case MODERN_GL_DRAW_PLAN_PIPELINE_FLAT_MATERIAL:
		return "flatMaterial";
	case MODERN_GL_DRAW_PLAN_PIPELINE_NONE:
	default:
		return "none";
	}
}

static void R_ModernGLDrawPlan_SetStatus( modernGLDrawPlanStats_t &stats, const char *status ) {
	idStr::snPrintf( stats.status, sizeof( stats.status ), "%s", status ? status : "unknown" );
}

static bool R_ModernGLDrawPlan_CategoryPipeline( renderPassCategory_t category, modernGLDrawPlanPipeline_t &pipeline, modernGLShaderProgramKind_t &shaderKind ) {
	switch ( category ) {
	case RENDER_PASS_DEPTH:
	case RENDER_PASS_STENCIL_SHADOW:
	case RENDER_PASS_SHADOW_MAP:
		pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_DEPTH;
		shaderKind = MODERN_GL_SHADER_DEPTH;
		return true;
	case RENDER_PASS_ARB2_INTERACTION:
	case RENDER_PASS_LIGHT_GRID:
	case RENDER_PASS_AMBIENT:
	case RENDER_PASS_FOG_BLEND:
		pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_FLAT_MATERIAL;
		shaderKind = MODERN_GL_SHADER_FLAT_MATERIAL;
		return true;
	default:
		pipeline = MODERN_GL_DRAW_PLAN_PIPELINE_NONE;
		shaderKind = MODERN_GL_SHADER_DEPTH;
		return false;
	}
}

static bool R_ModernGLDrawPlan_HasGraphPass( const idRenderGraph &graph, renderPassCategory_t category ) {
	for ( int i = 0; i < graph.NumPasses(); ++i ) {
		const renderGraphPass_t &pass = graph.Pass( i );
		if ( pass.category == category && pass.enabled && pass.packetBacked ) {
			return true;
		}
	}
	return false;
}

bool idModernGLDrawPlan::AddEntry( const drawPacket_t &draw, int drawPacketIndex, modernGLDrawPlanPipeline_t pipeline, const modernGLShaderProgramInfo_t &program ) {
	if ( numEntries >= MODERN_GL_DRAW_PLAN_MAX_ENTRIES ) {
		stats.overflow = true;
		stats.fallbackDraws++;
		return false;
	}

	modernGLDrawPlanEntry_t &entry = entries[numEntries++];
	memset( &entry, 0, sizeof( entry ) );
	entry.drawPacket = &draw;
	entry.passCategory = draw.passCategory;
	entry.pipeline = pipeline;
	entry.shaderKind = program.kind;
	entry.program = program.program;
	entry.drawPacketIndex = drawPacketIndex;
	entry.materialRecordIndex = draw.materialRecordIndex;
	entry.glslVersion = program.glslVersion;
	entry.indexCount = draw.indexCount;
	entry.vertexCount = draw.vertexCount;
	entry.indexed = draw.hasIndexCache || draw.indexCount > 0;

	stats.plannedDraws++;
	if ( pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_DEPTH ) {
		stats.depthDraws++;
	} else if ( pipeline == MODERN_GL_DRAW_PLAN_PIPELINE_FLAT_MATERIAL ) {
		stats.materialDraws++;
	}
	if ( entry.indexed ) {
		stats.indexedDraws++;
	} else {
		stats.vertexOnlyDraws++;
	}
	if ( entry.glslVersion > stats.highestGLSLVersion ) {
		stats.highestGLSLVersion = entry.glslVersion;
	}
	return true;
}

bool idModernGLDrawPlan::Build( const idScenePacketFrame &packetFrame, const idRenderGraph &graph ) {
	Clear();
	stats.sourceDrawPackets = packetFrame.NumDrawPackets();

	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	if ( !shaderStats.available ) {
		R_ModernGLDrawPlan_SetStatus( stats, "shader-library-unavailable" );
		stats.fallbackDraws = stats.sourceDrawPackets;
		return false;
	}

	modernGLDrawPlanPipeline_t previousPipeline = MODERN_GL_DRAW_PLAN_PIPELINE_NONE;
	renderPassCategory_t previousPass = RENDER_PASS_DEPTH;
	int previousMaterial = -2;
	unsigned int previousProgram = 0;
	bool havePrevious = false;

	stats.available = true;
	for ( int i = 0; i < packetFrame.NumDrawPackets(); ++i ) {
		const drawPacket_t &draw = packetFrame.DrawPacket( i );
		modernGLDrawPlanPipeline_t pipeline;
		modernGLShaderProgramKind_t shaderKind;
		if ( !R_ModernGLDrawPlan_CategoryPipeline( draw.passCategory, pipeline, shaderKind ) ) {
			stats.fallbackDraws++;
			continue;
		}
		if ( !R_ModernGLDrawPlan_HasGraphPass( graph, draw.passCategory ) ) {
			stats.fallbackDraws++;
			continue;
		}
		if ( !draw.hasGeometry || draw.materialRecordIndex < 0 ) {
			stats.fallbackDraws++;
			continue;
		}

		const modernGLShaderProgramInfo_t *program = R_ModernGLShaderLibrary_FindProgram( shaderKind, shaderStats.highestGLSLVersion );
		if ( program == NULL || program->program == 0 || !program->linked ) {
			stats.fallbackDraws++;
			continue;
		}

		if ( AddEntry( draw, i, pipeline, *program ) ) {
			if ( !havePrevious
				|| previousPipeline != pipeline
				|| previousPass != draw.passCategory
				|| previousProgram != program->program
				|| previousMaterial != draw.materialRecordIndex ) {
				stats.stateBatches++;
				if ( havePrevious && previousProgram != program->program ) {
					stats.programSwitches++;
				}
				if ( havePrevious && previousMaterial != draw.materialRecordIndex ) {
					stats.materialSwitches++;
				}
			}
			previousPipeline = pipeline;
			previousPass = draw.passCategory;
			previousProgram = program->program;
			previousMaterial = draw.materialRecordIndex;
			havePrevious = true;
		}
	}

	stats.valid = stats.available && !stats.overflow;
	R_ModernGLDrawPlan_SetStatus( stats, stats.valid ? "planned" : "overflow" );
	return stats.valid;
}

int idModernGLDrawPlan::NumEntries( void ) const {
	return numEntries;
}

const modernGLDrawPlanEntry_t &idModernGLDrawPlan::Entry( int index ) const {
	return entries[index];
}

const modernGLDrawPlanStats_t &idModernGLDrawPlan::Stats( void ) const {
	return stats;
}

bool RendererModernGLDrawPlan_RunSelfTest( void ) {
	const modernGLShaderLibraryStats_t &shaderStats = R_ModernGLShaderLibrary_Stats();
	if ( !shaderStats.available ) {
		common->Printf( "RendererModernGLDrawPlan self-test passed (shader library unavailable)\n" );
		return true;
	}

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
	drawSurfsCommand_t fxCmd;
	memset( &fxCmd, 0, sizeof( fxCmd ) );
	fxCmd.commandId = RC_DRAW_SPECIAL_EFFECTS;
	fxCmd.viewDef = &worldView;
	emptyCommand_t swapCmd;
	memset( &swapCmd, 0, sizeof( swapCmd ) );
	swapCmd.commandId = RC_SWAP_BUFFERS;
	drawCmd.next = &fxCmd.commandId;
	fxCmd.next = &swapCmd.commandId;
	swapCmd.next = NULL;

	idScenePacketFrame packetFrame;
	R_ScenePackets_BuildLegacyCommandStream( reinterpret_cast<const emptyCommand_t *>( &drawCmd ), packetFrame );
	idRenderGraph graph;
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );

	idModernGLDrawPlan plan;
	plan.Build( packetFrame, graph );
	const modernGLDrawPlanStats_t &stats = plan.Stats();

	const int expectedPlanned = tr.defaultMaterial != NULL ? 10 : 0;
	const int expectedFallback = tr.defaultMaterial != NULL ? 2 : packetFrame.NumDrawPackets();
	if ( stats.sourceDrawPackets != packetFrame.NumDrawPackets() || stats.plannedDraws != expectedPlanned || stats.fallbackDraws != expectedFallback ) {
		common->Printf( "RendererModernGLDrawPlan self-test failed: draw count mismatch\n" );
		return false;
	}
	if ( tr.defaultMaterial != NULL ) {
		if ( stats.depthDraws != 2 || stats.materialDraws != 8 || stats.stateBatches != 5 || stats.programSwitches != 1 || stats.overflow ) {
			common->Printf( "RendererModernGLDrawPlan self-test failed: plan classification mismatch\n" );
			return false;
		}
		if ( plan.NumEntries() != stats.plannedDraws ) {
			common->Printf( "RendererModernGLDrawPlan self-test failed: entry count mismatch\n" );
			return false;
		}
	}

	common->Printf(
		"RendererModernGLDrawPlan self-test passed (planned=%d fallback=%d batches=%d glsl=%d)\n",
		stats.plannedDraws,
		stats.fallbackDraws,
		stats.stateBatches,
		stats.highestGLSLVersion );
	return true;
}
