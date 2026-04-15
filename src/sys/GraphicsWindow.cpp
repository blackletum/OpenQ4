#include "GraphicsWindow.h"

#include <SDL3/SDL_vulkan.h>

namespace {

static SDL_Window *g_openq4PrimaryGraphicsWindow = NULL;

static const char *OpenQ4_GraphicsWindowMakeError( const char *prefix, const char *detail = nullptr ) {
	static thread_local char storage[ 512 ];
	if ( detail != nullptr && detail[ 0 ] != '\0' ) {
		SDL_snprintf( storage, sizeof( storage ), "%s%s", prefix, detail );
	} else {
		SDL_snprintf( storage, sizeof( storage ), "%s", prefix );
	}
	return storage;
}

static bool OpenQ4_GetWindowPlacementBounds( SDL_Window *window, SDL_Rect &bounds ) {
	SDL_DisplayID display = 0;

	if ( window != NULL ) {
		display = SDL_GetDisplayForWindow( window );
	}
	if ( display == 0 ) {
		display = SDL_GetPrimaryDisplay();
	}
	if ( display != 0 && SDL_GetDisplayUsableBounds( display, &bounds ) ) {
		return true;
	}
	if ( display != 0 && SDL_GetDisplayBounds( display, &bounds ) ) {
		return true;
	}

	return false;
}

static bool OpenQ4_RectFitsWithinBounds( const SDL_Rect &rect, const SDL_Rect &bounds ) {
	return rect.x >= bounds.x && rect.y >= bounds.y &&
		rect.x + rect.w <= bounds.x + bounds.w &&
		rect.y + rect.h <= bounds.y + bounds.h;
}

static void OpenQ4_ClampRectToBounds( SDL_Rect &rect, const SDL_Rect &bounds ) {
	const int maxX = bounds.x + ( bounds.w > rect.w ? bounds.w - rect.w : 0 );
	const int maxY = bounds.y + ( bounds.h > rect.h ? bounds.h - rect.h : 0 );

	if ( rect.x < bounds.x ) {
		rect.x = bounds.x;
	} else if ( rect.x > maxX ) {
		rect.x = maxX;
	}

	if ( rect.y < bounds.y ) {
		rect.y = bounds.y;
	} else if ( rect.y > maxY ) {
		rect.y = maxY;
	}
}

} // namespace

SDL_WindowFlags OpenQ4_GetGraphicsWindowFlags( openq4GraphicsApi_t api ) {
	SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

	switch ( api ) {
		case openq4GraphicsApi_t::OpenGL:
			flags |= SDL_WINDOW_OPENGL;
			break;
		case openq4GraphicsApi_t::Vulkan:
			flags |= SDL_WINDOW_VULKAN;
			break;
		default:
			break;
	}

	return flags;
}

bool OpenQ4_EnsureGraphicsVideoSubsystem( bool &initializedHere, const char *&error ) {
	initializedHere = false;

	if ( ( SDL_WasInit( SDL_INIT_VIDEO ) & SDL_INIT_VIDEO ) != 0 ) {
		return true;
	}

	if ( SDL_InitSubSystem( SDL_INIT_VIDEO ) != 0 ) {
		error = OpenQ4_GraphicsWindowMakeError( "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: ", SDL_GetError() );
		return false;
	}

	initializedHere = true;
	return true;
}

void OpenQ4_ReleaseGraphicsVideoSubsystem( bool initializedHere ) {
	if ( initializedHere && ( SDL_WasInit( SDL_INIT_VIDEO ) & SDL_INIT_VIDEO ) != 0 ) {
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
	}
}

bool OpenQ4_PrepareGraphicsWindowRuntime( openq4GraphicsApi_t api, bool &preparedRuntime, const char *&error ) {
	preparedRuntime = false;

	switch ( api ) {
		case openq4GraphicsApi_t::Vulkan:
			if ( !SDL_Vulkan_LoadLibrary( NULL ) ) {
				error = OpenQ4_GraphicsWindowMakeError( "SDL_Vulkan_LoadLibrary failed: ", SDL_GetError() );
				return false;
			}
			preparedRuntime = true;
			return true;

		default:
			return true;
	}
}

void OpenQ4_ReleaseGraphicsWindowRuntime( openq4GraphicsApi_t api, bool preparedRuntime ) {
	if ( api == openq4GraphicsApi_t::Vulkan && preparedRuntime ) {
		SDL_Vulkan_UnloadLibrary();
	}
}

bool OpenQ4_GetGraphicsWindowSizeInPixels( SDL_Window *window, int &width, int &height, const char *&error ) {
	width = 0;
	height = 0;

	if ( window == NULL ) {
		error = OpenQ4_GraphicsWindowMakeError( "OpenQ4_GetGraphicsWindowSizeInPixels received a null SDL window." );
		return false;
	}

	if ( !SDL_GetWindowSizeInPixels( window, &width, &height ) ) {
		error = OpenQ4_GraphicsWindowMakeError( "SDL_GetWindowSizeInPixels failed: ", SDL_GetError() );
		return false;
	}

	return true;
}

bool OpenQ4_CreateGraphicsWindow(
	const char *title,
	openq4GraphicsApi_t api,
	int width,
	int height,
	SDL_WindowFlags extraFlags,
	SDL_Window *&window,
	const char *&error ) {
	window = NULL;

	if ( title == NULL || title[ 0 ] == '\0' ) {
		error = OpenQ4_GraphicsWindowMakeError( "OpenQ4_CreateGraphicsWindow requires a non-empty title." );
		return false;
	}
	if ( width <= 0 || height <= 0 ) {
		error = OpenQ4_GraphicsWindowMakeError( "OpenQ4_CreateGraphicsWindow requires positive dimensions." );
		return false;
	}

	window = SDL_CreateWindow( title, width, height, OpenQ4_GetGraphicsWindowFlags( api ) | extraFlags );
	if ( window == NULL ) {
		error = OpenQ4_GraphicsWindowMakeError( "SDL_CreateWindow failed: ", SDL_GetError() );
		return false;
	}

	return true;
}

void OpenQ4_SetPrimaryGraphicsWindow( SDL_Window *window ) {
	g_openq4PrimaryGraphicsWindow = window;
}

SDL_Window *OpenQ4_GetPrimaryGraphicsWindow( void ) {
	return g_openq4PrimaryGraphicsWindow;
}

bool OpenQ4_GetPrimaryGraphicsWindowSizeInPixels( int &width, int &height, const char *&error ) {
	return OpenQ4_GetGraphicsWindowSizeInPixels( g_openq4PrimaryGraphicsWindow, width, height, error );
}

void OpenQ4_PositionSecondaryGraphicsWindow( SDL_Window *window ) {
	if ( window == NULL || window == g_openq4PrimaryGraphicsWindow || g_openq4PrimaryGraphicsWindow == NULL ) {
		return;
	}

	int primaryX = 0;
	int primaryY = 0;
	int primaryWidth = 0;
	int primaryHeight = 0;
	int windowWidth = 0;
	int windowHeight = 0;

	if ( !SDL_GetWindowPosition( g_openq4PrimaryGraphicsWindow, &primaryX, &primaryY ) ||
		!SDL_GetWindowSize( g_openq4PrimaryGraphicsWindow, &primaryWidth, &primaryHeight ) ||
		!SDL_GetWindowSize( window, &windowWidth, &windowHeight ) ||
		primaryWidth <= 0 || primaryHeight <= 0 || windowWidth <= 0 || windowHeight <= 0 ) {
		return;
	}

	static const int secondaryWindowSpacing = 32;

	SDL_Rect targetRect = { primaryX + secondaryWindowSpacing, primaryY + secondaryWindowSpacing, windowWidth, windowHeight };
	SDL_Rect bounds;
	const bool haveBounds = OpenQ4_GetWindowPlacementBounds( g_openq4PrimaryGraphicsWindow, bounds );

	const SDL_Rect candidateRects[] = {
		{ primaryX + primaryWidth + secondaryWindowSpacing, primaryY, windowWidth, windowHeight },
		{ primaryX - windowWidth - secondaryWindowSpacing, primaryY, windowWidth, windowHeight },
		{ primaryX, primaryY + primaryHeight + secondaryWindowSpacing, windowWidth, windowHeight },
		{ primaryX + secondaryWindowSpacing, primaryY + secondaryWindowSpacing, windowWidth, windowHeight },
	};

	for ( const SDL_Rect &candidateRect : candidateRects ) {
		if ( !haveBounds || OpenQ4_RectFitsWithinBounds( candidateRect, bounds ) ) {
			targetRect = candidateRect;
			break;
		}
	}

	if ( haveBounds ) {
		OpenQ4_ClampRectToBounds( targetRect, bounds );
	}

	(void)SDL_SetWindowPosition( window, targetRect.x, targetRect.y );
}

void OpenQ4_DestroyGraphicsWindow( SDL_Window *&window ) {
	if ( window != NULL ) {
		if ( window == g_openq4PrimaryGraphicsWindow ) {
			g_openq4PrimaryGraphicsWindow = NULL;
		}
		SDL_DestroyWindow( window );
		window = NULL;
	}
}

#if defined( _WIN32 )
bool OpenQ4_GetWindowWin32Handle( SDL_Window *window, void *&nativeHandle, const char *&error ) {
	nativeHandle = nullptr;

	if ( window == nullptr ) {
		error = OpenQ4_GraphicsWindowMakeError( "OpenQ4_GetWindowWin32Handle received a null SDL window." );
		return false;
	}

	SDL_PropertiesID properties = SDL_GetWindowProperties( window );
	if ( properties == 0 ) {
		error = OpenQ4_GraphicsWindowMakeError( "SDL_GetWindowProperties failed: ", SDL_GetError() );
		return false;
	}

	nativeHandle = SDL_GetPointerProperty( properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr );
	if ( nativeHandle == nullptr ) {
		error = OpenQ4_GraphicsWindowMakeError( "SDL did not expose a Win32 HWND for the window." );
		return false;
	}

	return true;
}
#endif
