// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __MODERN_GL_EXECUTOR_H__
#define __MODERN_GL_EXECUTOR_H__

#include "RendererCaps.h"
#include "RenderGraph.h"

typedef struct modernGLExecutorStats_s {
	bool	available;
	bool	enabled;
	bool	initialized;
	bool	vaoReady;
	bool	frameUBOReady;
	bool	shaderLibraryReady;
	bool	gpuDrivenReady;
	bool	lowOverheadReady;
	bool	sceneSSBOReady;
	bool	indirectBufferReady;
	bool	validationSSBOReady;
	bool	computeValidationReady;
	bool	tierUsesDSA;
	bool	tierUsesMultiBind;
	bool	legacyFallback;
	bool	drawPlanReady;
	bool	drawPlanOverflow;
	bool	submitPlanReady;
	bool	submitPlanOverflow;
	bool	visibleDepthRequested;
	bool	visibleDepthExecuted;
	bool	visibleDepthResourceReady;
	bool	visibleShadowResourceReady;
	bool	visibleDepthDebugOverlayReady;
	bool	opaqueGBufferRequested;
	bool	opaqueGBufferExecuted;
	bool	opaqueGBufferResourcesReady;
	bool	opaqueGBufferMRTReady;
	bool	opaqueGBufferDebugOverlayReady;
	int		graphPasses;
	int		preparedPasses;
	int		fallbackPasses;
	int		drawPackets;
	int		preparedDrawPackets;
	int		materialDrawPackets;
	int		resourceDrawPackets;
	int		geometryDrawPackets;
	int		guiDrawPackets;
	int		worldDrawPackets;
	int		shaderProgramCount;
	int		shaderFailureCount;
	int		highestGLSLVersion;
	int		gpuDrivenSceneRecords;
	int		gpuDrivenIndirectRecords;
	int		gpuDrivenSceneBytes;
	int		gpuDrivenIndirectBytes;
	int		gpuDrivenValidationBytes;
	int		gpuDrivenComputeDispatches;
	int		lowOverheadDSAUpdates;
	int		lowOverheadMultiBindBatches;
	int		drawPlanDraws;
	int		drawPlanDepthDraws;
	int		drawPlanMaterialDraws;
	int		drawPlanFallbackDraws;
	int		drawPlanIndexedDraws;
	int		drawPlanVertexOnlyDraws;
	int		drawPlanStateBatches;
	int		drawPlanProgramSwitches;
	int		drawPlanMaterialSwitches;
	int		submitPlanDraws;
	int		submitPlanFallbackDraws;
	int		submitPlanDepthDraws;
	int		submitPlanMaterialDraws;
	int		submitPlanMissingAmbientDraws;
	int		submitPlanMissingIndexDraws;
	int		submitPlanIndexUploadDraws;
	int		submitPlanProgramBatches;
	int		submitPlanVertexBufferBatches;
	int		submitPlanIndexBufferBatches;
	int		submitPlanScissorBatches;
	int		submitPlanMaterialBatches;
	int		submitPlanUniformUpdates;
	int		submitPlanFrameUBOBinds;
	bool	submitExecuted;
	int		submittedDraws;
	int		submittedDepthDraws;
	int		submittedMaterialDraws;
	int		submittedIndexUploadDraws;
	int		submittedFallbackDraws;
	int		visibleDepthDraws;
	int		visibleDepthFallbackDraws;
	int		visibleShadowDepthDraws;
	int		visibleShadowFallbackDraws;
	int		visibleStencilShadowFallbackDraws;
	int		visibleDepthMaterialFallbackDraws;
	int		visibleDepthAlphaTestFallbackDraws;
	int		visibleDepthDeformFallbackDraws;
	int		visibleDepthSkinnedFallbackDraws;
	int		visibleDepthGeometryFallbackDraws;
	int		visibleDepthResourceFallbackDraws;
	int		visibleDepthMismatchDraws;
	int		visibleDepthClearOps;
	int		visibleDepthResolveOps;
	int		visibleDepthDebugOverlayDraws;
	int		opaqueGBufferDraws;
	int		opaqueGBufferFallbackDraws;
	int		opaqueGBufferMaterialFallbackDraws;
	int		opaqueGBufferAlphaTestDraws;
	int		opaqueGBufferAlphaTestFallbackDraws;
	int		opaqueGBufferSkinnedDraws;
	int		opaqueGBufferSkinnedFallbackDraws;
	int		opaqueGBufferDeformFallbackDraws;
	int		opaqueGBufferGeometryFallbackDraws;
	int		opaqueGBufferTextureFallbackDraws;
	int		opaqueGBufferResourceFallbackDraws;
	int		opaqueGBufferLightGridDraws;
	int		opaqueGBufferClearOps;
	int		opaqueGBufferAttachmentCount;
	int		opaqueGBufferBytesPerPixel;
	int		opaqueGBufferBandwidthKB;
	int		opaqueGBufferDebugOverlayDraws;
	char	status[96];
} modernGLExecutorStats_t;

void R_ModernGLExecutor_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features );
void R_ModernGLExecutor_Shutdown( void );
void R_ModernGLExecutor_PrepareFrame( const idScenePacketFrame &packetFrame, const idRenderGraph &graph );
void R_ModernGLExecutor_DrawDepthDebugOverlay( void );
void R_ModernGLExecutor_DrawGBufferDebugOverlay( void );
const modernGLExecutorStats_t &R_ModernGLExecutor_Stats( void );
void R_ModernGLExecutor_PrintGfxInfo( void );
bool RendererModernGLExecutor_RunSelfTest( void );
bool RendererVisiblePath_RunSelfTest( void );
bool RendererGBuffer_RunSelfTest( void );

#endif /* !__MODERN_GL_EXECUTOR_H__ */
