// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __RENDERMODULEAPI_H__
#define __RENDERMODULEAPI_H__

/*
===============================================================================

	Renderer module ABI.

	Renderer backends (renderer-gl_<arch>, renderer-vk_<arch>) are dynamic
	modules loaded from the executable/package root and selected with the
	r_renderApi cvar. The handshake mirrors the game-module GetGameAPI
	convention: a single versioned extern "C" entry point exchanging import
	and export structs.

	This header must compile both inside engine translation units and inside
	standalone renderer-module translation units that do not link idlib, so
	everything a bring-up module needs is expressed through the plain C
	services table. The full C++ engine interfaces are also carried (as
	opaque pointers to a standalone module) for full renderer modules that
	compile against the engine headers.

	See docs/dev/plans/2026-07-16-vulkan-renderer.md for the roadmap.

===============================================================================
*/

// version history:
//  1 - Phase A bring-up handshake (services table, diagnostics surface)
//  2 - Phase B4 import/export completion: session/uiManager/
//      collisionModelManager/bse/eventLoop imports, renderModelManager
//      export, sleep/critical-section/RenderDoc services
#define RENDER_API_VERSION			2
#define RENDER_API_ENTRY_POINT		"GetRenderAPI"

class idSys;
class idCommon;
class idCVarSystem;
class idCmdSystem;
class idFileSystem;
class idDeclManager;
class idSoundSystem;
class idRenderSystem;
class idRenderModelManager;
class idSession;
class idUserInterfaceManager;
class idCollisionModelManager;
class idEventLoop;
class rvBSEManager;

// bring-up-safe engine services; every callback is bound engine-side and
// remains valid for the lifetime of the loaded module
typedef struct renderModuleServices_s {
	void			( *Printf )( const char *fmt, ... );
	void			( *Warning )( const char *fmt, ... );
	void			( *Error )( const char *fmt, ... );		// does not return
	int				( *Milliseconds )( void );
	const char *	( *CVar_GetString )( const char *name );
	int				( *CVar_GetInteger )( const char *name );
	bool			( *CVar_GetBool )( const char *name );
	void			( *CVar_SetString )( const char *name, const char *value );

	// --- version 2 ---
	void			( *Sleep )( int msec );
	// engine critical-section slots (CRITICAL_SECTION_* indices)
	void			( *EnterCriticalSection )( int index );
	void			( *LeaveCriticalSection )( int index );
	bool			( *IsRenderDocInjected )( void );
	// worker-thread creation intentionally not carried yet: the light-grid
	// bake pool's Sys_CreateThread signature (thread registry, priority,
	// threadInfo_t) is resolved by the module-side forwarder design in
	// Phase B8 (docs/dev/plans/2026-07-16-vulkan-renderer-phase-b.md)
} renderModuleServices_t;

// native window/surface handoff; zeroed until the engine has created a window
typedef struct renderModuleWindowInfo_s {
	bool			hasWindow;
	void *			nativeWindowHandle;		// HWND / X11 Window / wl_surface* / NSWindow*
	void *			nativeDisplayHandle;	// HDC / X11 Display* / wl_display*
	void *			sdlWindow;				// SDL_Window* when the SDL3 backend is active
	int				pixelWidth;
	int				pixelHeight;
} renderModuleWindowInfo_t;

typedef struct renderImport_s {
	int										version;		// RENDER_API_VERSION
	const renderModuleServices_t *			services;
	renderModuleWindowInfo_t				window;

	// full engine interfaces for full renderer modules; a standalone bring-up
	// module treats these as opaque
	idSys *									sys;
	idCommon *								common;
	idCVarSystem *							cvarSystem;
	idCmdSystem *							cmdSystem;
	idFileSystem *							fileSystem;
	idDeclManager *							declManager;
	idSoundSystem *							soundSystem;

	// --- version 2: the remaining engine interfaces the renderer sources
	// bind as globals inside the module (game-DLL convention) ---
	idSession *								session;		// demo IO, pacifier, rw/sw
	idUserInterfaceManager *				uiManager;
	idCollisionModelManager *				collisionModelManager;
	idEventLoop *							eventLoop;		// frame-time queries
	rvBSEManager *							bse;			// effects system
} renderImport_t;

// diagnostics surface, valid even while the module cannot yet provide a full
// idRenderSystem; drives rendererVkProbe and module self-tests
typedef struct renderModuleDiagnostics_s {
	// prints a full bring-up/probe report through services->Printf;
	// returns false when any required step fails
	bool			( *RunProbe )( bool verbose );
	// quiet pass/fail for self-test harnesses; writes a one-line summary
	bool			( *RunDeviceSelfTest )( char *outSummary, int summaryLength );
} renderModuleDiagnostics_t;

typedef struct renderExport_s {
	int										version;		// RENDER_API_VERSION
	const char *							backendName;	// "gl" / "vulkan" -> r_actualRenderApi
	const char *							moduleDescription;
	// full renderer interface; NULL while the module is bring-up/diagnostics
	// only, which the loader treats as an instruction to fall back
	idRenderSystem *						renderSystem;
	// version 2: published alongside renderSystem when a module activates
	// (the engine forwards it into gameImport for the game DLLs)
	idRenderModelManager *					renderModelManager;
	const renderModuleDiagnostics_t *		diagnostics;
	// releases module resources; must be safe to call before Sys_DLL_Unload
	// whenever GetRenderAPI has been called
	void			( *Shutdown )( void );
} renderExport_t;

extern "C" {
typedef renderExport_t * ( *GetRenderAPI_t )( renderImport_t *import );
}

#endif /* !__RENDERMODULEAPI_H__ */
