#ifndef __VK_EXECUTOR_HOOKS_H__
#define __VK_EXECUTOR_HOOKS_H__

/*
===============================================================================

	Narrow executor hooks for the passes that live outside vk_GuiExecutor.cpp:
	the full-screen post passes (vk_PostProcess.cpp), the scene overlays
	(vk_SceneEffects.cpp) and the debug tools (vk_DebugTools.cpp). vkExec
	stays file-static in vk_GuiExecutor.cpp; these are the only ways in.

	Every extra pass shares the interaction pipeline layout: single-sampler
	sets 0-5, the dynamic uniform slice on set 6 (at most 256 bytes) and the
	128-byte push range for both stages.

===============================================================================
*/

// vertex layouts for VK_Exec_ExtraPipeline
enum vkExtraVertexLayout_t {
	VK_EXTRA_VERTEX_NONE = 0,		// the vertex shader makes its own vertices
	VK_EXTRA_VERTEX_POSITION,		// idDrawVert xyz only
	VK_EXTRA_VERTEX_DRAWVERT,		// the interaction layout: xyz 0, colour 1, normal 2, tangents 3-4, st 5
	VK_EXTRA_VERTEX_DEBUG,			// vkDebugVert_t: xyzw 0, RGBA8 colour 1, st 2 (28-byte stride)
	VK_EXTRA_VERTEX_GUI				// gui.vert's idDrawVert xyz 0, colour 1 and st 2
};

// VK_Exec_ExtraPipeline flags
static const int VK_EXTRA_PIPELINE_LINES = 1 << 0;			// line-list topology
static const int VK_EXTRA_PIPELINE_COLOR_OFF = 1 << 1;		// every colour channel masked
static const int VK_EXTRA_PIPELINE_ALPHA_TO_COVERAGE = 1 << 2;
static const int VK_EXTRA_PIPELINE_POINTS = 1 << 3;			// point-list topology

// VK_Exec_ExtraPipeline kind ranges: vk_PostProcess.cpp 1-31,
// vk_SceneEffects.cpp 32-63, vk_DebugTools.cpp 64-95, vk_GuiExecutor.cpp 96+
static const int VK_EXTRA_KIND_SCENE_BASE = 32;
static const int VK_EXTRA_KIND_DEBUG_BASE = 64;
static const int VK_EXTRA_KIND_EXECUTOR_BASE = 96;

VkCommandBuffer		VK_Exec_ActiveCmd( void );
int					VK_Exec_ActiveFrameSlot( void );
bool				VK_GuiExecutor_FrameIsOpen( void );
bool				VK_GuiExecutor_BeginFrame( void );
bool				VK_GuiExecutor_EndFrameAndPresent( void );
bool				VK_Exec_MainRenderingScopeOpen( void );
bool				VK_Exec_ActiveTargetHasStencil( void );
bool				VK_Exec_ActiveTargetMultisampled( void );
int					VK_Exec_ActiveFramebufferWidth( void );
int					VK_Exec_ActiveFramebufferHeight( void );
idRenderTexture *	VK_Exec_ActiveRenderTexture( void );
int					VK_Exec_ActiveCubeFace( void );
bool				VK_Exec_SetRenderTarget( idRenderTexture *renderTexture, int cubeFace = 0 );
void				VK_Exec_ClearRenderTarget( bool clearColor, bool clearDepth, float depthValue,
							const float colorValue[ 4 ] );
bool				VK_Exec_CopyRender( idImage *image, int x, int y, int width, int height,
							int cubeFace, bool copyDepth );
bool				VK_Exec_ResolveRenderTargets( idRenderTexture *sourceRenderTexture,
							idRenderTexture *destinationRenderTexture, bool resolveDepth );
// _currentDepth for this view, captured once (backEnd.currentDepthCopied)
bool				VK_Exec_CaptureViewDepth( const viewDef_t *viewDef );

VkPipelineLayout	VK_Exec_InteractionPipelineLayout( void );
// Source-alpha variants of the interaction pipelines; shadowMode is
// 0 unshadowed, 1 projected, 2 point, matching vkInterPass_t::shadowMode.
VkPipeline			VK_Exec_TransparentInteractionPipeline( int shadowMode, bool composite );
VkDescriptorSet		VK_Exec_ImageDescriptor( unsigned int texnum, bool require2D );
VkDescriptorSet		VK_Exec_InteractionUniformSet( void );
int					VK_Exec_InteractionUniformAlloc( const void *data, int bytes );
VkPipeline			VK_Exec_PostPipeline( int kind, VkShaderModule vertModule,
							VkShaderModule fragModule, int stateBits );
VkPipeline			VK_Exec_ExtraPipeline( int kind, VkShaderModule vertModule,
							VkShaderModule fragModule, int stateBits, int vertexLayout, int flags );
void				VK_Exec_TransitionImageForSampling( idImage *image );
// Queue one compact luminance sample per slot. Completion is delivered only
// after that slot's existing frame fence, without an extra GPU wait. The
// diagnostic synchronous mode submits and resumes this frame without presenting.
bool				VK_Exec_QueueHDRExposureReadback( idImage *image, unsigned int generation, int frame, bool synchronous );
void				VK_PostProcess_ConsumeHDRSample( unsigned int generation, int frame, float logLuminance );
bool				VK_PostProcess_HDRSceneRequested( void );

bool				VK_Exec_BindTriGeometry( VkCommandBuffer cmd, int slot, const srfTriangles_t *tri );
// streams vertices into the frame's vertex ring and binds them as binding 0
bool				VK_Exec_BindTransientVertices( VkCommandBuffer cmd, const void *data, int bytes );
void				VK_BuildSurfMVP( const viewDef_t *viewDef, const drawSurf_t *drawSurf, float outMvp[ 16 ] );
void				VK_Exec_SetSurfScissor( VkCommandBuffer cmd, const viewDef_t *viewDef,
							const drawSurf_t *drawSurf, int fbHeight );
void				VK_Exec_SetViewScissor( VkCommandBuffer cmd, const viewDef_t *viewDef, int fbHeight );
// the 3D view's negative-height viewport, as VK_GuiExecutor_Draw3DView sets
// it; maxDepth 0.5 is the weapon depth range
bool				VK_Exec_SetViewViewport( VkCommandBuffer cmd, const viewDef_t *viewDef, float maxDepth );

// R_SetDrawInteraction (vk_Interactions.cpp)
void				VK_Interactions_SetDrawInteraction( const shaderStage_t *surfaceStage,
							const float *surfaceRegs, idImage **image, idVec4 matrix[ 2 ], float color[ 4 ] );

// Native emission shares material admission with the direct-light owner and
// replaces only the proven matching additive ambient stage, once per surface.
bool VK_PBR_EmissionForStage( const drawSurf_t *surf, int stageIndex,
		idImage *&image, float color[ 4 ] );

// Native ordered transparency. Returns true when the light pass recorded this
// surface's draws for the named source-alpha stage and has now composited
// them; the classic stage draw is then the replaced work, not extra work.
bool VK_PBR_DrawTransparentStage( VkCommandBuffer cmd, const viewDef_t *viewDef,
		const drawSurf_t *drawSurf, const srfTriangles_t *tri, const float mvp[ 16 ],
		int stageIndex, float alphaScale );

// The debug tools' stencil prints (RB_CountStencilBuffer, RB_ScanStencilBuffer).
// The executor copies the active target's stencil out and hands it to
// VK_DebugTools_PrintStencilReadback once the frame's fence has signalled.
enum vkStencilReadbackMode_t {
	VK_STENCIL_READBACK_COUNT = 1,	// the average, "overdraw: %5.1f"
	VK_STENCIL_READBACK_SCAN		// the histogram, "stencil values:"
};
bool				VK_Exec_QueueStencilReadback( int mode );
void				VK_DebugTools_PrintStencilReadback( const byte *stencil, int width, int height, int mode );

#endif /* !__VK_EXECUTOR_HOOKS_H__ */
