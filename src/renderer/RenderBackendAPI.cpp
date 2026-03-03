/*
===========================================================================

OpenQ4 - Renderer backend abstraction seam (Phase 7)

===========================================================================
*/

#include "tr_local.h"

#if defined( _WIN32 )
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static idStr rbGraphicsBackendRequested = "auto";
static idStr rbGraphicsBackendActive = "uninitialized";
static bool rbGraphicsVulkanBootstrap = false;
static bool rbGraphicsVulkanRuntimeAvailable = false;

static void R_UpdateActiveBackendCvar( const char* value ) {
	if ( value == NULL || value[0] == '\0' ) {
		value = "uninitialized";
	}
	r_activeGraphicsAPI.SetString( value );
	r_activeGraphicsAPI.ClearModified();
}

static bool R_ShouldAttemptVulkanBootstrap() {
	return ( idStr::Icmp( r_graphicsAPI.GetString(), "vulkan" ) == 0 ) ||
		( idStr::Icmp( r_graphicsAPI.GetString(), "auto" ) == 0 );
}

static bool R_IsVulkanRequired() {
	return ( idStr::Icmp( r_graphicsAPI.GetString(), "vulkan" ) == 0 ) &&
		r_requireVulkanBootstrap.GetBool();
}

static bool R_TryLoadVulkanRuntime() {
#if defined( ID_DEDICATED )
	return false;
#elif defined( _WIN32 )
	HMODULE module = LoadLibraryA( "vulkan-1.dll" );
	if ( module != NULL ) {
		FreeLibrary( module );
		return true;
	}
	return false;
#else
	const char* candidates[] = {
#if defined( __APPLE__ )
		"libvulkan.1.dylib",
		"libvulkan.dylib",
		"libMoltenVK.dylib",
#else
		"libvulkan.so.1",
		"libvulkan.so",
#endif
		NULL
	};

	for ( int i = 0; candidates[ i ] != NULL; ++i ) {
		void* handle = dlopen( candidates[ i ], RTLD_NOW | RTLD_LOCAL );
		if ( handle != NULL ) {
			dlclose( handle );
			return true;
		}
	}
	return false;
#endif
}

void R_ResetGraphicsBackendState( void ) {
	rbGraphicsBackendRequested = "auto";
	rbGraphicsBackendActive = "uninitialized";
	rbGraphicsVulkanBootstrap = false;
	rbGraphicsVulkanRuntimeAvailable = false;
	R_UpdateActiveBackendCvar( rbGraphicsBackendActive.c_str() );
}

bool R_InitGraphicsBackend( glimpParms_t parms ) {
	rbGraphicsBackendRequested = r_graphicsAPI.GetString();
	rbGraphicsVulkanBootstrap = false;
	rbGraphicsVulkanRuntimeAvailable = false;

	const bool wantsVulkanBootstrap = R_ShouldAttemptVulkanBootstrap();
	if ( wantsVulkanBootstrap ) {
		rbGraphicsVulkanRuntimeAvailable = R_TryLoadVulkanRuntime();
		if ( rbGraphicsVulkanRuntimeAvailable ) {
			rbGraphicsVulkanBootstrap = true;
			common->Printf( "phase7_backend: Vulkan runtime detected, bootstrap path enabled.\n" );
		} else if ( R_IsVulkanRequired() ) {
			common->FatalError( "phase7_backend: r_graphicsAPI=vulkan requested with r_requireVulkanBootstrap=1, but no Vulkan runtime loader was found." );
		} else {
			common->Printf( "phase7_backend: Vulkan runtime not detected, falling back to OpenGL backend.\n" );
		}
	}

	if ( !GLimp_Init( parms ) ) {
		rbGraphicsBackendActive = "none";
		R_UpdateActiveBackendCvar( rbGraphicsBackendActive.c_str() );
		return false;
	}

	rbGraphicsBackendActive = rbGraphicsVulkanBootstrap ? "vulkan-bootstrap+opengl" : "opengl";
	R_UpdateActiveBackendCvar( rbGraphicsBackendActive.c_str() );

	if ( r_showRenderBackend.GetBool() ) {
		R_PrintGraphicsBackendInfo();
	}

	return true;
}

void R_ShutdownGraphicsBackend( void ) {
	GLimp_Shutdown();
	rbGraphicsBackendActive = "uninitialized";
	rbGraphicsVulkanBootstrap = false;
	rbGraphicsVulkanRuntimeAvailable = false;
	R_UpdateActiveBackendCvar( rbGraphicsBackendActive.c_str() );
}

void R_SwapGraphicsBackendBuffers( void ) {
	GLimp_SwapBuffers();
}

const char* R_GetGraphicsBackendName( void ) {
	return rbGraphicsBackendActive.c_str();
}

const char* R_GetGraphicsBackendRequested( void ) {
	return rbGraphicsBackendRequested.c_str();
}

bool R_IsVulkanBootstrapActive( void ) {
	return rbGraphicsVulkanBootstrap;
}

void R_PrintGraphicsBackendInfo( void ) {
	common->Printf( "phase7_backend: requested=%s active=%s vulkanRuntime=%s vulkanBootstrap=%s\n",
		rbGraphicsBackendRequested.c_str(),
		rbGraphicsBackendActive.c_str(),
		rbGraphicsVulkanRuntimeAvailable ? "available" : "missing",
		rbGraphicsVulkanBootstrap ? "enabled" : "disabled" );
}
