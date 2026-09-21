// Copyright (C) 2026 DarkMatter Productions
//

#include "tr_local.h"
#include "RenderModuleAPI.h"
#include "RendererModule.h"
#include "../framework/RenderDoc.h"
#include "../framework/CVarCompletionSnapshot.h"
#include "../bse/BSEInterface.h"

/*
===============================================================================

	Renderer-module selection, loading, and fail-closed fallback.

	See docs/dev/plans/2026-07-16-vulkan-renderer.md.

===============================================================================
*/

// loader-owned cvars: defined here (not in a renderer TU) so they exist in
// every build shape, including module-only clients that shed the static
// renderer sources
static const char *r_renderApiArgs[] = { "best", "gl", "vulkan", "gl-module", NULL };
idCVar r_renderApi( "r_renderApi", "gl", CVAR_RENDERER | CVAR_ARCHIVE, "rendering API: best = platform default (currently gl), gl = OpenGL renderer (loaded as the renderer-gl module on module-only builds, statically linked elsewhere), vulkan = experimental Vulkan renderer module (falls back to gl when loading, device probing or startup device preparation fails; a later vid_restart device failure selects gl for the next launch), gl-module = alias that always selects the OpenGL module. Module selections take effect on engine restart.", r_renderApiArgs, idCmdSystem::ArgCompletion_String<r_renderApiArgs> );
idCVar r_actualRenderApi( "r_actualRenderApi", "UNINITIALIZED", CVAR_RENDERER | CVAR_ROM, "rendering API actually active after request/fallback selection" );

// engine-side homes for window/gui cvars referenced by both the platform
// backend (engine) and the renderer sources (module mirrors them in its
// glue TU); defined here for the same every-build-shape reason
idCVar r_fullscreenDesktop( "r_fullscreenDesktop", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "1 = native desktop fullscreen, 0 = exclusive mode using r_mode/r_customWidth/r_customHeight" );
idCVar r_borderless( "r_borderless", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "1 = borderless window mode when r_fullscreen is 0" );
idCVar r_windowWidth( "r_windowWidth", "1280", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "windowed mode width" );
idCVar r_windowHeight( "r_windowHeight", "720", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "windowed mode height" );
idCVar r_skipGuiShaders( "r_skipGuiShaders", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = skip all gui elements on surfaces, 2 = skip drawing but still handle events, 3 = draw but skip events", 0, 3, idCmdSystem::ArgCompletion_Integer<0,3> );

#ifdef OPENQ4_RENDERER_MODULE_ONLY
// module-only clients have no renderer TUs; the engine-side interface
// globals live here, NULL until RM_PublishActiveModuleInterfaces fills them
// at first boot (before declManager->Init, so nothing dereferences earlier)
idRenderSystem *		renderSystem = NULL;
idRenderModelManager *	renderModelManager = NULL;
#endif

#if defined( _M_X64 ) || defined( __x86_64__ )
	#define RENDERER_MODULE_ARCH_TAG "x64"
#elif defined( _M_IX86 ) || defined( __i386__ )
	#define RENDERER_MODULE_ARCH_TAG "x86"
#elif defined( _M_ARM64 ) || defined( __aarch64__ )
	#define RENDERER_MODULE_ARCH_TAG "arm64"
#else
	#define RENDERER_MODULE_ARCH_TAG "unknown"
#endif

typedef struct rendererModuleState_s {
	rendererModuleStatus_t	status;
	intptr_t				moduleHandle;
	renderExport_t			moduleExport;
	bool					moduleExportValid;
	// engine-side interface pointers saved across a module activation so
	// unload can restore them
	idRenderSystem *		savedRenderSystem;
	idRenderModelManager *	savedRenderModelManager;
	bool					interfacesPublished;
	// module activation (publishing a module renderSystem) is only allowed
	// during the first boot, before renderSystem->Init() binds engine state
	// to the active renderer instance
	bool					activationAllowed;
	bool					everBooted;
	bool					startupDeviceFailed;
	bool					startupRetryPending;
	// cvar completion callbacks as they were before the loaded module's
	// GetRenderAPI registered its static cvars; put back before it unloads
	idCVarCompletionSnapshot	moduleCompletions;
} rendererModuleState_t;

static rendererModuleState_t rm_state;

// module binary short tags; indexed by rendererModuleApi_t
static const char *rm_moduleBinaryTags[ RENDER_MODULE_API_COUNT ] = { "gl", "vk", "gl" };
static const char *rm_apiNames[ RENDER_MODULE_API_COUNT ] = { "gl", "vulkan", "gl-module" };

/*
====================
Engine-side bindings for the module services table
====================
*/
static void RM_Services_Printf( const char *fmt, ... ) {
	va_list	args;
	char	text[ 4096 ];

	va_start( args, fmt );
	idStr::vsnPrintf( text, sizeof( text ), fmt, args );
	va_end( args );
	common->Printf( "%s", text );
}

static void RM_Services_Warning( const char *fmt, ... ) {
	va_list	args;
	char	text[ 4096 ];

	va_start( args, fmt );
	idStr::vsnPrintf( text, sizeof( text ), fmt, args );
	va_end( args );
	common->Warning( "%s", text );
}

static void RM_Services_Error( const char *fmt, ... ) {
	va_list	args;
	char	text[ 4096 ];

	va_start( args, fmt );
	idStr::vsnPrintf( text, sizeof( text ), fmt, args );
	va_end( args );
	common->Error( "%s", text );
}

static int RM_Services_Milliseconds( void ) {
	return Sys_Milliseconds();
}

static const char *RM_Services_CVarGetString( const char *name ) {
	return cvarSystem->GetCVarString( name );
}

static int RM_Services_CVarGetInteger( const char *name ) {
	return cvarSystem->GetCVarInteger( name );
}

static bool RM_Services_CVarGetBool( const char *name ) {
	return cvarSystem->GetCVarBool( name );
}

static void RM_Services_CVarSetString( const char *name, const char *value ) {
	cvarSystem->SetCVarString( name, value );
}

static void RM_Services_Sleep( int msec ) {
	Sys_Sleep( msec );
}

static void RM_Services_EnterCriticalSection( int index ) {
	Sys_EnterCriticalSection( index );
}

static void RM_Services_LeaveCriticalSection( int index ) {
	Sys_LeaveCriticalSection( index );
}

static bool RM_Services_IsRenderDocInjected( void ) {
	return RenderDoc_IsInjected();
}

static void RM_Services_PrintRendererApiStatus( void ) {
	RendererModule_PrintGfxInfo();
}

static bool RM_Services_ResetRenderApiAfterDeviceFailure( void ) {
	return R_RendererModule_ResetApiAfterDeviceFailure();
}

static const renderModuleServices_t rm_services = {
	RM_Services_Printf,
	RM_Services_Warning,
	RM_Services_Error,
	RM_Services_Milliseconds,
	RM_Services_CVarGetString,
	RM_Services_CVarGetInteger,
	RM_Services_CVarGetBool,
	RM_Services_CVarSetString,
	RM_Services_Sleep,
	RM_Services_EnterCriticalSection,
	RM_Services_LeaveCriticalSection,
	RM_Services_IsRenderDocInjected,
	RM_Services_PrintRendererApiStatus,
	RM_Services_ResetRenderApiAfterDeviceFailure,
};

/*
====================
RM_BuildImport

Single import builder shared by the boot loader and the on-demand probe so
the field set cannot drift between them.
====================
*/
static void RM_BuildImport( renderImport_t &moduleImport ) {
	memset( &moduleImport, 0, sizeof( moduleImport ) );
	moduleImport.version = RENDER_API_VERSION;
	moduleImport.services = &rm_services;
	moduleImport.sys = ::sys;
	moduleImport.common = ::common;
	moduleImport.cvarSystem = ::cvarSystem;
	moduleImport.cmdSystem = ::cmdSystem;
	moduleImport.fileSystem = ::fileSystem;
	moduleImport.declManager = ::declManager;
	moduleImport.soundSystem = ::soundSystem;
	moduleImport.session = ::session;
	moduleImport.uiManager = ::uiManager;
	moduleImport.collisionModelManager = ::collisionModelManager;
	moduleImport.eventLoop = ::eventLoop;
	moduleImport.bse = ::bse;
	moduleImport.windowServices = Sys_GetRenderWindowServices();
	moduleImport.console = ::console;
}

/*
====================
RM_PublishActiveModuleInterfaces

Points the engine's renderer-interface globals at a module's exports. Dormant
until the Phase B8 seam lets RM_ExportCanRender activate a full module; kept
wired into the activation branch so the flip stays a one-line policy change.
====================
*/
static void RM_PublishActiveModuleInterfaces( const renderExport_t &moduleExport ) {
	rm_state.savedRenderSystem = ::renderSystem;
	rm_state.savedRenderModelManager = ::renderModelManager;
	rm_state.interfacesPublished = true;
	if ( moduleExport.renderSystem != NULL ) {
		::renderSystem = moduleExport.renderSystem;
	}
	if ( moduleExport.renderModelManager != NULL ) {
		::renderModelManager = moduleExport.renderModelManager;
	}
}

/*
====================
RM_RestorePublishedInterfaces
====================
*/
static void RM_RestorePublishedInterfaces( void ) {
	if ( !rm_state.interfacesPublished ) {
		return;
	}
	::renderSystem = rm_state.savedRenderSystem;
	::renderModelManager = rm_state.savedRenderModelManager;
	rm_state.savedRenderSystem = NULL;
	rm_state.savedRenderModelManager = NULL;
	rm_state.interfacesPublished = false;
}

/*
====================
R_RendererModule_ParseApi
====================
*/
bool R_RendererModule_ParseApi( const char *value, rendererModuleApi_t &api ) {
	if ( value == NULL || value[ 0 ] == '\0' ) {
		api = RENDER_MODULE_API_GL;
		return false;
	}
	if ( idStr::Icmp( value, "gl" ) == 0 || idStr::Icmp( value, "opengl" ) == 0 ) {
		api = RENDER_MODULE_API_GL;
		return true;
	}
	if ( idStr::Icmp( value, "vulkan" ) == 0 || idStr::Icmp( value, "vk" ) == 0 ) {
		api = RENDER_MODULE_API_VULKAN;
		return true;
	}
	if ( idStr::Icmp( value, "gl-module" ) == 0 ) {
		api = RENDER_MODULE_API_GL_MODULE;
		return true;
	}
	if ( idStr::Icmp( value, "best" ) == 0 ) {
		// the platform default stays GL until Vulkan promotion evidence lands
		api = RENDER_MODULE_API_GL;
		return true;
	}
	api = RENDER_MODULE_API_GL;
	return false;
}

/*
====================
R_RendererModule_ApiName
====================
*/
const char *R_RendererModule_ApiName( rendererModuleApi_t api ) {
	if ( api < 0 || api >= RENDER_MODULE_API_COUNT ) {
		return "invalid";
	}
	return rm_apiNames[ api ];
}

/*
====================
R_RendererModule_BuildBinaryName
====================
*/
void R_RendererModule_BuildBinaryNameForArch( rendererModuleApi_t api, const char *archTag, char *outName, int maxLength ) {
	const char *tag = ( api >= 0 && api < RENDER_MODULE_API_COUNT ) ? rm_moduleBinaryTags[ api ] : "invalid";
	idStr::snPrintf( outName, maxLength, "renderer-%s_%s", tag, archTag );
}

void R_RendererModule_BuildBinaryName( rendererModuleApi_t api, char *outName, int maxLength ) {
	R_RendererModule_BuildBinaryNameForArch( api, RENDERER_MODULE_ARCH_TAG, outName, maxLength );
}

/*
====================
R_RendererModule_BuildFallbackLadder

Every ladder fails closed onto GL, which retains its own safe-mode retry.
====================
*/
int R_RendererModule_BuildFallbackLadder( rendererModuleApi_t requested, rendererModuleApi_t *outLadder, int maxEntries ) {
	int numEntries = 0;

	if ( maxEntries <= 0 ) {
		return 0;
	}
	if ( requested != RENDER_MODULE_API_GL ) {
		outLadder[ numEntries++ ] = requested;
	}
	if ( numEntries < maxEntries ) {
		outLadder[ numEntries++ ] = RENDER_MODULE_API_GL;
	}
	return numEntries;
}

/*
====================
RM_ResolveModulePath

Renderer modules load only from the executable directory, the platform package
root, or the platform's trusted module root, mirroring the trusted-root policy
in idFileSystem::FindDLL; they are never loaded from pak files, fs_savepath, or
mod content. The extra roots matter on macOS, where the executable lives in
openQ4.app/Contents/MacOS while signed Mach-O modules are staged in the flat
Contents/Frameworks code directory.
====================
*/
static bool RM_ModuleFileExists( const char *path ) {
	FILE *file = fopen( path, "rb" );
	if ( file == NULL ) {
		return false;
	}
	fclose( file );
	return true;
}

static bool RM_ResolveModulePath( rendererModuleApi_t api, char *outPath, int maxLength ) {
	char	binaryName[ MAX_OSPATH ];
	char	fileName[ MAX_OSPATH ];

	// Thin packages carry architecture-specific module names. A universal2
	// macOS package instead carries ONE two-slice module, so both executable
	// slices must converge on the same trusted name -- mirrors the game-module
	// fallback in openQ4_BuildGameModuleBinaryNameForArch / Common.cpp.
	const char *archTags[ 2 ];
	int numArchTags = 0;
	archTags[ numArchTags++ ] = RENDERER_MODULE_ARCH_TAG;
#if defined( MACOS_X ) || defined( __APPLE__ )
	archTags[ numArchTags++ ] = "universal2";
#endif

	R_RendererModule_BuildBinaryName( api, binaryName, sizeof( binaryName ) );
	sys->DLL_GetFileName( binaryName, fileName, sizeof( fileName ) );

	idStr exeDir = Sys_EXEPath();
	exeDir.StripFilename();

	idStr searchRoots[ 3 ];
	int numSearchRoots = 0;
	searchRoots[ numSearchRoots++ ] = exeDir;

	char moduleRoot[ MAX_OSPATH ];
	if ( Sys_GetGameModuleRootDirectory( moduleRoot, sizeof( moduleRoot ) )
			&& exeDir.Icmp( moduleRoot ) != 0 ) {
		searchRoots[ numSearchRoots++ ] = moduleRoot;
	}
	char packageRoot[ MAX_OSPATH ];
	if ( Sys_GetPackageRootDirectory( packageRoot, sizeof( packageRoot ) ) ) {
		bool duplicateRoot = false;
		for ( int i = 0; i < numSearchRoots; i++ ) {
			if ( searchRoots[ i ].Icmp( packageRoot ) == 0 ) {
				duplicateRoot = true;
				break;
			}
		}
		if ( !duplicateRoot ) {
			searchRoots[ numSearchRoots++ ] = packageRoot;
		}
	}

	// first existing candidate wins; fall back to the executable directory and
	// this slice's own arch name so a missing module still reports the path the
	// operator most likely expected
	idStr fallback;
	for ( int archIndex = 0; archIndex < numArchTags; archIndex++ ) {
		R_RendererModule_BuildBinaryNameForArch( api, archTags[ archIndex ], binaryName, sizeof( binaryName ) );
		sys->DLL_GetFileName( binaryName, fileName, sizeof( fileName ) );

		for ( int i = 0; i < numSearchRoots; i++ ) {
			idStr candidate = searchRoots[ i ] + PATHSEPERATOR_STR + fileName;
			if ( candidate.Length() >= maxLength ) {
				continue;
			}
			if ( archIndex == 0 && i == 0 ) {
				fallback = candidate;
			}
			if ( RM_ModuleFileExists( candidate.c_str() ) ) {
				idStr::Copynz( outPath, candidate.c_str(), maxLength );
				return true;
			}
		}
	}

	if ( fallback.Length() == 0 ) {
		outPath[ 0 ] = '\0';
		return false;
	}
	idStr::Copynz( outPath, fallback.c_str(), maxLength );
	return true;
}

/*
====================
RM_ValidateExport

Shared by the live loader and the self-test so rejection rules cannot drift.
Returns false when the export must be rejected; a valid bring-up export
(NULL renderSystem) returns true with diagnosticsOnly set.
====================
*/
static bool RM_ValidateExport( const renderExport_t *moduleExport, bool &diagnosticsOnly, const char **reason ) {
	diagnosticsOnly = false;
	*reason = "";

	if ( moduleExport == NULL ) {
		*reason = "module returned no export table";
		return false;
	}
	if ( moduleExport->version != RENDER_API_VERSION ) {
		*reason = "module render API version mismatch";
		return false;
	}
	if ( moduleExport->backendName == NULL || moduleExport->backendName[ 0 ] == '\0' ) {
		*reason = "module reported no backend name";
		return false;
	}
	if ( moduleExport->renderSystem == NULL ) {
		diagnosticsOnly = true;
	}
	return true;
}

/*
====================
RM_ExportCanRender

Activation policy on top of shape validation: with the Phase B8 seam landed
the engine can host a full module-provided idRenderSystem, so any export that
carries one is activatable. Bring-up/diagnostics exports still refuse. Shared
with the self-test so the policy cannot silently drift.
====================
*/
static bool RM_ExportCanRender( const renderExport_t *moduleExport, const char **reason ) {
	bool diagnosticsOnly = false;

	if ( !RM_ValidateExport( moduleExport, diagnosticsOnly, reason ) ) {
		return false;
	}
	if ( diagnosticsOnly ) {
		*reason = "module is bring-up/diagnostics only";
		return false;
	}
	return true;
}

/*
====================
RM_ExportDeviceReady

A module that exports a device self-test must pass it before it activates.
The Vulkan
probe creates its own instance and device without touching a window, so it
covers a missing loader or driver, no GPU, a device below the feature floor,
and device creation. Later surface/swapchain/resource failures return through
PrepareStartupDevice; the engine releases all startup owners before retrying
the fallback renderer. Shared with the self-test so the policy cannot drift.
====================
*/
static bool RM_ExportDeviceReady( const renderExport_t *moduleExport, char *outSummary, int summaryLength ) {
	outSummary[ 0 ] = '\0';
	if ( moduleExport->diagnostics == NULL || moduleExport->diagnostics->RunDeviceSelfTest == NULL ) {
		return true;
	}
	if ( moduleExport->diagnostics->RunDeviceSelfTest( outSummary, summaryLength ) ) {
		return true;
	}
	if ( outSummary[ 0 ] == '\0' ) {
		idStr::Copynz( outSummary, "no failure detail recorded", summaryLength );
	}
	return false;
}

/*
====================
RM_UnloadModuleBinary

Every renderer module unload goes through here. The module's GetRenderAPI
pointed the value completion of each cvar it declares into the module, the
ones it shares with the engine and the active renderer included
(CVarCompletionSnapshot.h), so first put back the callbacks captured when it
was loaded.
====================
*/
static void RM_UnloadModuleBinary( intptr_t handle, idCVarCompletionSnapshot &completions ) {
	completions.Restore();
	completions.Clear();
	Sys_DLL_Unload( handle );
}

/*
====================
RM_UnloadModule
====================
*/
static void RM_UnloadModule( void ) {
	RM_RestorePublishedInterfaces();
	if ( rm_state.moduleExportValid && rm_state.moduleExport.Shutdown != NULL ) {
		rm_state.moduleExport.Shutdown();
	}
	rm_state.moduleExportValid = false;
	memset( &rm_state.moduleExport, 0, sizeof( rm_state.moduleExport ) );
	if ( rm_state.moduleHandle != 0 ) {
		RM_UnloadModuleBinary( rm_state.moduleHandle, rm_state.moduleCompletions );
		rm_state.moduleHandle = 0;
	}
}

/*
====================
RM_AppendFallbackReason
====================
*/
static void RM_AppendFallbackReason( rendererModuleStatus_t &status, const char *reason ) {
	if ( status.fallbackReason[ 0 ] != '\0' ) {
		idStr::Append( status.fallbackReason, sizeof( status.fallbackReason ), "; " );
	}
	idStr::Append( status.fallbackReason, sizeof( status.fallbackReason ), reason );
}

/*
====================
RM_TryLoadModuleApi

Attempts to activate one candidate API from the ladder. Returns true when the
candidate is now the active renderer path.
====================
*/
static bool RM_TryLoadModuleApi( rendererModuleApi_t api, rendererModuleStatus_t &status ) {
#ifndef OPENQ4_RENDERER_MODULE_ONLY
	if ( api == RENDER_MODULE_API_GL ) {
		// this build shape statically links the OpenGL renderer; activating
		// it is always possible
		status.activeApi = RENDER_MODULE_API_GL;
		status.disposition = ( status.requestedApi == RENDER_MODULE_API_GL ) ?
				RENDER_MODULE_DISPOSITION_BUILTIN : RENDER_MODULE_DISPOSITION_FALLBACK;
		return true;
	}
#endif

	if ( !rm_state.activationAllowed ) {
		// outside the first-boot activation window the engine has already
		// bound decl/material/font state to the active renderer instance, so
		// the candidate could never activate; don't even load it
		common->Warning( "r_renderApi '%s': renderer modules can only activate at engine startup; restart to apply",
				R_RendererModule_ApiName( api ) );
		RM_AppendFallbackReason( status, "renderer module activation requires an engine restart" );
		return false;
	}

	char modulePath[ sizeof( status.modulePath ) ];
	if ( !RM_ResolveModulePath( api, modulePath, sizeof( modulePath ) ) ) {
		RM_AppendFallbackReason( status, "module path resolution failed" );
		return false;
	}
	idStr::Copynz( status.modulePath, modulePath, sizeof( status.modulePath ) );

	common->Printf( "Loading renderer module: api='%s' path='%s'\n", R_RendererModule_ApiName( api ), modulePath );
	intptr_t handle = Sys_DLL_Load( modulePath );
	if ( handle == 0 ) {
		common->Warning( "renderer module '%s' failed to load", modulePath );
		RM_AppendFallbackReason( status, "module load failed" );
		return false;
	}
	// kept with the module if it activates, spent by the unload otherwise
	rm_state.moduleCompletions.Capture();

	GetRenderAPI_t GetRenderAPI = ( GetRenderAPI_t )Sys_DLL_GetProcAddress( handle, RENDER_API_ENTRY_POINT );
	if ( GetRenderAPI == NULL ) {
		common->Warning( "renderer module '%s' has no %s entry point", modulePath, RENDER_API_ENTRY_POINT );
		RM_UnloadModuleBinary( handle, rm_state.moduleCompletions );
		RM_AppendFallbackReason( status, "missing GetRenderAPI entry point" );
		return false;
	}

	renderImport_t moduleImport;
	RM_BuildImport( moduleImport );

	renderExport_t *moduleExport = GetRenderAPI( &moduleImport );

	const char *reason = "";
	if ( !RM_ExportCanRender( moduleExport, &reason ) ) {
		common->Warning( "renderer module '%s'%s%s cannot become the active renderer: %s; falling back to OpenGL. Use rendererVkProbe to inspect it.",
				modulePath,
				( moduleExport != NULL && moduleExport->moduleDescription != NULL ) ? " " : "",
				( moduleExport != NULL && moduleExport->moduleDescription != NULL ) ? moduleExport->moduleDescription : "",
				reason );
		if ( moduleExport != NULL && moduleExport->version == RENDER_API_VERSION && moduleExport->Shutdown != NULL ) {
			moduleExport->Shutdown();
		}
		RM_UnloadModuleBinary( handle, rm_state.moduleCompletions );
		RM_AppendFallbackReason( status, reason );
		return false;
	}

	// The early device gate costs
	// a throwaway instance and device on top of the renderer's own, so the
	// log records how long it took
	char deviceSummary[ 192 ];
	const int probeStartMsec = Sys_Milliseconds();
	const bool deviceReady = RM_ExportDeviceReady( moduleExport, deviceSummary, sizeof( deviceSummary ) );
	const int probeMsec = Sys_Milliseconds() - probeStartMsec;
	if ( !deviceReady ) {
		common->Warning( "renderer module '%s' found no usable device (%s) in %d ms; falling back to OpenGL. Use rendererVkProbe to inspect it.",
				modulePath, deviceSummary, probeMsec );
		if ( moduleExport->Shutdown != NULL ) {
			moduleExport->Shutdown();
		}
		RM_UnloadModuleBinary( handle, rm_state.moduleCompletions );
		char deviceReason[ 256 ];
		idStr::snPrintf( deviceReason, sizeof( deviceReason ), "device probe failed: %s", deviceSummary );
		RM_AppendFallbackReason( status, deviceReason );
		return false;
	}
	if ( deviceSummary[ 0 ] != '\0' ) {
		common->Printf( "Renderer module device probe: %s (%d ms)\n", deviceSummary, probeMsec );
	}

	rm_state.moduleHandle = handle;
	rm_state.moduleExport = *moduleExport;
	rm_state.moduleExportValid = true;
	RM_PublishActiveModuleInterfaces( rm_state.moduleExport );
	status.activeApi = api;
	status.disposition = ( status.requestedApi == api ) ?
			RENDER_MODULE_DISPOSITION_MODULE : RENDER_MODULE_DISPOSITION_FALLBACK;
	return true;
}

/*
====================
R_RendererModule_Boot
====================
*/
void R_RendererModule_Boot( void ) {
	rendererModuleStatus_t &status = rm_state.status;

	if ( rm_state.interfacesPublished ) {
		// a module renderSystem is live; re-boots (vid_restart) must not
		// unload the code the engine is executing through
		return;
	}

	const bool retryStartup = rm_state.startupRetryPending;
	const rendererModuleStatus_t failedStatus = status;
	rm_state.activationAllowed = !rm_state.everBooted || retryStartup;
	rm_state.everBooted = true;

	RM_UnloadModule();
	memset( &status, 0, sizeof( status ) );

	const char *requestedValue = retryStartup ? failedStatus.requestedValue : r_renderApi.GetString();
	idStr::Copynz( status.requestedValue, requestedValue, sizeof( status.requestedValue ) );

	rendererModuleApi_t requestedApi;
	if ( !R_RendererModule_ParseApi( requestedValue, requestedApi ) ) {
		common->Warning( "r_renderApi '%s' is not a valid rendering API; using 'gl'", requestedValue );
	}
#ifdef ID_DEDICATED
	// the dedicated server keeps the statically linked front-end until the
	// Phase B7 source unification; never activate a renderer module there
	if ( requestedApi != RENDER_MODULE_API_GL ) {
		common->Printf( "r_renderApi '%s' ignored on the dedicated server; using 'gl'\n", requestedValue );
		requestedApi = RENDER_MODULE_API_GL;
	}
#endif
	status.requestedApi = requestedApi;

	rendererModuleApi_t ladder[ RENDER_MODULE_API_COUNT ];
	const int numCandidates = R_RendererModule_BuildFallbackLadder(
			retryStartup ? RENDER_MODULE_API_GL : requestedApi, ladder, RENDER_MODULE_API_COUNT );
	if ( retryStartup ) {
		RM_AppendFallbackReason( status, failedStatus.fallbackReason );
		// The retry flag is set only after unloading. Report it here, once
		// the restarted filesystem can append to the original startup log.
		common->Printf( "Renderer startup recovery: failed Vulkan module unloaded after owner teardown\n" );
	}
	// Consume the retry before loading: a failing GL tail cannot recurse.
	rm_state.startupRetryPending = false;

	bool activated = false;
	for ( int i = 0; i < numCandidates; i++ ) {
		if ( RM_TryLoadModuleApi( ladder[ i ], status ) ) {
			activated = true;
			break;
		}
	}
	if ( !activated ) {
		// on module-only builds the GL tail is the renderer-gl module, which
		// can genuinely fail (missing/mis-staged binary); on static builds
		// the tail cannot fail and this stays a guard
		common->FatalError( "no renderer path could be activated (requested '%s'): %s", requestedValue,
				status.fallbackReason[ 0 ] != '\0' ? status.fallbackReason : "no failure detail recorded" );
		return;
	}

	r_actualRenderApi.SetString( R_RendererModule_ApiName( status.activeApi ) );

	if ( status.disposition == RENDER_MODULE_DISPOSITION_FALLBACK ) {
		common->Warning( "renderer API fallback: requested '%s', active '%s' (%s)",
				R_RendererModule_ApiName( status.requestedApi ),
				R_RendererModule_ApiName( status.activeApi ),
				status.fallbackReason );
	} else {
		common->Printf( "Renderer API: %s (%s)\n", R_RendererModule_ApiName( status.activeApi ),
				status.disposition == RENDER_MODULE_DISPOSITION_MODULE ? "module" : "builtin" );
	}
	if ( retryStartup ) {
		common->Printf( "Renderer startup recovery: OpenGL selected after complete owner teardown\n" );
	}
}

bool R_RendererModule_PrepareStartupDevice( void ) {
	if ( !rm_state.moduleExportValid || rm_state.moduleExport.PrepareStartupDevice == NULL ) {
		return true;
	}
	char reason[ 256 ] = {};
	if ( rm_state.moduleExport.PrepareStartupDevice( reason, sizeof( reason ) ) ) {
		return true;
	}
	rm_state.startupDeviceFailed = rm_state.status.activeApi == RENDER_MODULE_API_VULKAN;
	RM_AppendFallbackReason( rm_state.status,
			reason[ 0 ] != '\0' ? reason : "renderer startup device preparation failed" );
	common->Printf( "Renderer startup recovery: device preparation failed (%s); releasing startup owners\n",
			rm_state.status.fallbackReason );
	return false;
}

bool R_RendererModule_RetryFailedStartup( void ) {
	if ( !rm_state.startupDeviceFailed || rm_state.status.activeApi != RENDER_MODULE_API_VULKAN
			|| !rm_state.interfacesPublished ) {
		return false;
	}
	// The caller has completed ShutdownGame, including renderer and decl
	// destruction, and has returned from every module call. Retire console
	// callbacks as well as cvar completions before unloading their code.
	cmdSystem->RemoveFlaggedCommands( CMD_FL_RENDERER );
	RM_UnloadModule();
	rm_state.startupDeviceFailed = false;
	rm_state.startupRetryPending = true;
	r_actualRenderApi.SetString( "UNINITIALIZED" );
	return true;
}

/*
====================
RM_PeekConfigRenderApi

The archived r_renderApi value lives in the config file, but config execution
happens after renderSystem->Init() — too late for the module swap. Peek the
last r_renderApi set/seta out of the config text; command-line +set overrides
are applied on top by the caller.
====================
*/
static bool RM_PeekConfigRenderApi( char *outValue, int maxLength ) {
	char *buffer = NULL;
	if ( fileSystem->ReadFile( CONFIG_FILE, ( void ** )&buffer, NULL ) < 0 || buffer == NULL ) {
		return false;
	}

	bool found = false;
	idLexer src( buffer, idStr::Length( buffer ), CONFIG_FILE,
			LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWPATHNAMES | LEXFL_NOSTRINGESCAPECHARS );
	idToken token, name, value;
	while ( src.ReadToken( &token ) ) {
		if ( token.Icmp( "set" ) != 0 && token.Icmp( "seta" ) != 0 ) {
			src.SkipRestOfLine();
			continue;
		}
		if ( !src.ReadTokenOnLine( &name ) || name.Icmp( "r_renderApi" ) != 0 ) {
			src.SkipRestOfLine();
			continue;
		}
		if ( src.ReadTokenOnLine( &value ) ) {
			idStr::Copynz( outValue, value.c_str(), maxLength );
			found = true;		// last occurrence wins, like config execution
		}
		src.SkipRestOfLine();
	}

	fileSystem->FreeFile( buffer );
	return found;
}

/*
====================
Loader-owned console commands

Registered by the loader (not the renderer's R_InitCommands) so they exist
and reach the real loader diagnostics in every build shape, including
module-only clients where R_InitCommands runs inside the renderer module.
====================
*/
static void R_RendererModuleSelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !RendererModule_RunSelfTest() ) {
		common->Warning( "Renderer module self-test failed" );
	}
}

static void R_RendererVkProbe_f( const idCmdArgs &args ) {
	const bool verbose = ( args.Argc() < 2 ) || ( idStr::Icmp( args.Argv( 1 ), "quiet" ) != 0 );
	R_RendererModule_RunVulkanProbe( verbose );
}

// implemented in ui/DeviceContext.cpp; declared here rather than through
// ui/DeviceContext.h so this TU keeps no include edge into src/ui
bool UI_FontParity_RunSelfTest( void );

static void R_UIFontParitySelfTest_f( const idCmdArgs &args ) {
	(void)args;
	if ( !UI_FontParity_RunSelfTest() ) {
		common->Warning( "UI font parity self-test failed" );
	}
}

static void RM_RegisterCommands( void ) {
	cmdSystem->AddCommand( "rendererModuleSelfTest", R_RendererModuleSelfTest_f, CMD_FL_RENDERER, "run renderer module selection/loading self tests" );
	cmdSystem->AddCommand( "rendererVkProbe", R_RendererVkProbe_f, CMD_FL_RENDERER, "load the Vulkan renderer module, run its device bring-up probe, and unload it" );
	cmdSystem->AddCommand( "uiFontParitySelfTest", R_UIFontParitySelfTest_f, CMD_FL_RENDERER, "run GUI font retail parity self tests" );
}

/*
====================
R_RendererModule_BootEarly
====================
*/
void R_RendererModule_BootEarly( void ) {
	RM_RegisterCommands();

	char configValue[ 64 ];
	if ( RM_PeekConfigRenderApi( configValue, sizeof( configValue ) ) ) {
		r_renderApi.SetString( configValue );
	}
	// command-line +set r_renderApi overrides the archived value
	common->StartupVariable( "r_renderApi", false );

	R_RendererModule_Boot();
}

/*
====================
R_RendererModule_ResetApiAfterDeviceFailure

The active module could not start its device. Decls and the render system
already belong to it, so the ladder cannot fall back in-process any more; what
is left is making sure the next launch does not stop the same way, which it
would every time because r_renderApi is archived.

The config is appended to, never rewritten: this runs before the game module
registers its cvars, and WriteConfigToFile would drop every one of them. The
appended line wins because RM_PeekConfigRenderApi and config execution both
take the last set/seta. Only the fs_savepath config the engine itself writes is
touched, and only when it is what selected vulkan; a selection made on the
command line has nothing archived to undo.
====================
*/
bool R_RendererModule_ResetApiAfterDeviceFailure( void ) {
	r_renderApi.SetString( "gl" );

	char configValue[ 64 ];
	rendererModuleApi_t configApi;
	if ( !RM_PeekConfigRenderApi( configValue, sizeof( configValue ) )
			|| !R_RendererModule_ParseApi( configValue, configApi )
			|| configApi != RENDER_MODULE_API_VULKAN ) {
		return false;
	}

	// copied at once: the path lives in a buffer the next file call reuses
	const idStr configPath = fileSystem->RelativePathToOSPath( CONFIG_FILE, "fs_savepath" );
	idFile *existing = fileSystem->OpenExplicitFileRead( configPath.c_str() );
	if ( existing == NULL ) {
		return false;
	}
	fileSystem->CloseFile( existing );

	idFile *config = fileSystem->OpenFileAppend( CONFIG_FILE, false, "fs_savepath" );
	if ( config == NULL ) {
		common->Warning( "could not reset r_renderApi to gl in %s", configPath.c_str() );
		return false;
	}
	// the leading newline keeps a config that lacks a final newline from
	// joining its last line onto this one
	config->Printf( "\n// the Vulkan device could not start; the next launch uses OpenGL\nseta r_renderApi \"gl\"\n" );
	fileSystem->CloseFile( config );
	common->Printf( "r_renderApi reset to gl in %s; the next launch uses OpenGL unless a +set r_renderApi launch option selects vulkan again\n",
			configPath.c_str() );
	return true;
}

/*
====================
R_RendererModule_Shutdown
====================
*/
void R_RendererModule_Shutdown( void ) {
	RM_UnloadModule();
	rm_state.status.disposition = RENDER_MODULE_DISPOSITION_NONE;
}

/*
====================
R_RendererModule_GetStatus
====================
*/
const rendererModuleStatus_t &R_RendererModule_GetStatus( void ) {
	return rm_state.status;
}

/*
====================
RendererModule_PrintGfxInfo
====================
*/
void RendererModule_PrintGfxInfo( void ) {
	const rendererModuleStatus_t &status = rm_state.status;
	const char *dispositionName = "not-booted";

	switch ( status.disposition ) {
		case RENDER_MODULE_DISPOSITION_BUILTIN:	dispositionName = "builtin"; break;
		case RENDER_MODULE_DISPOSITION_MODULE:	dispositionName = "module"; break;
		case RENDER_MODULE_DISPOSITION_FALLBACK:	dispositionName = "fallback"; break;
		default: break;
	}

	common->Printf( "Renderer API: requested=%s active=%s disposition=%s\n",
			R_RendererModule_ApiName( status.requestedApi ),
			R_RendererModule_ApiName( status.activeApi ),
			dispositionName );
	if ( status.modulePath[ 0 ] != '\0' ) {
		common->Printf( "Renderer module path: %s\n", status.modulePath );
	}
	if ( status.fallbackReason[ 0 ] != '\0' ) {
		common->Printf( "Renderer API fallback reason: %s\n", status.fallbackReason );
	}
}

/*
====================
R_RendererModule_RunVulkanProbe

Loads the Vulkan module for a diagnostics pass and unloads it afterwards.
Never touches the live GL context or window; the probe is instance/device
scoped only.

Refused while Vulkan is the active renderer. Loading the module again then
returns the live instance rather than a fresh one: GetRenderAPI would
re-initialize its idlib under the running renderer, Shutdown would free its
SIMD processor and clear its dict string pools while the renderer still uses
both, and the probe's throwaway instance and device would repoint the volk
function pointers the live device calls through.
====================
*/
bool R_RendererModule_RunVulkanProbe( bool verbose ) {
	char modulePath[ 1024 ];

	if ( rm_state.interfacesPublished && rm_state.status.activeApi == RENDER_MODULE_API_VULKAN ) {
		common->Printf( "rendererVkProbe: the Vulkan renderer is active; restart with r_renderApi gl to probe\n" );
		return false;
	}

	if ( !RM_ResolveModulePath( RENDER_MODULE_API_VULKAN, modulePath, sizeof( modulePath ) ) ) {
		common->Printf( "rendererVkProbe: module path resolution failed\n" );
		return false;
	}
	common->Printf( "rendererVkProbe: loading '%s'\n", modulePath );

	intptr_t handle = Sys_DLL_Load( modulePath );
	if ( handle == 0 ) {
		common->Printf( "rendererVkProbe: module not present or failed to load (build with -Dbuild_renderer_vk=true and stage it next to the executable)\n" );
		return false;
	}
	// the unload puts back every cvar completion GetRenderAPI takes over,
	// the active renderer's and the game's included
	idCVarCompletionSnapshot completions;
	completions.Capture();

	bool probePassed = false;
	GetRenderAPI_t GetRenderAPI = ( GetRenderAPI_t )Sys_DLL_GetProcAddress( handle, RENDER_API_ENTRY_POINT );
	if ( GetRenderAPI == NULL ) {
		common->Printf( "rendererVkProbe: module has no %s entry point\n", RENDER_API_ENTRY_POINT );
	} else {
		renderImport_t moduleImport;
		RM_BuildImport( moduleImport );

		renderExport_t *moduleExport = GetRenderAPI( &moduleImport );

		bool diagnosticsOnly = false;
		const char *reason = "";
		if ( !RM_ValidateExport( moduleExport, diagnosticsOnly, &reason ) ) {
			common->Printf( "rendererVkProbe: module rejected: %s\n", reason );
		} else if ( moduleExport->diagnostics == NULL || moduleExport->diagnostics->RunProbe == NULL ) {
			common->Printf( "rendererVkProbe: module exposes no probe diagnostics\n" );
		} else {
			common->Printf( "rendererVkProbe: module '%s' (%s)\n",
					moduleExport->backendName,
					moduleExport->moduleDescription != NULL ? moduleExport->moduleDescription : "no description" );
			probePassed = moduleExport->diagnostics->RunProbe( verbose );
			common->Printf( "rendererVkProbe: %s\n", probePassed ? "PASS" : "FAIL" );
		}
		if ( moduleExport != NULL && moduleExport->version == RENDER_API_VERSION && moduleExport->Shutdown != NULL ) {
			moduleExport->Shutdown();
		}
	}

	RM_UnloadModuleBinary( handle, completions );
	return probePassed;
}

// stand-ins for a module's device self-test, driving RM_ExportDeviceReady
static bool RM_SelfTestDeviceReady( char *outSummary, int summaryLength ) {
	idStr::Copynz( outSummary, "self-test device ready", summaryLength );
	return true;
}

static bool RM_SelfTestDeviceMissing( char *outSummary, int summaryLength ) {
	idStr::Copynz( outSummary, "self-test device missing", summaryLength );
	return false;
}

static bool RM_SelfTestDeviceMissingSilently( char *outSummary, int summaryLength ) {
	(void)outSummary;
	(void)summaryLength;
	return false;
}

/*
====================
RendererModule_RunSelfTest

Pure-logic checks that need no window, device, or module binary: api-string
parsing, module naming, ladder composition, export validation rules, and the
device gate in front of activation.
====================
*/
bool RendererModule_RunSelfTest( void ) {
	int numFailures = 0;

	// api parsing
	{
		rendererModuleApi_t api;
		if ( !R_RendererModule_ParseApi( "gl", api ) || api != RENDER_MODULE_API_GL ) {
			common->Warning( "rendererModuleSelfTest: parse 'gl' failed" );
			numFailures++;
		}
		if ( !R_RendererModule_ParseApi( "VULKAN", api ) || api != RENDER_MODULE_API_VULKAN ) {
			common->Warning( "rendererModuleSelfTest: parse 'VULKAN' failed" );
			numFailures++;
		}
		if ( !R_RendererModule_ParseApi( "vk", api ) || api != RENDER_MODULE_API_VULKAN ) {
			common->Warning( "rendererModuleSelfTest: parse 'vk' alias failed" );
			numFailures++;
		}
		if ( !R_RendererModule_ParseApi( "best", api ) || api != RENDER_MODULE_API_GL ) {
			common->Warning( "rendererModuleSelfTest: 'best' must resolve to gl until promotion evidence lands" );
			numFailures++;
		}
		if ( R_RendererModule_ParseApi( "glide", api ) || api != RENDER_MODULE_API_GL ) {
			common->Warning( "rendererModuleSelfTest: invalid api must be rejected and select gl" );
			numFailures++;
		}
	}

	// module naming
	{
		char name[ MAX_OSPATH ];
		R_RendererModule_BuildBinaryName( RENDER_MODULE_API_VULKAN, name, sizeof( name ) );
		idStr expected = va( "renderer-vk_%s", RENDERER_MODULE_ARCH_TAG );
		if ( expected.Cmp( name ) != 0 ) {
			common->Warning( "rendererModuleSelfTest: vulkan module name '%s', expected '%s'", name, expected.c_str() );
			numFailures++;
		}
		R_RendererModule_BuildBinaryName( RENDER_MODULE_API_GL, name, sizeof( name ) );
		expected = va( "renderer-gl_%s", RENDERER_MODULE_ARCH_TAG );
		if ( expected.Cmp( name ) != 0 ) {
			common->Warning( "rendererModuleSelfTest: gl module name '%s', expected '%s'", name, expected.c_str() );
			numFailures++;
		}

		// a universal2 macOS package carries one two-slice module, so both
		// executable slices must be able to name it
		R_RendererModule_BuildBinaryNameForArch( RENDER_MODULE_API_VULKAN, "universal2", name, sizeof( name ) );
		if ( idStr::Cmp( name, "renderer-vk_universal2" ) != 0 ) {
			common->Warning( "rendererModuleSelfTest: universal2 vulkan module name '%s', expected 'renderer-vk_universal2'", name );
			numFailures++;
		}
	}

	// fallback ladder composition
	{
		rendererModuleApi_t ladder[ RENDER_MODULE_API_COUNT ];
		int numEntries = R_RendererModule_BuildFallbackLadder( RENDER_MODULE_API_VULKAN, ladder, RENDER_MODULE_API_COUNT );
		if ( numEntries != 2 || ladder[ 0 ] != RENDER_MODULE_API_VULKAN || ladder[ 1 ] != RENDER_MODULE_API_GL ) {
			common->Warning( "rendererModuleSelfTest: vulkan ladder must be [vulkan, gl]" );
			numFailures++;
		}
		numEntries = R_RendererModule_BuildFallbackLadder( RENDER_MODULE_API_GL, ladder, RENDER_MODULE_API_COUNT );
		if ( numEntries != 1 || ladder[ 0 ] != RENDER_MODULE_API_GL ) {
			common->Warning( "rendererModuleSelfTest: gl ladder must be [gl]" );
			numFailures++;
		}
	}

	// export validation rules
	{
		bool diagnosticsOnly = false;
		const char *reason = "";

		if ( RM_ValidateExport( NULL, diagnosticsOnly, &reason ) ) {
			common->Warning( "rendererModuleSelfTest: NULL export must be rejected" );
			numFailures++;
		}

		renderExport_t testExport;
		memset( &testExport, 0, sizeof( testExport ) );
		testExport.version = RENDER_API_VERSION + 1;
		testExport.backendName = "vulkan";
		if ( RM_ValidateExport( &testExport, diagnosticsOnly, &reason ) ) {
			common->Warning( "rendererModuleSelfTest: version mismatch must be rejected" );
			numFailures++;
		}

		testExport.version = RENDER_API_VERSION;
		testExport.backendName = NULL;
		if ( RM_ValidateExport( &testExport, diagnosticsOnly, &reason ) ) {
			common->Warning( "rendererModuleSelfTest: missing backend name must be rejected" );
			numFailures++;
		}

		testExport.backendName = "vulkan";
		testExport.renderSystem = NULL;
		if ( !RM_ValidateExport( &testExport, diagnosticsOnly, &reason ) || !diagnosticsOnly ) {
			common->Warning( "rendererModuleSelfTest: bring-up export must validate as diagnostics-only" );
			numFailures++;
		}

		// activation policy: bring-up exports stay diagnostics-only; a full
		// export is activatable now that the Phase B8 seam hosts module
		// renderers (the first-boot activation window is enforced separately
		// in RM_TryLoadModuleApi)
		if ( RM_ExportCanRender( &testExport, &reason ) ) {
			common->Warning( "rendererModuleSelfTest: diagnostics-only export must not be activatable" );
			numFailures++;
		}
		static int dummyRenderSystemStorage;
		testExport.renderSystem = reinterpret_cast<idRenderSystem *>( &dummyRenderSystemStorage );
		if ( !RM_ExportCanRender( &testExport, &reason ) ) {
			common->Warning( "rendererModuleSelfTest: full exports must be activatable with the Phase B8 seam landed" );
			numFailures++;
		}
		testExport.renderSystem = NULL;
	}

	// device gate: a module that exports a device self-test activates only
	// when it passes, and a failure always carries a reason for the fallback
	{
		renderModuleDiagnostics_t testDiagnostics;
		memset( &testDiagnostics, 0, sizeof( testDiagnostics ) );
		renderExport_t testExport;
		memset( &testExport, 0, sizeof( testExport ) );
		char summary[ 128 ];

		if ( !RM_ExportDeviceReady( &testExport, summary, sizeof( summary ) ) ) {
			common->Warning( "rendererModuleSelfTest: a module without diagnostics must not be device-gated" );
			numFailures++;
		}
		testExport.diagnostics = &testDiagnostics;
		if ( !RM_ExportDeviceReady( &testExport, summary, sizeof( summary ) ) ) {
			common->Warning( "rendererModuleSelfTest: a module without a device self-test must not be device-gated" );
			numFailures++;
		}
		testDiagnostics.RunDeviceSelfTest = RM_SelfTestDeviceReady;
		if ( !RM_ExportDeviceReady( &testExport, summary, sizeof( summary ) ) ) {
			common->Warning( "rendererModuleSelfTest: a passing device self-test must allow activation" );
			numFailures++;
		}
		testDiagnostics.RunDeviceSelfTest = RM_SelfTestDeviceMissing;
		if ( RM_ExportDeviceReady( &testExport, summary, sizeof( summary ) )
				|| idStr::Cmp( summary, "self-test device missing" ) != 0 ) {
			common->Warning( "rendererModuleSelfTest: a failed device self-test must refuse activation and keep its summary" );
			numFailures++;
		}
		testDiagnostics.RunDeviceSelfTest = RM_SelfTestDeviceMissingSilently;
		if ( RM_ExportDeviceReady( &testExport, summary, sizeof( summary ) ) || summary[ 0 ] == '\0' ) {
			common->Warning( "rendererModuleSelfTest: a device self-test that fails without a summary must still record a reason" );
			numFailures++;
		}
	}

	// import completeness: every v2 interface pointer and service binding
	// must be filled by the shared builder
	{
		renderImport_t testImport;
		RM_BuildImport( testImport );
		if ( testImport.version != RENDER_API_VERSION ) {
			common->Warning( "rendererModuleSelfTest: import version must be RENDER_API_VERSION" );
			numFailures++;
		}
		if ( testImport.services == NULL
				|| testImport.services->Sleep == NULL
				|| testImport.services->EnterCriticalSection == NULL
				|| testImport.services->LeaveCriticalSection == NULL
				|| testImport.services->IsRenderDocInjected == NULL ) {
			common->Warning( "rendererModuleSelfTest: v2 service bindings incomplete" );
			numFailures++;
		}
		if ( testImport.services == NULL || testImport.services->ResetRenderApiAfterDeviceFailure == NULL ) {
			common->Warning( "rendererModuleSelfTest: v12 device-failure reset service not bound" );
			numFailures++;
		}
		if ( testImport.sys == NULL || testImport.common == NULL || testImport.cvarSystem == NULL
				|| testImport.cmdSystem == NULL || testImport.fileSystem == NULL
				|| testImport.declManager == NULL || testImport.soundSystem == NULL
				|| testImport.session == NULL || testImport.uiManager == NULL
				|| testImport.collisionModelManager == NULL || testImport.eventLoop == NULL
				|| testImport.bse == NULL ) {
			common->Warning( "rendererModuleSelfTest: v2 import interface pointers incomplete" );
			numFailures++;
		}
	}

	if ( numFailures > 0 ) {
		common->Warning( "RendererModule self-test failed: %d failure(s)", numFailures );
		return false;
	}
	common->Printf( "RendererModule self-test passed\n" );
	return true;
}
