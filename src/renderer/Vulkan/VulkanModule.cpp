// Copyright (C) 2026 DarkMatter Productions
//

#include <string.h>

#include "../RenderModuleAPI.h"
#include "VulkanBringup.h"

/*
===============================================================================

	renderer-vk module entry point.

	Phase A bring-up: the module proves the GetRenderAPI handshake and
	exposes device diagnostics, but returns a NULL renderSystem so the
	engine's fail-closed ladder keeps OpenGL visible. Later roadmap phases
	replace the NULL with the full Vulkan idRenderSystem implementation.

===============================================================================
*/

static renderExport_t vk_moduleExport;

static void VK_Module_Shutdown( void ) {
	VK_Bringup_Shutdown();
}

static const renderModuleDiagnostics_t vk_moduleDiagnostics = {
	VK_Bringup_RunProbe,
	VK_Bringup_RunDeviceSelfTest,
};

#if defined( _WIN32 )
	#define RENDER_MODULE_EXPORT __declspec( dllexport )
#else
	#define RENDER_MODULE_EXPORT __attribute__( ( visibility( "default" ) ) )
#endif

extern "C" RENDER_MODULE_EXPORT
renderExport_t *GetRenderAPI( renderImport_t *moduleImport ) {
	memset( &vk_moduleExport, 0, sizeof( vk_moduleExport ) );
	vk_moduleExport.version = RENDER_API_VERSION;
	vk_moduleExport.backendName = "vulkan";
	vk_moduleExport.moduleDescription = "openQ4 native Vulkan renderer (Phase A bring-up: instance/device/memory diagnostics)";
	vk_moduleExport.renderSystem = NULL;	// bring-up: engine must fall back to GL for rendering
	vk_moduleExport.diagnostics = &vk_moduleDiagnostics;
	vk_moduleExport.Shutdown = VK_Module_Shutdown;

	if ( moduleImport != NULL && moduleImport->version == RENDER_API_VERSION ) {
		VK_Bringup_SetServices( moduleImport->services );
	} else {
		VK_Bringup_SetServices( NULL );
	}
	return &vk_moduleExport;
}
