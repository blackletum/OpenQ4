// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "RenderGraph.h"

idRenderGraph::idRenderGraph()
	: numPasses( 0 ) {
	memset( &stats, 0, sizeof( stats ) );
}

void idRenderGraph::Clear( void ) {
	memset( passes, 0, sizeof( passes ) );
	memset( &stats, 0, sizeof( stats ) );
	numPasses = 0;
}

bool idRenderGraph::AddPass( renderPassCategory_t category, const char *name, bool enabled, bool legacyWrapped ) {
	if ( numPasses >= RENDER_GRAPH_MAX_PASSES ) {
		stats.overflow = true;
		return false;
	}
	passes[numPasses].category = category;
	passes[numPasses].name = name;
	passes[numPasses].passPacketCount = 0;
	passes[numPasses].drawPacketCount = 0;
	passes[numPasses].scenePacketCount = 0;
	passes[numPasses].commandPacketCount = 0;
	passes[numPasses].enabled = enabled;
	passes[numPasses].legacyWrapped = legacyWrapped;
	passes[numPasses].packetBacked = false;
	numPasses++;
	stats.graphPasses = numPasses;
	return true;
}

bool idRenderGraph::AddPacketPass( renderPassCategory_t category, const char *name, int drawPackets, int commandPackets ) {
	int passIndex = FindPass( category );
	if ( passIndex < 0 ) {
		if ( !AddPass( category, name, true, true ) ) {
			return false;
		}
		passIndex = numPasses - 1;
	}

	renderGraphPass_t &pass = passes[passIndex];
	pass.packetBacked = true;
	pass.passPacketCount++;
	pass.drawPacketCount += drawPackets;
	pass.commandPacketCount += commandPackets;
	pass.scenePacketCount++;
	stats.passPackets++;
	stats.drawPackets += drawPackets;
	stats.commandPackets += commandPackets;
	return true;
}

void idRenderGraph::SetPacketFrameStats( int scenePackets, int commandPackets, bool overflow ) {
	stats.scenePackets = scenePackets;
	stats.commandPackets = commandPackets;
	stats.overflow = stats.overflow || overflow;
}

int idRenderGraph::NumPasses( void ) const {
	return numPasses;
}

int idRenderGraph::FindPass( renderPassCategory_t category ) const {
	for ( int i = 0; i < numPasses; ++i ) {
		if ( passes[i].category == category ) {
			return i;
		}
	}
	return -1;
}

const renderGraphPass_t &idRenderGraph::Pass( int index ) const {
	return passes[index];
}

const renderGraphStats_t &idRenderGraph::Stats( void ) const {
	return stats;
}

static bool R_RenderGraph_HasPass( const idRenderGraph &graph, renderPassCategory_t category ) {
	for ( int i = 0; i < graph.NumPasses(); ++i ) {
		if ( graph.Pass( i ).category == category ) {
			return true;
		}
	}
	return false;
}

static void R_RenderGraph_AddPassOnce( idRenderGraph &graph, renderPassCategory_t category, const char *name ) {
	if ( !R_RenderGraph_HasPass( graph, category ) ) {
		graph.AddPass( category, name, true, true );
	}
}

void R_RenderGraph_BuildLegacyFrameGraph( const emptyCommand_t *cmds, idRenderGraph &graph ) {
	graph.Clear();

	for ( const emptyCommand_t *cmd = cmds; cmd != NULL; cmd = reinterpret_cast<const emptyCommand_t *>( cmd->next ) ) {
		switch ( cmd->commandId ) {
		case RC_DRAW_VIEW:
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_DEPTH, "legacyDepth" );
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_ARB2_INTERACTION, "legacyARB2Interaction" );
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_LIGHT_GRID, "legacyLightGrid" );
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_AMBIENT, "legacyAmbient" );
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_FOG_BLEND, "legacyFogBlend" );
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_AUTHORED_POST, "legacyPostProcess" );
			break;
		case RC_DRAW_SPECIAL_EFFECTS:
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_SPECIAL_EFFECTS, "legacySpecialEffects" );
			break;
		case RC_SET_RENDERTEXTURE:
		case RC_RESOLVE_MSAA:
		case RC_CLEAR_RENDERTARGET:
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_AUTHORED_POST, "legacyRenderTargetOps" );
			break;
		case RC_SWAP_BUFFERS:
			R_RenderGraph_AddPassOnce( graph, RENDER_PASS_PRESENT, "legacyPresent" );
			break;
		default:
			break;
		}
	}
}

static const char *R_RenderGraph_LegacyPassName( renderPassCategory_t category ) {
	switch ( category ) {
	case RENDER_PASS_DEPTH:
		return "legacyDepth";
	case RENDER_PASS_STENCIL_SHADOW:
		return "legacyStencilShadow";
	case RENDER_PASS_SHADOW_MAP:
		return "legacyShadowMap";
	case RENDER_PASS_ARB2_INTERACTION:
		return "legacyARB2Interaction";
	case RENDER_PASS_LIGHT_GRID:
		return "legacyLightGrid";
	case RENDER_PASS_AMBIENT:
		return "legacyAmbient";
	case RENDER_PASS_FOG_BLEND:
		return "legacyFogBlend";
	case RENDER_PASS_SSAO:
		return "legacySSAO";
	case RENDER_PASS_MOTION_BLUR:
		return "legacyMotionBlur";
	case RENDER_PASS_LENS_FLARE:
		return "legacyLensFlare";
	case RENDER_PASS_BLOOM:
		return "legacyBloom";
	case RENDER_PASS_AUTHORED_POST:
		return "legacyPostProcess";
	case RENDER_PASS_SPECIAL_EFFECTS:
		return "legacySpecialEffects";
	case RENDER_PASS_GUI:
		return "legacyGUI";
	case RENDER_PASS_PRESENT:
		return "legacyPresent";
	default:
		return RenderPassCategory_Name( category );
	}
}

void R_RenderGraph_BuildFromScenePackets( const idScenePacketFrame &packetFrame, idRenderGraph &graph ) {
	graph.Clear();

	for ( int i = 0; i < packetFrame.NumPasses(); ++i ) {
		const passPacket_t &pass = packetFrame.Pass( i );
		graph.AddPacketPass( pass.passCategory, R_RenderGraph_LegacyPassName( pass.passCategory ), pass.drawPacketCount, 0 );
	}

	const scenePacketFrameStats_t &packetStats = packetFrame.Stats();
	graph.SetPacketFrameStats( packetStats.scenePackets, packetStats.commandPackets, packetStats.overflow );
}

void R_RenderGraph_LogIfVerbose( const idRenderGraph &graph ) {
	if ( r_rendererMetrics.GetInteger() < 2 ) {
		return;
	}

	const renderGraphStats_t &stats = graph.Stats();
	common->Printf(
		"renderGraph packet passes=%d passPackets=%d scenes=%d draws=%d cmds=%d overflow=%d:",
		graph.NumPasses(),
		stats.passPackets,
		stats.scenePackets,
		stats.drawPackets,
		stats.commandPackets,
		stats.overflow ? 1 : 0 );
	for ( int i = 0; i < graph.NumPasses(); ++i ) {
		const renderGraphPass_t &pass = graph.Pass( i );
		common->Printf(
			" %s%s[p=%d d=%d]",
			pass.name ? pass.name : RenderPassCategory_Name( pass.category ),
			pass.enabled ? "" : "(off)",
			pass.passPacketCount,
			pass.drawPacketCount );
	}
	common->Printf( "\n" );
}

static bool R_RenderGraph_CheckPass( const idRenderGraph &graph, int index, renderPassCategory_t category, int passPackets, int drawPackets ) {
	if ( index < 0 || index >= graph.NumPasses() ) {
		return false;
	}
	const renderGraphPass_t &pass = graph.Pass( index );
	return pass.category == category && pass.passPacketCount == passPackets && pass.drawPacketCount == drawPackets && pass.packetBacked;
}

static bool R_RenderGraph_RunWorldPacketSelfTest( void ) {
	drawSurf_t drawSurfs[2];
	memset( drawSurfs, 0, sizeof( drawSurfs ) );
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
	const renderGraphStats_t &stats = graph.Stats();
	if ( graph.NumPasses() != 8 || stats.passPackets != 8 || stats.scenePackets != 3 || stats.drawPackets != 12 || stats.commandPackets != 2 || stats.overflow ) {
		return false;
	}

	return
		R_RenderGraph_CheckPass( graph, 0, RENDER_PASS_DEPTH, 1, 2 ) &&
		R_RenderGraph_CheckPass( graph, 1, RENDER_PASS_ARB2_INTERACTION, 1, 2 ) &&
		R_RenderGraph_CheckPass( graph, 2, RENDER_PASS_LIGHT_GRID, 1, 2 ) &&
		R_RenderGraph_CheckPass( graph, 3, RENDER_PASS_AMBIENT, 1, 2 ) &&
		R_RenderGraph_CheckPass( graph, 4, RENDER_PASS_FOG_BLEND, 1, 2 ) &&
		R_RenderGraph_CheckPass( graph, 5, RENDER_PASS_AUTHORED_POST, 1, 2 ) &&
		R_RenderGraph_CheckPass( graph, 6, RENDER_PASS_SPECIAL_EFFECTS, 1, 0 ) &&
		R_RenderGraph_CheckPass( graph, 7, RENDER_PASS_PRESENT, 1, 0 );
}

static bool R_RenderGraph_RunGuiPacketSelfTest( void ) {
	drawSurf_t drawSurf;
	memset( &drawSurf, 0, sizeof( drawSurf ) );
	drawSurf_t *drawSurfPtrs[1] = { &drawSurf };
	viewDef_t guiView;
	memset( &guiView, 0, sizeof( guiView ) );
	guiView.viewEntitys = NULL;
	guiView.drawSurfs = drawSurfPtrs;
	guiView.numDrawSurfs = 1;

	drawSurfsCommand_t drawCmd;
	memset( &drawCmd, 0, sizeof( drawCmd ) );
	drawCmd.commandId = RC_DRAW_VIEW;
	drawCmd.viewDef = &guiView;
	emptyCommand_t swapCmd;
	memset( &swapCmd, 0, sizeof( swapCmd ) );
	swapCmd.commandId = RC_SWAP_BUFFERS;
	drawCmd.next = &swapCmd.commandId;
	swapCmd.next = NULL;

	idScenePacketFrame packetFrame;
	R_ScenePackets_BuildLegacyCommandStream( reinterpret_cast<const emptyCommand_t *>( &drawCmd ), packetFrame );
	idRenderGraph graph;
	R_RenderGraph_BuildFromScenePackets( packetFrame, graph );
	const renderGraphStats_t &stats = graph.Stats();
	if ( graph.NumPasses() != 2 || stats.passPackets != 2 || stats.scenePackets != 2 || stats.drawPackets != 1 || stats.commandPackets != 1 || stats.overflow ) {
		return false;
	}

	return
		R_RenderGraph_CheckPass( graph, 0, RENDER_PASS_GUI, 1, 1 ) &&
		R_RenderGraph_CheckPass( graph, 1, RENDER_PASS_PRESENT, 1, 0 );
}

bool RendererRenderGraph_RunSelfTest( void ) {
	if ( !R_RenderGraph_RunWorldPacketSelfTest() ) {
		common->Printf( "RendererRenderGraph self-test failed: world packet graph mismatch\n" );
		return false;
	}
	if ( !R_RenderGraph_RunGuiPacketSelfTest() ) {
		common->Printf( "RendererRenderGraph self-test failed: gui packet graph mismatch\n" );
		return false;
	}
	common->Printf( "RendererRenderGraph self-test passed (2 cases)\n" );
	return true;
}
