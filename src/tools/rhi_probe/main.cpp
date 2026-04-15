#include <cstdio>
#include <string>

#include <SDL3/SDL.h>

#include "NvrhiProbe.h"
#include "renderer/NVRHI/NvrhiSession.h"

int main( int argc, char **argv ) {
	nvrhiProbeOptions_t options;
	std::string parseError;
	if ( !NvrhiProbe_ParseOptions( argc, argv, options, parseError ) ) {
		std::fprintf( stderr, "Error: %s\n\n", parseError.c_str() );
		NvrhiProbe_PrintUsage();
		return 1;
	}

	if ( options.showUsage ) {
		NvrhiProbe_PrintUsage();
		return 0;
	}

	const char *error = NULL;
	openq4GraphicsApi_t resolvedApi = openq4GraphicsApi_t::Auto;
	if ( !OpenQ4_StartNvrhiBootstrapSession( options.api, options.width, options.height, options.hidden, options.vsync, resolvedApi, error ) ) {
		std::fprintf( stderr, "Backend initialization failed: %s\n", error != NULL ? error : "Unknown error." );
		return 1;
	}

	std::printf( "Running %s probe (%s, %s)\n",
		OpenQ4_GetNvrhiBootstrapSessionBackendName(),
		options.hidden ? "hidden window" : "visible window",
		options.vsync ? "vsync" : "no vsync" );
	int sessionWidth = 0;
	int sessionHeight = 0;
	if ( OpenQ4_GetNvrhiBootstrapSessionWindowSizeInPixels( sessionWidth, sessionHeight ) ) {
		std::printf( "Probe window size: %dx%d pixels\n", sessionWidth, sessionHeight );
	}

	bool quit = false;

	while ( !quit ) {
		SDL_Event event;
		while ( SDL_PollEvent( &event ) ) {
			switch ( event.type ) {
				case SDL_EVENT_QUIT:
				case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
					quit = true;
					break;

				case SDL_EVENT_KEY_DOWN:
					if ( event.key.key == SDLK_ESCAPE ) {
						quit = true;
					}
					break;

				default:
					break;
			}
		}

		if ( options.frames > 0 && OpenQ4_GetNvrhiBootstrapSessionFrameCount() >= options.frames ) {
			break;
		}

		if ( !OpenQ4_TickNvrhiBootstrapSession( error ) ) {
			std::fprintf( stderr, "Render failure after %d frame(s): %s\n", OpenQ4_GetNvrhiBootstrapSessionFrameCount(), error != NULL ? error : "Unknown error." );
			OpenQ4_StopNvrhiBootstrapSession();
			return 1;
		}

		if ( options.hidden && options.frames == 0 ) {
			SDL_Delay( 1 );
		}
	}

	const int renderedFrames = OpenQ4_GetNvrhiBootstrapSessionFrameCount();
	OpenQ4_StopNvrhiBootstrapSession();
	std::printf( "Probe completed successfully after %d frame(s).\n", renderedFrames );
	return 0;
}
