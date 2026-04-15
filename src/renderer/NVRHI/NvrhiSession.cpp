#include "NvrhiSession.h"

#include <string>

#include "NvrhiBootstrap.h"
#include "NvrhiError.h"
#include "../../sys/GraphicsWindow.h"

#include <SDL3/SDL.h>

namespace {

struct openq4NvrhiBootstrapSessionState_t {
	bool active = false;
	bool videoWasInitialized = false;
	bool preparedWindowRuntime = false;
	bool hidden = false;
	SDL_Window *window = NULL;
	unsigned int windowId = 0;
	idNvrhiBootstrapBackend *backend = NULL;
	openq4GraphicsApi_t resolvedApi = openq4GraphicsApi_t::Auto;
	int renderedFrames = 0;
	Uint64 startCounter = 0;
	double counterFrequency = 1.0;
};

static openq4NvrhiBootstrapSessionState_t g_nvrhiBootstrapSession;

static void OpenQ4_ResetNvrhiBootstrapSessionState( openq4NvrhiBootstrapSessionState_t &session ) {
	session.active = false;
	session.videoWasInitialized = false;
	session.preparedWindowRuntime = false;
	session.hidden = false;
	session.window = NULL;
	session.windowId = 0;
	session.backend = NULL;
	session.resolvedApi = openq4GraphicsApi_t::Auto;
	session.renderedFrames = 0;
	session.startCounter = 0;
	session.counterFrequency = 1.0;
}

} // namespace

bool OpenQ4_StartNvrhiBootstrapSession(
	openq4GraphicsApi_t requestedApi,
	int width,
	int height,
	bool hidden,
	bool vsync,
	openq4GraphicsApi_t &resolvedApi,
	const char *&error ) {
	OpenQ4_StopNvrhiBootstrapSession();
	resolvedApi = requestedApi;

	if ( resolvedApi == openq4GraphicsApi_t::Auto ) {
		resolvedApi = OpenQ4_NvrhiBootstrapGetDefaultApi();
	}

	if ( resolvedApi == openq4GraphicsApi_t::Auto ) {
		error = OpenQ4_NvrhiMakeError( "No compiled NVRHI bootstrap backend is available in this build." );
		return false;
	}
	g_nvrhiBootstrapSession.resolvedApi = resolvedApi;

	if ( width <= 0 || height <= 0 ) {
		error = OpenQ4_NvrhiMakeError( "NVRHI bootstrap sessions require positive window dimensions." );
		return false;
	}

	if ( !OpenQ4_EnsureGraphicsVideoSubsystem( g_nvrhiBootstrapSession.videoWasInitialized, error ) ) {
		OpenQ4_StopNvrhiBootstrapSession();
		return false;
	}

	if ( !OpenQ4_PrepareGraphicsWindowRuntime( resolvedApi, g_nvrhiBootstrapSession.preparedWindowRuntime, error ) ) {
		OpenQ4_StopNvrhiBootstrapSession();
		return false;
	}

	SDL_WindowFlags windowFlags = 0;
	if ( hidden ) {
		windowFlags |= SDL_WINDOW_HIDDEN;
	}

	const std::string windowTitle = std::string( "OpenQ4 Graphics API Probe - " ) + OpenQ4_GraphicsApiName( resolvedApi );
	if ( !OpenQ4_CreateGraphicsWindow(
		windowTitle.c_str(),
		resolvedApi,
		width,
		height,
		windowFlags,
		g_nvrhiBootstrapSession.window,
		error ) ) {
		OpenQ4_StopNvrhiBootstrapSession();
		return false;
	}
	if ( !hidden ) {
		OpenQ4_PositionSecondaryGraphicsWindow( g_nvrhiBootstrapSession.window );
	}
	g_nvrhiBootstrapSession.hidden = hidden;
	g_nvrhiBootstrapSession.windowId = static_cast<unsigned int>( SDL_GetWindowID( g_nvrhiBootstrapSession.window ) );

	g_nvrhiBootstrapSession.backend = OpenQ4_CreateNvrhiBootstrapBackend( resolvedApi, error );
	if ( g_nvrhiBootstrapSession.backend == NULL ) {
		OpenQ4_StopNvrhiBootstrapSession();
		return false;
	}

	openq4NvrhiBootstrapOptions_t bootstrapOptions;
	bootstrapOptions.api = resolvedApi;
	bootstrapOptions.width = width;
	bootstrapOptions.height = height;
	bootstrapOptions.hidden = hidden;
	bootstrapOptions.vsync = vsync;

	if ( !g_nvrhiBootstrapSession.backend->Initialize( g_nvrhiBootstrapSession.window, bootstrapOptions, error ) ) {
		OpenQ4_StopNvrhiBootstrapSession();
		return false;
	}

	g_nvrhiBootstrapSession.renderedFrames = 0;
	g_nvrhiBootstrapSession.startCounter = SDL_GetPerformanceCounter();
	g_nvrhiBootstrapSession.counterFrequency = static_cast<double>( SDL_GetPerformanceFrequency() );
	if ( g_nvrhiBootstrapSession.counterFrequency <= 0.0 ) {
		g_nvrhiBootstrapSession.counterFrequency = 1.0;
	}
	g_nvrhiBootstrapSession.active = true;

	return true;
}

bool OpenQ4_TickNvrhiBootstrapSession( const char *&error ) {
	if ( !g_nvrhiBootstrapSession.active || g_nvrhiBootstrapSession.backend == NULL ) {
		return true;
	}

	SDL_PumpEvents();

	const Uint64 nowCounter = SDL_GetPerformanceCounter();
	const double timeSeconds = static_cast<double>( nowCounter - g_nvrhiBootstrapSession.startCounter ) / g_nvrhiBootstrapSession.counterFrequency;
	if ( !g_nvrhiBootstrapSession.backend->RenderFrame( timeSeconds, error ) ) {
		return false;
	}

	g_nvrhiBootstrapSession.renderedFrames++;
	return true;
}

void OpenQ4_StopNvrhiBootstrapSession( void ) {
	if ( g_nvrhiBootstrapSession.backend != NULL ) {
		g_nvrhiBootstrapSession.backend->Shutdown();
		delete g_nvrhiBootstrapSession.backend;
		g_nvrhiBootstrapSession.backend = NULL;
	}

	OpenQ4_DestroyGraphicsWindow( g_nvrhiBootstrapSession.window );

	OpenQ4_ReleaseGraphicsWindowRuntime( g_nvrhiBootstrapSession.resolvedApi, g_nvrhiBootstrapSession.preparedWindowRuntime );
	g_nvrhiBootstrapSession.preparedWindowRuntime = false;

	OpenQ4_ReleaseGraphicsVideoSubsystem( g_nvrhiBootstrapSession.videoWasInitialized );

	OpenQ4_ResetNvrhiBootstrapSessionState( g_nvrhiBootstrapSession );
}

bool OpenQ4_IsNvrhiBootstrapSessionActive( void ) {
	return g_nvrhiBootstrapSession.active;
}

openq4GraphicsApi_t OpenQ4_GetNvrhiBootstrapSessionApi( void ) {
	return g_nvrhiBootstrapSession.resolvedApi;
}

int OpenQ4_GetNvrhiBootstrapSessionFrameCount( void ) {
	return g_nvrhiBootstrapSession.renderedFrames;
}

const char *OpenQ4_GetNvrhiBootstrapSessionBackendName( void ) {
	if ( !g_nvrhiBootstrapSession.active || g_nvrhiBootstrapSession.backend == NULL ) {
		return NULL;
	}

	return g_nvrhiBootstrapSession.backend->GetName();
}

bool OpenQ4_IsNvrhiBootstrapSessionHidden( void ) {
	return g_nvrhiBootstrapSession.hidden;
}

unsigned int OpenQ4_GetNvrhiBootstrapSessionWindowId( void ) {
	return g_nvrhiBootstrapSession.windowId;
}

bool OpenQ4_GetNvrhiBootstrapSessionWindowSizeInPixels( int &width, int &height ) {
	const char *error = NULL;

	if ( g_nvrhiBootstrapSession.window == NULL ) {
		width = 0;
		height = 0;
		return false;
	}

	return OpenQ4_GetGraphicsWindowSizeInPixels( g_nvrhiBootstrapSession.window, width, height, error );
}
