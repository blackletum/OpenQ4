// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	Vulkan backend for the shared renderer front-end (Phase C,
	docs/dev/plans/2026-07-17-vulkan-phase-c.md).

	Phase C scope: the real game loop runs against the module's
	idRenderSystem (the shared front-end), and every frame ends in an
	animated clear presented through the swapchain. Draw-bearing commands
	are consumed and discarded; later phases replace the discard with the
	real Vulkan draw path. This TU also carries the backend globals and the
	entry points the front-end links against in place of the excluded GL
	backend TUs.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../RenderModuleAPI.h"
#include "../GLStateCache.h"
#include "../GLDebugScope.h"
#include "../ShadowMapArb2Parity.h"
#include "../ModernGLShaderLibrary.h"
#include "../RendererUpload.h"
#include "../ModernGLExecutor.h"
#include "../RenderGraphResources.h"
#include "VulkanDevice.h"

// the back-end state object normally defined by tr_backend.cpp
backEndState_t	backEnd;

// defined in RenderSystem_init.cpp (shared front-end)
bool R_GetInitialWindowSize( bool fullScreen, int *width, int *height );

static const renderWindowServices_t *vkBackendServices = NULL;
static float vkClearColor[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };

// vk_GuiExecutor.cpp
void VK_GuiExecutor_SetClearColor( const float color[ 4 ] );
void VK_GuiExecutor_Draw2DView( const viewDef_t *viewDef );
void VK_GuiExecutor_Draw3DView( const viewDef_t *viewDef );
bool VK_GuiExecutor_EnsureFrameOpen( void );
bool VK_GuiExecutor_EndFrameAndPresent( void );
bool VK_GuiExecutor_FrameIsOpen( void );

/*
====================
VK_FillGLConfigFromDevice

The front-end keys nearly all behavior off glConfig; describe the Vulkan
device conservatively so CPU-side logic stays on portable paths.
====================
*/
static void VK_FillGLConfigFromDevice( void ) {
	static char vendorString[ 128 ];
	static char rendererString[ 256 ];
	static char versionString[ 128 ];

	idStr::snPrintf( vendorString, sizeof( vendorString ), "vendorID 0x%04x", vkCtx.deviceProperties.vendorID );
	idStr::Copynz( rendererString, vkCtx.deviceProperties.deviceName, sizeof( rendererString ) );
	idStr::snPrintf( versionString, sizeof( versionString ), "Vulkan %u.%u.%u",
			VK_API_VERSION_MAJOR( vkCtx.deviceProperties.apiVersion ),
			VK_API_VERSION_MINOR( vkCtx.deviceProperties.apiVersion ),
			VK_API_VERSION_PATCH( vkCtx.deviceProperties.apiVersion ) );

	glConfig.renderer_string = rendererString;
	glConfig.vendor_string = vendorString;
	glConfig.version_string = versionString;
	glConfig.extensions_string = "";
	glConfig.wgl_extensions_string = "";

	glConfig.maxTextureSize = (int)vkCtx.deviceProperties.limits.maxImageDimension2D;
	glConfig.maxTextureUnits = 8;
	glConfig.maxTextureCoords = 8;
	glConfig.maxTextureImageUnits = 16;

	glConfig.colorBits = 32;
	glConfig.depthBits = 24;
	glConfig.stencilBits = 8;

	glConfig.vidWidth = (int)vkCtx.swapchainExtent.width;
	glConfig.vidHeight = (int)vkCtx.swapchainExtent.height;

	glConfig.isFullscreen = r_fullscreen.GetBool();

	// the front-end's standard (ARB2-shaped) path is what the Vulkan backend
	// consumes; without this SetBackEndRenderer() finds no usable back end
	// and FatalErrors on the first BeginFrame after a config marks
	// r_renderer modified
	glConfig.allowARB2Path = true;

	// capabilities Vulkan carries unconditionally that shared front-end code
	// checks: NPOT (kills TG_POT_CORRECTION texgens), S3TC + BC7 (without
	// these R_BinaryImageHeaderSupportedByRenderer REJECTS generated
	// compressed .bimage files and lightgrid chunks — retail DDS loads
	// bypass the check, which masked this at menu scope), cube maps
	// (vk_Image allocates 6-layer CUBE_COMPATIBLE images since Phase D)
	glConfig.textureNonPowerOfTwoAvailable = true;
	glConfig.textureCompressionAvailable = true;
	glConfig.bptcTextureCompressionAvailable = true;
	glConfig.cubeMapAvailable = true;
}

/*
====================
VK_InitRenderDevice

The Vulkan equivalent of R_InitOpenGL's window/context bring-up: create the
engine window for a Vulkan surface, bring up the device + swapchain, fill
glConfig, and hand input to the engine.
====================
*/
bool VK_InitRenderDevice( void ) {
	common->Printf( "----- VK_InitRenderDevice -----\n" );

	vkBackendServices = Sys_GetRenderWindowServices();
	if ( vkBackendServices == NULL ) {
		common->Warning( "Vulkan: no window services on this platform backend" );
		return false;
	}
	if ( !vkBackendServices->PrepareWindowSystem() ) {
		common->Warning( "Vulkan: window system preparation failed" );
		return false;
	}

	glimpParms_t parms;
	memset( &parms, 0, sizeof( parms ) );
	parms.fullScreen = r_fullscreen.GetBool();
	R_GetInitialWindowSize( parms.fullScreen, &parms.width, &parms.height );
	parms.borderless = !parms.fullScreen && r_borderless.GetBool();
	parms.displayHz = r_displayRefresh.GetInteger();
	parms.multiSamples = 0;
	parms.stereo = false;

	renderWindowParms_t windowParms;
	memset( &windowParms, 0, sizeof( windowParms ) );
	windowParms.width = parms.width;
	windowParms.height = parms.height;
	windowParms.fullScreen = parms.fullScreen;
	windowParms.borderless = parms.borderless;
	windowParms.hiddenWindow = r_hiddenWindow.GetBool();
	windowParms.displayHz = parms.displayHz;

	renderFramebufferDesc_t desc;
	memset( &desc, 0, sizeof( desc ) );
	desc.surfaceKind = RENDER_SURFACE_VULKAN;
	desc.redBits = 8;
	desc.greenBits = 8;
	desc.blueBits = 8;
	desc.alphaBits = 8;
	desc.depthBits = 24;
	desc.stencilBits = 8;
	desc.doubleBuffer = true;

	renderModuleWindowInfo_t windowInfo;
	memset( &windowInfo, 0, sizeof( windowInfo ) );
	bool reusedPreserved = false;
	if ( !vkBackendServices->CreateWindowForFramebuffer( &desc, &windowParms, &windowInfo, &reusedPreserved ) ) {
		common->Warning( "Vulkan: window creation failed" );
		return false;
	}

	if ( !VK_Device_Init( vkBackendServices ) ) {
		vkBackendServices->DestroyAttemptWindow();
		return false;
	}

	(void)vkBackendServices->ApplyScreenParms( &windowParms );
	vkBackendServices->RefreshNativeWindowHandles( &windowInfo );
	if ( windowInfo.pixelWidth > 0 && windowInfo.pixelHeight > 0
			&& ( (uint32_t)windowInfo.pixelWidth != vkCtx.swapchainExtent.width
				|| (uint32_t)windowInfo.pixelHeight != vkCtx.swapchainExtent.height ) ) {
		// fullscreen/mode application changed the drawable size
		(void)VK_Device_RecreateSwapchain();
	}

	VK_FillGLConfigFromDevice();
	glConfig.uiViewportX = windowInfo.uiViewportX;
	glConfig.uiViewportY = windowInfo.uiViewportY;
	glConfig.uiViewportWidth = windowInfo.uiViewportWidth;
	glConfig.uiViewportHeight = windowInfo.uiViewportHeight;
	glConfig.isInitialized = true;

	vkBackendServices->NotifyWindowReady();
	Sys_InitInput();

	common->Printf( "Vulkan renderer initialized: %s (%s)\n", glConfig.renderer_string, glConfig.version_string );
	return true;
}

/*
====================
VK_ShutdownRenderDevice
====================
*/
void VK_ShutdownRenderDevice( void ) {
	VK_Device_Shutdown();
	if ( vkBackendServices != NULL ) {
		vkBackendServices->BeginWindowTeardown();
		vkBackendServices->FinishWindowTeardown();
	}
	glConfig.isInitialized = false;
}

/*
====================
GLimp_* context entry points

The front-end keeps its existing calls; under the Vulkan backend they map
onto the device context (or are inert where GL semantics have no analog).
====================
*/
void GLimp_Shutdown( void ) {
	VK_ShutdownRenderDevice();
}

bool GLimp_SetScreenParms( glimpParms_t parms ) {
	if ( vkBackendServices == NULL ) {
		return false;
	}
	renderWindowParms_t windowParms;
	memset( &windowParms, 0, sizeof( windowParms ) );
	windowParms.width = parms.width;
	windowParms.height = parms.height;
	windowParms.fullScreen = parms.fullScreen;
	windowParms.borderless = parms.borderless;
	windowParms.hiddenWindow = parms.hiddenWindow;
	windowParms.displayHz = parms.displayHz;
	if ( !vkBackendServices->ApplyScreenParms( &windowParms ) ) {
		return false;
	}
	(void)VK_Device_RecreateSwapchain();
	glConfig.vidWidth = (int)vkCtx.swapchainExtent.width;
	glConfig.vidHeight = (int)vkCtx.swapchainExtent.height;
	glConfig.isFullscreen = parms.fullScreen;
	return true;
}

void GLimp_SwapBuffers( void ) {
	// live window-state poll, mirroring the GL seam
	if ( vkBackendServices != NULL && vkBackendServices->RefreshNativeWindowHandles != NULL ) {
		renderModuleWindowInfo_t info;
		memset( &info, 0, sizeof( info ) );
		vkBackendServices->RefreshNativeWindowHandles( &info );
		glConfig.uiViewportX = info.uiViewportX;
		glConfig.uiViewportY = info.uiViewportY;
		glConfig.uiViewportWidth = info.uiViewportWidth;
		glConfig.uiViewportHeight = info.uiViewportHeight;
		if ( info.pixelWidth > 0 && info.pixelHeight > 0
				&& ( (uint32_t)info.pixelWidth != vkCtx.swapchainExtent.width
					|| (uint32_t)info.pixelHeight != vkCtx.swapchainExtent.height ) ) {
			// only recreate between frames; an open frame presents into the
			// old swapchain and OUT_OF_DATE handling catches the rest
			if ( !VK_GuiExecutor_FrameIsOpen() ) {
				if ( VK_Device_RecreateSwapchain() ) {
					glConfig.vidWidth = (int)vkCtx.swapchainExtent.width;
					glConfig.vidHeight = (int)vkCtx.swapchainExtent.height;
				}
			}
		}
	}

	// present whatever the frame holds; a frame with no draws still clears
	VK_GuiExecutor_SetClearColor( vkClearColor );
	if ( VK_GuiExecutor_EnsureFrameOpen() ) {
		VK_GuiExecutor_EndFrameAndPresent();
	}
}

void GLimp_ActivateContext( void ) {
}

void GLimp_DeactivateContext( void ) {
}

bool GLimp_EnsureActiveContext( const char *operation ) {
	(void)operation;
	return vkCtx.initialized;
}

void *GLimp_ExtensionPointer( const char *name ) {
	(void)name;
	return NULL;
}

void GLimp_SetGamma( unsigned short red[256], unsigned short green[256], unsigned short blue[256] ) {
	(void)red;
	(void)green;
	(void)blue;
}

bool GLimp_UseNativeGammaRamps( void ) {
	return false;
}

void GLimp_EnableLogging( bool enable ) {
	(void)enable;
}

void GLimp_PreserveWindowOnShutdown( void ) {
}

// legacy SMP entry points referenced by dormant paths
void GLimp_WakeBackEnd( void *data ) {
	(void)data;
}

void GLimp_FrontEndSleep( void ) {
}

void *GLimp_BackEndSleep( void ) {
	return NULL;
}

bool GLimp_SpawnRenderThread( void ( *function )( void ) ) {
	(void)function;
	return false;
}

/*
====================
RB_ExecuteBackEndCommands

Phase D command consumption: 2D views draw through the GUI executor; 3D
views arrive with Phase E; RC_SWAP_BUFFERS closes and presents the frame.
====================
*/
void RB_ExecuteBackEndCommands( const emptyCommand_t *cmds ) {
	if ( !glConfig.isInitialized ) {
		return;
	}

	for ( ; cmds != NULL; cmds = (const emptyCommand_t *)cmds->next ) {
		switch ( cmds->commandId ) {
			case RC_NOP:
				break;
			case RC_SET_BUFFER: {
				const setBufferCommand_t *cmd = (const setBufferCommand_t *)cmds;
				backEnd.frameCount = cmd->frameCount;
				// clear-color policy mirrors the GL RB_SetBuffer; black
				// otherwise (the swapchain load op always clears)
				float c[ 3 ];
				if ( sscanf( r_clear.GetString(), "%f %f %f", &c[ 0 ], &c[ 1 ], &c[ 2 ] ) == 3 ) {
					vkClearColor[ 0 ] = c[ 0 ];
					vkClearColor[ 1 ] = c[ 1 ];
					vkClearColor[ 2 ] = c[ 2 ];
				} else if ( r_clear.GetInteger() == 1 ) {
					vkClearColor[ 0 ] = 0.4f;
					vkClearColor[ 1 ] = 0.0f;
					vkClearColor[ 2 ] = 0.25f;
				} else {
					vkClearColor[ 0 ] = vkClearColor[ 1 ] = vkClearColor[ 2 ] = 0.0f;
				}
				vkClearColor[ 3 ] = 1.0f;
				VK_GuiExecutor_SetClearColor( vkClearColor );
				break;
			}
			case RC_DRAW_VIEW: {
				const drawSurfsCommand_t *cmd = (const drawSurfsCommand_t *)cmds;
				if ( cmd->viewDef != NULL ) {
					if ( cmd->viewDef->viewEntitys == NULL ) {
						// 2D view (GUI/console/cinematics)
						VK_GuiExecutor_Draw2DView( cmd->viewDef );
					} else {
						// world view: depth prepass + ambient walks (Phase E)
						VK_GuiExecutor_Draw3DView( cmd->viewDef );
					}
				}
				break;
			}
			case RC_SWAP_BUFFERS:
				GLimp_SwapBuffers();
				break;
			default:
				// copy/capture commands arrive with Phase E+
				break;
		}
	}
}


/*
===============================================================================
	Phase C link surface for the excluded GL-backend TUs.

	The shared front-end references these symbols on paths that stay cold
	under the clear-only Vulkan backend (image upload halves, GL state/debug
	caches, ARB2 shadow parity, the GL shader library, upload-ring stats).
	Each becomes a real Vulkan implementation in its roadmap phase (D:
	images/uploads, E/F: draw + shadow paths); until then they are inert.
===============================================================================
*/

// tr_backend/tr_main frame allocator anchor (the functions live in the
// shared tr_main.cpp; only the global's home was the excluded tr_backend)
frameData_t *frameData = NULL;

// idImage GPU half: real Vulkan implementation in vk_Image.cpp (Phase D)

// --- idRenderTexture (OpenGL/gl_RenderTexture.cpp): bookkeeping-only until
// the Vulkan render-target path lands (Phase E/H) ---
idRenderTexture::idRenderTexture( idImage *colorImage, idImage *depthImage ) {
	if ( colorImage != NULL ) {
		colorImages.Append( colorImage );
	}
	this->depthImage = depthImage;
	deviceHandle = 0;
	deviceHandleGeneration = -1;
	validatedCubeFaces = 0;
	cachedDepthHandle = 0;
	cachedDepthGeneration = 0;
}

idRenderTexture::~idRenderTexture() {
}

bool idRenderTexture::Resize( int width, int height ) {
	(void)width; (void)height;
	return true;
}

bool idRenderTexture::EnsureDeviceHandle( void ) {
	return false;
}

bool idRenderTexture::MakeCurrent( void ) {
	return false;
}

bool idRenderTexture::MakeCurrent( int cubeFace ) {
	(void)cubeFace;
	return false;
}

void idRenderTexture::BindNull( void ) {
}

unsigned int idRenderTexture::GetDeviceHandle( void ) {
	return 0;
}

void idRenderTexture::SetDebugLabel( const char *label ) {
	debugLabel = label != NULL ? label : "";
}

void idRenderTexture::AddRenderImage( idImage *image ) {
	if ( image != NULL ) {
		colorImages.Append( image );
	}
}

bool idRenderTexture::InitRenderTexture( void ) {
	return false;
}

// --- tr_backend helpers ---
void RB_LogComment( const char *comment, ... ) {
	(void)comment;
}

void GL_SelectTextureNoClient( int unit ) {
	(void)unit;
}

void GL_ClearStateDelta( void ) {
}

void RB_GetShaderTextureMatrix( const float *shaderRegisters, const textureStage_t *texture, float matrix[16] ) {
	(void)shaderRegisters; (void)texture;
	// identity until the Vulkan draw path lands (Phase E)
	memset( matrix, 0, sizeof( float ) * 16 );
	matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

// --- draw_arb2 surface ---
int R_FindARBProgram( unsigned int target, const char *program ) {
	(void)target; (void)program;
	return 0;
}

bool RB_DrawSurfHasSoftParticleStage( const drawSurf_t *surf ) {
	(void)surf;
	return false;
}

bool RB_ShadowMapBuildArb2ParityState( const viewLight_t *vLight, const viewDef_t *viewDef, int shadowMapSize, shadowMapArb2ParityState_t &state ) {
	(void)vLight; (void)viewDef; (void)shadowMapSize;
	memset( &state, 0, sizeof( state ) );
	return false;
}

bool RB_ShadowMapEstimateArb2CacheOwnership( const viewLight_t *vLight, const viewDef_t *viewDef, shadowMapArb2CacheEstimate_t &estimate ) {
	(void)vLight; (void)viewDef;
	memset( &estimate, 0, sizeof( estimate ) );
	return false;
}

bool RB_ShadowMapProjectedAtlasSlotForLight( int lightDefIndex, shadowMapArb2AtlasSlot_t &slot ) {
	(void)lightDefIndex;
	memset( &slot, 0, sizeof( slot ) );
	return false;
}

void RB_ShadowMapProjectedAtlasSlotMarkUsed( int lightDefIndex ) {
	(void)lightDefIndex;
}

bool RB_ShadowMapArb2ReceiverFallbackSelfTest( void ) {
	// no ARB2 receiver path exists under the Vulkan backend yet
	return true;
}

// --- GL debug scope/labels (KHR_debug; no Vulkan analog wired yet) ---
idGLDebugScope::idGLDebugScope( const char *name, unsigned int id ) {
	(void)name; (void)id;
}

idGLDebugScope::~idGLDebugScope() {
}

void R_GLDebug_LabelBuffer( unsigned int name, const char *label ) {
	(void)name; (void)label;
}

void R_GLDebug_LabelTexture( unsigned int name, const char *label ) {
	(void)name; (void)label;
}

void R_GLDebug_LabelProgram( unsigned int name, const char *label ) {
	(void)name; (void)label;
}

void R_GLDebug_LabelVertexArray( unsigned int name, const char *label ) {
	(void)name; (void)label;
}

// --- GL state cache (cold under the clear backend) ---
idGLStateCache::idGLStateCache() {
}

bool idGLStateCache::UseProgram( GLuint program ) { (void)program; return false; }
bool idGLStateCache::BindVertexArray( GLuint vertexArray ) { (void)vertexArray; return false; }
bool idGLStateCache::BindBuffer( GLenum target, GLuint buffer ) { (void)target; (void)buffer; return false; }
bool idGLStateCache::BindBufferBase( GLenum target, GLuint index, GLuint buffer ) { (void)target; (void)index; (void)buffer; return false; }
bool idGLStateCache::BindBuffersBase( GLenum target, GLuint first, GLsizei count, const GLuint *buffers ) { (void)target; (void)first; (void)count; (void)buffers; return false; }
bool idGLStateCache::ActiveTextureUnit( int unit ) { (void)unit; return false; }
bool idGLStateCache::BindTexture( int unit, GLenum target, GLuint texture ) { (void)unit; (void)target; (void)texture; return false; }
bool idGLStateCache::BindFramebuffer( GLenum target, GLuint framebuffer ) { (void)target; (void)framebuffer; return false; }
bool idGLStateCache::SetBlendEnabled( bool enabled ) { (void)enabled; return false; }
bool idGLStateCache::SetDepthTestEnabled( bool enabled ) { (void)enabled; return false; }
bool idGLStateCache::SetDepthMask( GLboolean mask ) { (void)mask; return false; }
bool idGLStateCache::SetStencilTestEnabled( bool enabled ) { (void)enabled; return false; }
bool idGLStateCache::SetScissorTestEnabled( bool enabled ) { (void)enabled; return false; }
bool idGLStateCache::SetCullFaceEnabled( bool enabled ) { (void)enabled; return false; }
bool idGLStateCache::SetViewport( GLint x, GLint y, GLsizei width, GLsizei height ) { (void)x; (void)y; (void)width; (void)height; return false; }
bool idGLStateCache::SetScissor( GLint x, GLint y, GLsizei width, GLsizei height ) { (void)x; (void)y; (void)width; (void)height; return false; }
bool idGLStateCache::SetColorMask( GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha ) { (void)red; (void)green; (void)blue; (void)alpha; return false; }

idGLStateCache &R_GLStateCache( void ) {
	static idGLStateCache cache;
	return cache;
}

void R_GLStateCache_InvalidateAll( const char *reason ) {
	(void)reason;
}

// --- GL shader library (Vulkan pipeline library replaces it in Phase E) ---
const modernGLShaderLibraryStats_t &R_ModernGLShaderLibrary_Stats( void ) {
	static modernGLShaderLibraryStats_t stats;
	memset( &stats, 0, sizeof( stats ) );
	return stats;
}

const modernGLShaderProgramInfo_t *R_ModernGLShaderLibrary_FindProgram( modernGLShaderProgramKind_t kind, int preferredGLSLVersion ) {
	(void)kind; (void)preferredGLSLVersion;
	return NULL;
}

// --- upload ring stats (Vulkan staging ring replaces it in Phase D) ---
const rendererUploadStats_t &R_RendererUpload_Stats( void ) {
	static rendererUploadStats_t stats;
	memset( &stats, 0, sizeof( stats ) );
	return stats;
}


/*
===============================================================================
	Residual link surface (complete enumeration, link cycle 6).

	R_InitOpenGL and the GL vid-restart flow stay compiled but unreachable
	under the Vulkan seam; their callees plus the per-frame upload hooks,
	GL self-test commands, and debug-tool entry points resolve here.
===============================================================================
*/

// GL bring-up flow (unreachable: the InitOpenGL seam routes to
// VK_InitRenderDevice)
bool GLimp_Init( glimpParms_t parms ) {
	(void)parms;
	common->Warning( "GLimp_Init reached under the Vulkan backend" );
	return false;
}

void R_ARB2_Init( void ) {
}

void RB_ResetARB2InteractionHandoffBreadcrumb( void ) {
}

void R_ReloadARBPrograms_f( const idCmdArgs &args ) {
	(void)args;
	common->Printf( "reloadARBprograms: not applicable under the Vulkan backend\n" );
}

void R_ReportShaderPrograms_f( const idCmdArgs &args ) {
	(void)args;
	common->Printf( "reportShaderPrograms: not applicable under the Vulkan backend\n" );
}

bool R_ValidateGLSLProgram( newShaderStage_t *stage ) {
	(void)stage;
	return true;
}

bool R_IsARBProgramValid( unsigned int target, unsigned int handle ) {
	(void)target; (void)handle;
	return true;
}

// GL state wrappers (cold)
void GL_State( int stateBits ) {
	(void)stateBits;
}

void GL_Cull( int cullType ) {
	(void)cullType;
}

void GL_SelectTexture( int unit ) {
	(void)unit;
}

void GL_TexEnv( int env ) {
	(void)env;
}

// KHR_debug output layer
bool R_GLDebugOutput_Available( void ) {
	return false;
}

bool R_GLDebugOutput_Registered( void ) {
	return false;
}

void R_GLDebugOutput_Init( void ) {
}

void R_GLDebugOutput_Shutdown( void ) {
}

void R_GLDebugOutput_FlushMessages( void ) {
}

// modern GL executor (the Vulkan executor replaces it from Phase E)
void R_ModernGLExecutor_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	(void)caps; (void)features;
}

void R_ModernGLExecutor_Shutdown( void ) {
}

void R_ModernGLExecutor_InvalidatePlans( void ) {
}

void R_ModernGLExecutor_PrintGfxInfo( void ) {
}

bool R_ModernGLExecutor_ModernVisibleRequestedForPost( void ) {
	return false;
}

const modernGLExecutorStats_t &R_ModernGLExecutor_Stats( void ) {
	static modernGLExecutorStats_t stats;
	memset( &stats, 0, sizeof( stats ) );
	return stats;
}

bool R_ModernGLShaderLibrary_Reload( void ) {
	return false;
}

// render-graph transient resources (Vulkan graph resources from Phase E)
void R_RenderGraphResources_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	(void)caps; (void)features;
}

void R_RenderGraphResources_Shutdown( void ) {
}

void R_RenderGraphResources_PrintGfxInfo( void ) {
}

void R_RenderGraphResources_DumpLatest( void ) {
}

// upload ring (Vulkan staging ring from Phase D)
void R_RendererUpload_Init( const renderBackendCaps_t &caps ) {
	(void)caps;
}

void R_RendererUpload_Shutdown( void ) {
}

void R_RendererUpload_BeginFrame( int frameCount ) {
	(void)frameCount;
}

void R_RendererUpload_EndFrame( void ) {
}

bool R_RendererUpload_AllocFrameTemp( void *data, int bytes, int alignment, rendererUploadAllocation_t &allocation ) {
	(void)data; (void)bytes; (void)alignment;
	memset( &allocation, 0, sizeof( allocation ) );
	return false;
}

bool R_RendererUpload_AllocStaticBuffer( void *data, int bytes, bool indexBuffer, bool dynamic, unsigned int &handle ) {
	(void)data; (void)bytes; (void)indexBuffer; (void)dynamic;
	handle = 0;
	return false;
}

void R_RendererUpload_FreeStaticBuffer( unsigned int &handle, int bytes, bool indexBuffer ) {
	(void)bytes; (void)indexBuffer;
	handle = 0;
}

bool R_RendererUpload_DynamicFrameBridgeAvailable( void ) {
	return false;
}

int R_RendererUpload_FrameCapacity( void ) {
	return 0;
}

void R_RendererUpload_RecordLegacyStall( void ) {
}

void R_RendererUpload_RecordLegacyUpload( int bytes ) {
	(void)bytes;
}

// shadow-map / post-process resource teardown hooks
bool RB_ShadowMapResourcesKnownGood( bool verbose ) {
	(void)verbose;
	return false;
}

void RB_ShutdownShadowMapResources( void ) {
}

void RB_ShutdownScenePostProcess( void ) {
}

// buffered debug-tool surface (Phase I implements as buffered-line draws)
void RB_AddDebugLine( const idVec4 &color, const idVec3 &start, const idVec3 &end, const int lifeTime, const bool depthTest ) {
	(void)color; (void)start; (void)end; (void)lifeTime; (void)depthTest;
}

void RB_AddDebugPolygon( const idVec4 &color, const idWinding &winding, const int lifeTime, const bool depthTest ) {
	(void)color; (void)winding; (void)lifeTime; (void)depthTest;
}

void RB_AddDebugText( const char *text, const idVec3 &origin, float scale, const idVec4 &color, const idMat3 &viewAxis, const int align, const int lifetime, bool depthTest ) {
	(void)text; (void)origin; (void)scale; (void)color; (void)viewAxis; (void)align; (void)lifetime; (void)depthTest;
}

void RB_ClearDebugLines( int time ) {
	(void)time;
}

void RB_ClearDebugPolygons( int time ) {
	(void)time;
}

void RB_ClearDebugText( int time ) {
	(void)time;
}

void RB_DrawBounds( const idBounds &bounds ) {
	(void)bounds;
}

float RB_DrawTextLength( const char *text, float scale, int len ) {
	(void)text; (void)scale; (void)len;
	return 0.0f;
}

void RB_DrawElementsImmediate( const srfTriangles_t *tri ) {
	(void)tri;
}

void RB_ShowImages( void ) {
}

void RB_ShutdownDebugTools( void ) {
}

// GL-subsystem self-test commands: honest skips under the Vulkan backend
#define VK_GL_SELFTEST_STUB( name ) \
	bool name( void ) { \
		common->Printf( #name ": skipped (GL subsystem; not applicable under the Vulkan backend)\n" ); \
		return true; \
	}

VK_GL_SELFTEST_STUB( RendererDeferredResolve_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererForwardPlus_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererGBuffer_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererGLStateCache_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererGpuDriven_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererLowOverhead_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererModernCompatibility_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererModernGLExecutor_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererModernGLShaderLibrary_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererModernVisibility_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererModernVisible_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererPassOwnership_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererRenderGraphResource_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererUpload_RunSelfTest )
VK_GL_SELFTEST_STUB( RendererVisiblePath_RunSelfTest )

#endif /* OPENQ4_RENDERER_VK_MODULE */


