// Copyright (C) 2026 DarkMatter Productions
//

#include "tr_local.h"
#include "RenderModuleAPI.h"
#include "RendererModule.h"

/*
===============================================================================

	Renderer-module selection, loading, and fail-closed fallback.

	See docs/dev/plans/2026-07-16-vulkan-renderer.md.

===============================================================================
*/

extern idCVar r_renderApi;
extern idCVar r_actualRenderApi;

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
} rendererModuleState_t;

static rendererModuleState_t rm_state;

// module binary short tags; indexed by rendererModuleApi_t
static const char *rm_moduleBinaryTags[ RENDER_MODULE_API_COUNT ] = { "gl", "vk" };
static const char *rm_apiNames[ RENDER_MODULE_API_COUNT ] = { "gl", "vulkan" };

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

static const renderModuleServices_t rm_services = {
	RM_Services_Printf,
	RM_Services_Warning,
	RM_Services_Error,
	RM_Services_Milliseconds,
	RM_Services_CVarGetString,
	RM_Services_CVarGetInteger,
	RM_Services_CVarGetBool,
	RM_Services_CVarSetString,
};

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
void R_RendererModule_BuildBinaryName( rendererModuleApi_t api, char *outName, int maxLength ) {
	const char *tag = ( api >= 0 && api < RENDER_MODULE_API_COUNT ) ? rm_moduleBinaryTags[ api ] : "invalid";
	idStr::snPrintf( outName, maxLength, "renderer-%s_%s", tag, RENDERER_MODULE_ARCH_TAG );
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

Renderer modules load only from the executable directory (or the platform
package root), mirroring the trusted-root policy in idFileSystem::FindDLL;
they are never loaded from pak files, fs_savepath, or mod content.
====================
*/
static bool RM_ResolveModulePath( rendererModuleApi_t api, char *outPath, int maxLength ) {
	char	binaryName[ MAX_OSPATH ];
	char	fileName[ MAX_OSPATH ];

	R_RendererModule_BuildBinaryName( api, binaryName, sizeof( binaryName ) );
	sys->DLL_GetFileName( binaryName, fileName, sizeof( fileName ) );

	idStr exeDir = Sys_EXEPath();
	exeDir.StripFilename();

	idStr candidate = exeDir + PATHSEPERATOR_STR + fileName;
	if ( candidate.Length() >= maxLength ) {
		outPath[ 0 ] = '\0';
		return false;
	}
	idStr::Copynz( outPath, candidate.c_str(), maxLength );
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

Activation policy on top of shape validation: until the Phase B module seam
lands, the engine cannot hand the frame loop to a module-provided
idRenderSystem, so even a full export must not be reported as the active
renderer — that would leave r_actualRenderApi/gfxInfo claiming a module
renders while the builtin GL path draws every frame. Shared with the
self-test so the policy cannot silently drift when Phase B flips it.
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
	// Phase B (docs/dev/plans/2026-07-16-vulkan-renderer.md) replaces this
	// constant refusal with the real renderSystem handoff
	*reason = "engine does not yet host full renderer modules (Phase B seam pending)";
	return false;
}

/*
====================
RM_UnloadModule
====================
*/
static void RM_UnloadModule( void ) {
	if ( rm_state.moduleExportValid && rm_state.moduleExport.Shutdown != NULL ) {
		rm_state.moduleExport.Shutdown();
	}
	rm_state.moduleExportValid = false;
	memset( &rm_state.moduleExport, 0, sizeof( rm_state.moduleExport ) );
	if ( rm_state.moduleHandle != 0 ) {
		Sys_DLL_Unload( rm_state.moduleHandle );
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
	if ( api == RENDER_MODULE_API_GL ) {
		// the OpenGL renderer is statically linked until the Phase B module
		// seam lands; activating it is always possible
		status.activeApi = RENDER_MODULE_API_GL;
		status.disposition = ( status.requestedApi == RENDER_MODULE_API_GL ) ?
				RENDER_MODULE_DISPOSITION_BUILTIN : RENDER_MODULE_DISPOSITION_FALLBACK;
		return true;
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

	GetRenderAPI_t GetRenderAPI = ( GetRenderAPI_t )Sys_DLL_GetProcAddress( handle, RENDER_API_ENTRY_POINT );
	if ( GetRenderAPI == NULL ) {
		common->Warning( "renderer module '%s' has no %s entry point", modulePath, RENDER_API_ENTRY_POINT );
		Sys_DLL_Unload( handle );
		RM_AppendFallbackReason( status, "missing GetRenderAPI entry point" );
		return false;
	}

	renderImport_t moduleImport;
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
		Sys_DLL_Unload( handle );
		RM_AppendFallbackReason( status, reason );
		return false;
	}

	rm_state.moduleHandle = handle;
	rm_state.moduleExport = *moduleExport;
	rm_state.moduleExportValid = true;
	status.activeApi = api;
	status.disposition = RENDER_MODULE_DISPOSITION_MODULE;
	return true;
}

/*
====================
R_RendererModule_Boot
====================
*/
void R_RendererModule_Boot( void ) {
	rendererModuleStatus_t &status = rm_state.status;

	RM_UnloadModule();
	memset( &status, 0, sizeof( status ) );

	const char *requestedValue = r_renderApi.GetString();
	idStr::Copynz( status.requestedValue, requestedValue, sizeof( status.requestedValue ) );

	rendererModuleApi_t requestedApi;
	if ( !R_RendererModule_ParseApi( requestedValue, requestedApi ) ) {
		common->Warning( "r_renderApi '%s' is not a valid rendering API; using 'gl'", requestedValue );
	}
	status.requestedApi = requestedApi;

	rendererModuleApi_t ladder[ RENDER_MODULE_API_COUNT ];
	const int numCandidates = R_RendererModule_BuildFallbackLadder( requestedApi, ladder, RENDER_MODULE_API_COUNT );

	bool activated = false;
	for ( int i = 0; i < numCandidates; i++ ) {
		if ( RM_TryLoadModuleApi( ladder[ i ], status ) ) {
			activated = true;
			break;
		}
	}
	if ( !activated ) {
		// the GL tail of the ladder cannot fail above; guard anyway
		common->FatalError( "no renderer path could be activated (requested '%s')", requestedValue );
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
====================
*/
bool R_RendererModule_RunVulkanProbe( bool verbose ) {
	char modulePath[ 1024 ];

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

	bool probePassed = false;
	GetRenderAPI_t GetRenderAPI = ( GetRenderAPI_t )Sys_DLL_GetProcAddress( handle, RENDER_API_ENTRY_POINT );
	if ( GetRenderAPI == NULL ) {
		common->Printf( "rendererVkProbe: module has no %s entry point\n", RENDER_API_ENTRY_POINT );
	} else {
		renderImport_t moduleImport;
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

	Sys_DLL_Unload( handle );
	return probePassed;
}

/*
====================
RendererModule_RunSelfTest

Pure-logic checks that need no window, device, or module binary: api-string
parsing, module naming, ladder composition, and export validation rules.
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

		// activation policy: no export may become the active renderer until
		// the Phase B seam can actually hand it the frame loop; when Phase B
		// flips RM_ExportCanRender, this check must be updated deliberately
		if ( RM_ExportCanRender( &testExport, &reason ) ) {
			common->Warning( "rendererModuleSelfTest: diagnostics-only export must not be activatable" );
			numFailures++;
		}
		static int dummyRenderSystemStorage;
		testExport.renderSystem = reinterpret_cast<idRenderSystem *>( &dummyRenderSystemStorage );
		if ( RM_ExportCanRender( &testExport, &reason ) ) {
			common->Warning( "rendererModuleSelfTest: full exports must not activate before the Phase B module seam lands" );
			numFailures++;
		}
		testExport.renderSystem = NULL;
	}

	if ( numFailures > 0 ) {
		common->Warning( "RendererModule self-test failed: %d failure(s)", numFailures );
		return false;
	}
	common->Printf( "RendererModule self-test passed\n" );
	return true;
}
