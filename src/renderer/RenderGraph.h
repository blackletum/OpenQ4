// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __RENDER_GRAPH_H__
#define __RENDER_GRAPH_H__

#include "ScenePackets.h"

const int RENDER_GRAPH_MAX_PASSES = 32;

typedef struct renderGraphPass_s {
	renderPassCategory_t	category;
	const char				*name;
	int						passPacketCount;
	int						drawPacketCount;
	int						scenePacketCount;
	int						commandPacketCount;
	bool					enabled;
	bool					legacyWrapped;
	bool					packetBacked;
} renderGraphPass_t;

typedef struct renderGraphStats_s {
	int						graphPasses;
	int						passPackets;
	int						scenePackets;
	int						drawPackets;
	int						commandPackets;
	bool					overflow;
} renderGraphStats_t;

class idRenderGraph {
public:
	idRenderGraph();
	void Clear( void );
	bool AddPass( renderPassCategory_t category, const char *name, bool enabled, bool legacyWrapped );
	bool AddPacketPass( renderPassCategory_t category, const char *name, int drawPackets, int commandPackets );
	void SetPacketFrameStats( int scenePackets, int commandPackets, bool overflow );
	int NumPasses( void ) const;
	int FindPass( renderPassCategory_t category ) const;
	const renderGraphPass_t &Pass( int index ) const;
	const renderGraphStats_t &Stats( void ) const;

private:
	renderGraphPass_t passes[RENDER_GRAPH_MAX_PASSES];
	renderGraphStats_t stats;
	int numPasses;
};

void R_RenderGraph_BuildLegacyFrameGraph( const emptyCommand_t *cmds, idRenderGraph &graph );
void R_RenderGraph_BuildFromScenePackets( const idScenePacketFrame &packetFrame, idRenderGraph &graph );
void R_RenderGraph_LogIfVerbose( const idRenderGraph &graph );
bool RendererRenderGraph_RunSelfTest( void );

#endif /* !__RENDER_GRAPH_H__ */
