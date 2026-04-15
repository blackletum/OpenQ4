#include "NvrhiCommand.h"

#include <string>

#include "NvrhiBootstrap.h"
#include "NvrhiError.h"
#include "NvrhiSession.h"
#include "../../sys/GraphicsWindow.h"

namespace {

static void OpenQ4_GetDefaultBootstrapWindowSize( int &width, int &height ) {
	width = 1280;
	height = 720;

	const char *error = NULL;
	int primaryWidth = 0;
	int primaryHeight = 0;
	if ( OpenQ4_GetPrimaryGraphicsWindowSizeInPixels( primaryWidth, primaryHeight, error ) && primaryWidth > 0 && primaryHeight > 0 ) {
		width = primaryWidth;
		height = primaryHeight;
	}
}

} // namespace

bool OpenQ4_RunNvrhiBootstrapFrames(
	openq4GraphicsApi_t requestedApi,
	int frames,
	bool hidden,
	bool vsync,
	openq4GraphicsApi_t &resolvedApi,
	const char *&error ) {
	resolvedApi = requestedApi;

	if ( resolvedApi == openq4GraphicsApi_t::Auto ) {
		resolvedApi = OpenQ4_NvrhiBootstrapGetDefaultApi();
	}

	if ( resolvedApi == openq4GraphicsApi_t::Auto ) {
		error = OpenQ4_NvrhiMakeError( "No compiled NVRHI bootstrap backend is available in this build." );
		return false;
	}

	if ( frames <= 0 ) {
		error = OpenQ4_NvrhiMakeError( "NVRHI bootstrap smoke tests require a positive frame count." );
		return false;
	}

	int width = 1280;
	int height = 720;
	OpenQ4_GetDefaultBootstrapWindowSize( width, height );

	if ( !OpenQ4_StartNvrhiBootstrapSession( resolvedApi, width, height, hidden, vsync, resolvedApi, error ) ) {
		return false;
	}

	for ( int frame = 0; frame < frames; ++frame ) {
		if ( !OpenQ4_TickNvrhiBootstrapSession( error ) ) {
			OpenQ4_StopNvrhiBootstrapSession();
			return false;
		}
	}

	OpenQ4_StopNvrhiBootstrapSession();
	return true;
}
