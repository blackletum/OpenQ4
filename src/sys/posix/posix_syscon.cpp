/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code. See docs/legal for details.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#include "../../framework/Common.h"
#include "../../framework/async/AsyncNetwork.h"
#include "../sys_public.h"
#include "posix_public.h"

#include <pthread.h>

#if defined( USE_SDL3 )
#include <SDL3/SDL.h>
#include <cmath>

// sdl3_backend.cpp; applies platform SDL hint defaults (including the macOS
// Metal bridge render-driver selection) before the first SDL video use.
void Sys_SDL_ApplyVideoHintDefaults( void );
// sdl3_backend.cpp; releases a (possibly fullscreen) game window so the
// fatal-error console is actually visible.
void Sys_SDL_EmergencyReleaseGameWindow( void );
#endif

static idCVar sys_consoleWindow(
	"sys_consoleWindow",
#ifdef ID_DEDICATED
	"0",
#else
	"1",
#endif
	CVAR_SYSTEM | CVAR_BOOL | CVAR_ARCHIVE,
	"enable the Linux/macOS SDL system console window"
);

static idCVar sys_viewlog(
	"sys_viewlog",
	"0",
	CVAR_SYSTEM | CVAR_INTEGER,
	"show the Linux/macOS system console window"
);

static idCVar sys_winViewlogAlias(
	"win_viewlog",
	"0",
	CVAR_SYSTEM | CVAR_INTEGER,
	"compatibility alias for showing the system console window"
);

namespace {

static const int POSIX_CONSOLE_BUFFER_SIZE = 32768;
static const int POSIX_CONSOLE_HISTORY = 64;
static const int POSIX_CONSOLE_WIDTH = 760;
static const int POSIX_CONSOLE_HEIGHT = 520;
static const int POSIX_CONSOLE_MARGIN = 8;
static const int POSIX_CONSOLE_BUTTON_WIDTH = 74;
static const int POSIX_CONSOLE_BUTTON_HEIGHT = 24;
static const int POSIX_CONSOLE_INPUT_HEIGHT = 24;
static const int POSIX_CONSOLE_STATUS_HEIGHT = 28;
static const int POSIX_CONSOLE_FONT_SIZE = 8;
static const int POSIX_CONSOLE_APPEND_SIZE = 4096;
static const int POSIX_CONSOLE_FATAL_SIZE = 4096;
static const int POSIX_CONSOLE_MAX_WHEEL_STEPS_PER_EVENT = 64;
static const int POSIX_SPLASH_WIDTH = 512;
static const int POSIX_SPLASH_HEIGHT = 384;
static const char POSIX_SPLASH_BMP[] = "assets/splash/quake4_rt_bitmap_4001.bmp";

struct posixConsoleBuffer_t {
	pthread_mutex_t mutex;
	char text[ POSIX_CONSOLE_BUFFER_SIZE + 1 ];
	char fatalText[ POSIX_CONSOLE_FATAL_SIZE + 1 ];
	int length;
	int fatalLength;

	posixConsoleBuffer_t() : mutex( PTHREAD_MUTEX_INITIALIZER ), length( 0 ), fatalLength( 0 ) {
		text[0] = '\0';
		fatalText[0] = '\0';
	}
};

static posixConsoleBuffer_t s_consoleBuffer;

#if defined( USE_SDL3 )
struct posixSplashWindow_t {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	SDL_WindowID windowID;
	SDL_WindowID lastWindowID;
	bool videoInitializedBySplash;
	bool createFailed;
	float textureWidth;
	float textureHeight;

	posixSplashWindow_t()
		: window( NULL )
		, renderer( NULL )
		, texture( NULL )
		, windowID( 0 )
		, lastWindowID( 0 )
		, videoInitializedBySplash( false )
		, createFailed( false )
		, textureWidth( 0.0f )
		, textureHeight( 0.0f ) {
	}
};

struct posixConsoleLayout_t {
	SDL_FRect statusRect;
	SDL_FRect outputRect;
	SDL_FRect inputRect;
	SDL_FRect copyButtonRect;
	SDL_FRect clearButtonRect;
	SDL_FRect quitButtonRect;
};

struct posixConsoleWindow_t {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_WindowID windowID;
	bool visible;
	bool quitOnClose;
	bool inputFocused;
	bool videoInitializedByConsole;
	bool createFailed;
	bool exitRequested;
	bool forceFatalWindow;
	int scrollLines;
	idEditField inputField;
	idEditField history[ POSIX_CONSOLE_HISTORY ];
	int nextHistoryLine;
	int historyLine;
	posixConsoleLayout_t layout;

	posixConsoleWindow_t()
		: window( NULL )
		, renderer( NULL )
		, windowID( 0 )
		, visible( false )
		, quitOnClose( false )
		, inputFocused( true )
		, videoInitializedByConsole( false )
		, createFailed( false )
		, exitRequested( false )
		, forceFatalWindow( false )
		, scrollLines( 0 )
		, nextHistoryLine( 0 )
		, historyLine( 0 ) {
	}
};

static posixSplashWindow_t s_splashWindow;
static posixConsoleWindow_t s_consoleWindow;

static const char *Posix_ConsoleInputState( int &inputLength, int &inputCursor ) {
	const char *inputBuffer = s_consoleWindow.inputField.GetBuffer();
	if ( inputBuffer == NULL ) {
		inputBuffer = "";
	}
	const size_t rawInputLength = strlen( inputBuffer );
	inputLength = rawInputLength > static_cast<size_t>( idMath::INT_MAX ) ? idMath::INT_MAX : static_cast<int>( rawInputLength );
	inputCursor = idMath::ClampInt( 0, inputLength, s_consoleWindow.inputField.GetCursor() );
	return inputBuffer;
}
#endif

static void Posix_ConsoleLockBuffer( void ) {
	pthread_mutex_lock( &s_consoleBuffer.mutex );
}

static void Posix_ConsoleUnlockBuffer( void ) {
	pthread_mutex_unlock( &s_consoleBuffer.mutex );
}

static void Posix_ConsoleCopyBuffer( idStr &copy ) {
	Posix_ConsoleLockBuffer();
	copy = s_consoleBuffer.text;
	Posix_ConsoleUnlockBuffer();
}

static void Posix_ConsoleCopyFatalError( idStr &copy ) {
	Posix_ConsoleLockBuffer();
	copy = s_consoleBuffer.fatalText;
	Posix_ConsoleUnlockBuffer();
}

static void Posix_ConsoleClearBuffer( void ) {
	Posix_ConsoleLockBuffer();
	s_consoleBuffer.text[0] = '\0';
	s_consoleBuffer.length = 0;
	Posix_ConsoleUnlockBuffer();
#if defined( USE_SDL3 )
	s_consoleWindow.scrollLines = 0;
#endif
}

static void Posix_ConsoleQueueCommand( const char *command ) {
	if ( command == NULL || command[0] == '\0' ) {
		return;
	}

	const size_t commandLength = strlen( command );
	if ( commandLength > static_cast<size_t>( idMath::INT_MAX - 1 ) ) {
		return;
	}

	const int len = static_cast<int>( commandLength ) + 1;
	char *buffer = static_cast<char *>( Mem_Alloc( len ) );
	if ( buffer == NULL ) {
		return;
	}
	idStr::Copynz( buffer, command, len );
	Posix_QueEvent( SE_CONSOLE, 0, 0, len, buffer );
}

static int Posix_ConsoleCleanText( const char *message, char *cleaned, const int cleanedSize ) {
	if ( cleaned == NULL || cleanedSize <= 0 ) {
		return 0;
	}
	cleaned[0] = '\0';

	if ( message == NULL || message[0] == '\0' ) {
		return 0;
	}

	int write = 0;
	for ( int read = 0; message[read] != '\0' && write < cleanedSize - 1; ++read ) {
		if ( message[read] == '\r' ) {
			if ( message[read + 1] != '\n' ) {
				cleaned[write++] = '\n';
			}
			continue;
		}
		int escapeType = 0;
		const int escapeLength = idStr::IsEscape( &message[read], &escapeType );
		if ( escapeLength > 0 ) {
			read += escapeLength - 1;
			continue;
		}
		cleaned[write++] = message[read];
	}
	cleaned[write] = '\0';
	return write;
}

#if defined( USE_SDL3 )
static bool Posix_SplashVideoReady( void ) {
	return ( SDL_WasInit( SDL_INIT_VIDEO ) & SDL_INIT_VIDEO ) != 0;
}

static SDL_Renderer *Posix_CreateSupportRenderer( SDL_Window *window, const char *purpose ) {
	if ( window == NULL ) {
		return NULL;
	}

	const char *purposeName = purpose != NULL ? purpose : "support";

#if defined( MACOS_X ) && defined( OPENQ4_MACOS_METAL_BRIDGE )
	SDL_Renderer *renderer = SDL_CreateRenderer( window, NULL );
	if ( renderer != NULL ) {
		// Report the driver SDL actually created, not just the requested hint;
		// this is the Metal bridge package's observable signoff diagnostic.
		const char *driverName = SDL_GetRendererName( renderer );
		if ( driverName == NULL || driverName[0] == '\0' ) {
			driverName = "unknown";
		}
		Sys_Printf( "SDL %s renderer: macOS Metal bridge created '%s' driver\n", purposeName, driverName );
		if ( idStr::Icmp( driverName, "metal" ) != 0 ) {
			Sys_Printf( "SDL %s renderer: macOS Metal bridge expected 'metal' but SDL selected '%s'\n", purposeName, driverName );
		}
		return renderer;
	}
	Sys_Printf( "SDL %s renderer: Metal/default renderer failed: %s; falling back to software\n", purposeName, SDL_GetError() );
#endif

	SDL_Renderer *softwareRenderer = SDL_CreateRenderer( window, "software" );
	if ( softwareRenderer == NULL ) {
		Sys_Printf( "SDL %s renderer: software renderer failed: %s\n", purposeName, SDL_GetError() );
	}
	return softwareRenderer;
}

static bool Posix_SplashEnsureVideo( void ) {
	if ( s_splashWindow.videoInitializedBySplash ) {
		return Posix_SplashVideoReady();
	}

	Sys_SDL_ApplyVideoHintDefaults();

	// Take a distinct SDL video reference even when the renderer or console
	// already initialized the subsystem. SDL destroys every video object when
	// the final reference is released, so borrowing another owner's reference
	// would leave our cached window/renderer pointers dangling on vid_restart.
	if ( !SDL_InitSubSystem( SDL_INIT_VIDEO ) ) {
		if ( !s_splashWindow.createFailed ) {
			Sys_Printf( "SDL splash disabled: failed to initialize video subsystem: %s\n", SDL_GetError() );
		}
		s_splashWindow.createFailed = true;
		return false;
	}

	s_splashWindow.videoInitializedBySplash = true;
	return true;
}

static bool Posix_SplashEventHasWindowID( const SDL_Event &event, SDL_WindowID windowID ) {
	if ( windowID == 0 ) {
		return false;
	}

	switch ( event.type ) {
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
		case SDL_EVENT_WINDOW_FOCUS_LOST:
		case SDL_EVENT_WINDOW_SHOWN:
		case SDL_EVENT_WINDOW_HIDDEN:
		case SDL_EVENT_WINDOW_EXPOSED:
		case SDL_EVENT_WINDOW_MOVED:
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		case SDL_EVENT_WINDOW_MINIMIZED:
		case SDL_EVENT_WINDOW_MAXIMIZED:
		case SDL_EVENT_WINDOW_RESTORED:
		case SDL_EVENT_WINDOW_MOUSE_ENTER:
		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			return event.window.windowID == windowID;
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
			return event.key.windowID == windowID;
		case SDL_EVENT_TEXT_INPUT:
			return event.text.windowID == windowID;
		case SDL_EVENT_TEXT_EDITING:
			return event.edit.windowID == windowID;
		case SDL_EVENT_TEXT_EDITING_CANDIDATES:
			return event.edit_candidates.windowID == windowID;
		case SDL_EVENT_MOUSE_MOTION:
			return event.motion.windowID == windowID;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			return event.button.windowID == windowID;
		case SDL_EVENT_MOUSE_WHEEL:
			return event.wheel.windowID == windowID;
		default:
			break;
	}

	return false;
}

static void Posix_SplashDrainEvents( SDL_WindowID windowID ) {
	if ( windowID == 0 ) {
		return;
	}

	SDL_Event event;
	while ( SDL_PollEvent( &event ) ) {
		if ( !Posix_SplashEventHasWindowID( event, windowID ) ) {
			if ( !SDL_PushEvent( &event ) ) {
				Sys_Printf( "SDL splash: failed to requeue non-splash event %u: %s\n", event.type, SDL_GetError() );
			}
			break;
		}
	}
}

static void Posix_SplashAppendCandidate( idStrList &candidates, const char *basePath, const char *relativePath ) {
	if ( basePath == NULL || basePath[0] == '\0' || relativePath == NULL || relativePath[0] == '\0' ) {
		return;
	}

	idStr candidate = basePath;
	candidate.StripTrailing( '/' );
	candidate.AppendPath( relativePath );
	candidates.Append( candidate );
}

static void Posix_SplashAppendPrefixedCandidate( idStrList &candidates, const char *basePath, const char *prefix ) {
	idStr relativePath = prefix != NULL ? prefix : "";
	relativePath += POSIX_SPLASH_BMP;
	Posix_SplashAppendCandidate( candidates, basePath, relativePath.c_str() );
}

static void Posix_SplashBuildCandidates( idStrList &candidates ) {
	candidates.Clear();

	Posix_SplashAppendCandidate( candidates, Posix_Cwd(), POSIX_SPLASH_BMP );

	const char *basePath = SDL_GetBasePath();
	if ( basePath != NULL && basePath[0] != '\0' ) {
		Posix_SplashAppendCandidate( candidates, basePath, POSIX_SPLASH_BMP );
		Posix_SplashAppendPrefixedCandidate( candidates, basePath, "../" );
		Posix_SplashAppendPrefixedCandidate( candidates, basePath, "../Resources/" );
		Posix_SplashAppendPrefixedCandidate( candidates, basePath, "../../" );
	}

	const char *exePath = Sys_EXEPath();
	if ( exePath != NULL && exePath[0] != '\0' ) {
		idStr exeDir = exePath;
		exeDir.StripFilename();
		Posix_SplashAppendCandidate( candidates, exeDir.c_str(), POSIX_SPLASH_BMP );
		Posix_SplashAppendPrefixedCandidate( candidates, exeDir.c_str(), "../" );
		Posix_SplashAppendPrefixedCandidate( candidates, exeDir.c_str(), "../Resources/" );
	}

	// macOS .app launches keep the splash asset at the extracted package root,
	// three levels above the in-bundle executable directory.
	char packageRoot[MAX_OSPATH];
	if ( Sys_GetPackageRootDirectory( packageRoot, sizeof( packageRoot ) ) ) {
		Posix_SplashAppendCandidate( candidates, packageRoot, POSIX_SPLASH_BMP );
	}
}

static bool Posix_SplashLoadTexture( void ) {
	idStrList candidates;
	Posix_SplashBuildCandidates( candidates );

	for ( int i = 0; i < candidates.Num(); ++i ) {
		SDL_Surface *surface = SDL_LoadBMP( candidates[i].c_str() );
		if ( surface == NULL ) {
			continue;
		}

		s_splashWindow.texture = SDL_CreateTextureFromSurface( s_splashWindow.renderer, surface );
		SDL_DestroySurface( surface );
		if ( s_splashWindow.texture == NULL ) {
			continue;
		}

		if ( !SDL_GetTextureSize( s_splashWindow.texture, &s_splashWindow.textureWidth, &s_splashWindow.textureHeight ) ) {
			s_splashWindow.textureWidth = POSIX_SPLASH_WIDTH;
			s_splashWindow.textureHeight = POSIX_SPLASH_HEIGHT;
		}
		return true;
	}

	return false;
}

static void Posix_SplashRenderFallback( void ) {
	(void)SDL_SetRenderDrawColor( s_splashWindow.renderer, 0x10, 0x13, 0x08, 0xff );
	(void)SDL_RenderClear( s_splashWindow.renderer );

	SDL_FRect panel = { 28.0f, 110.0f, POSIX_SPLASH_WIDTH - 56.0f, 164.0f };
	(void)SDL_SetRenderDrawColor( s_splashWindow.renderer, 0x1b, 0x20, 0x0a, 0xff );
	(void)SDL_RenderFillRect( s_splashWindow.renderer, &panel );
	(void)SDL_SetRenderDrawColor( s_splashWindow.renderer, 0xf0, 0x9e, 0x0d, 0xff );
	(void)SDL_RenderRect( s_splashWindow.renderer, &panel );
	(void)SDL_RenderDebugText( s_splashWindow.renderer, 196.0f, 174.0f, GAME_NAME );
	(void)SDL_RenderDebugText( s_splashWindow.renderer, 176.0f, 194.0f, "initializing..." );
}

static void Posix_SplashRender( void ) {
	if ( s_splashWindow.window == NULL || s_splashWindow.renderer == NULL ) {
		return;
	}

	if ( s_splashWindow.texture != NULL ) {
		(void)SDL_SetRenderDrawColor( s_splashWindow.renderer, 0x00, 0x00, 0x00, 0xff );
		(void)SDL_RenderClear( s_splashWindow.renderer );

		SDL_FRect dst = {
			0.0f,
			0.0f,
			s_splashWindow.textureWidth > 0.0f ? s_splashWindow.textureWidth : POSIX_SPLASH_WIDTH,
			s_splashWindow.textureHeight > 0.0f ? s_splashWindow.textureHeight : POSIX_SPLASH_HEIGHT
		};
		(void)SDL_RenderTexture( s_splashWindow.renderer, s_splashWindow.texture, NULL, &dst );
	} else {
		Posix_SplashRenderFallback();
	}

	(void)SDL_RenderPresent( s_splashWindow.renderer );
}

static void Posix_SplashDestroy( void ) {
	const SDL_WindowID destroyedWindowID = s_splashWindow.windowID;
	if ( s_splashWindow.texture != NULL ) {
		SDL_DestroyTexture( s_splashWindow.texture );
		s_splashWindow.texture = NULL;
	}
	if ( s_splashWindow.renderer != NULL ) {
		SDL_DestroyRenderer( s_splashWindow.renderer );
		s_splashWindow.renderer = NULL;
	}
	if ( s_splashWindow.window != NULL ) {
		SDL_DestroyWindow( s_splashWindow.window );
		s_splashWindow.window = NULL;
	}

	s_splashWindow.lastWindowID = destroyedWindowID;
	s_splashWindow.windowID = 0;
	s_splashWindow.textureWidth = 0.0f;
	s_splashWindow.textureHeight = 0.0f;

	Posix_SplashDrainEvents( destroyedWindowID );

	if ( s_splashWindow.videoInitializedBySplash ) {
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
		s_splashWindow.videoInitializedBySplash = false;
	}
}

static SDL_WindowID Posix_ConsoleWindowID( void ) {
	return s_consoleWindow.windowID;
}

static bool Posix_ConsoleVideoReady( void ) {
	return ( SDL_WasInit( SDL_INIT_VIDEO ) & SDL_INIT_VIDEO ) != 0;
}

static bool Posix_ConsoleEnsureVideo( void ) {
	if ( s_consoleWindow.videoInitializedByConsole ) {
		return Posix_ConsoleVideoReady();
	}

	Sys_SDL_ApplyVideoHintDefaults();

	// Keep an independent reference while console resources exist. This keeps
	// them valid across renderer shutdown/reinitialization and pairs exactly
	// with the console's SDL_QuitSubSystem call.
	if ( !SDL_InitSubSystem( SDL_INIT_VIDEO ) ) {
		if ( !s_consoleWindow.createFailed ) {
			Sys_Printf( "SDL system console disabled: failed to initialize video subsystem: %s\n", SDL_GetError() );
		}
		s_consoleWindow.createFailed = true;
		return false;
	}

	s_consoleWindow.videoInitializedByConsole = true;
	return true;
}

static void Posix_ConsoleUpdateLayout( void ) {
	int width = POSIX_CONSOLE_WIDTH;
	int height = POSIX_CONSOLE_HEIGHT;
	if ( s_consoleWindow.window != NULL ) {
		(void)SDL_GetWindowSize( s_consoleWindow.window, &width, &height );
	}

	if ( width < 360 ) {
		width = 360;
	}
	if ( height < 240 ) {
		height = 240;
	}

	const float margin = static_cast<float>( POSIX_CONSOLE_MARGIN );
	const float buttonHeight = static_cast<float>( POSIX_CONSOLE_BUTTON_HEIGHT );
	const float inputHeight = static_cast<float>( POSIX_CONSOLE_INPUT_HEIGHT );
	const float statusHeight = static_cast<float>( POSIX_CONSOLE_STATUS_HEIGHT );
	const float statusGap = 6.0f;
	const float bottomY = static_cast<float>( height ) - margin - buttonHeight;
	const float inputY = bottomY - margin - inputHeight;
	const float outputY = margin + statusHeight + statusGap;
	const float outputHeight = Max( 32.0f, inputY - outputY - margin );

	s_consoleWindow.layout.statusRect = {
		margin,
		margin,
		static_cast<float>( width ) - margin * 2.0f,
		statusHeight
	};

	s_consoleWindow.layout.outputRect = {
		margin,
		outputY,
		static_cast<float>( width ) - margin * 2.0f,
		outputHeight
	};
	s_consoleWindow.layout.inputRect = {
		margin,
		inputY,
		static_cast<float>( width ) - margin * 2.0f,
		inputHeight
	};
	s_consoleWindow.layout.copyButtonRect = {
		margin,
		bottomY,
		static_cast<float>( POSIX_CONSOLE_BUTTON_WIDTH ),
		buttonHeight
	};
	s_consoleWindow.layout.clearButtonRect = {
		margin + static_cast<float>( POSIX_CONSOLE_BUTTON_WIDTH ) + margin,
		bottomY,
		static_cast<float>( POSIX_CONSOLE_BUTTON_WIDTH ),
		buttonHeight
	};
	s_consoleWindow.layout.quitButtonRect = {
		static_cast<float>( width ) - margin - static_cast<float>( POSIX_CONSOLE_BUTTON_WIDTH ),
		bottomY,
		static_cast<float>( POSIX_CONSOLE_BUTTON_WIDTH ),
		buttonHeight
	};
}

static bool Posix_ConsoleCreateWindow( void ) {
	if ( s_consoleWindow.window != NULL ) {
		return true;
	}
	if ( s_consoleWindow.createFailed ) {
		return false;
	}
	// Fatal errors reach this path after common->Shutdown() has released the
	// registered cvar storage. A forced fatal window must therefore avoid every
	// cvar access; normal console creation still requires a live cvar system.
	if ( !s_consoleWindow.forceFatalWindow &&
		 ( cvarSystem == NULL || !cvarSystem->IsInitialized() || !sys_consoleWindow.GetBool() ) ) {
		return false;
	}
	if ( !Posix_ConsoleEnsureVideo() ) {
		return false;
	}

	const SDL_WindowFlags flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
	s_consoleWindow.window = SDL_CreateWindow(
		GAME_NAME " Console",
		POSIX_CONSOLE_WIDTH,
		POSIX_CONSOLE_HEIGHT,
		flags
	);
	if ( s_consoleWindow.window == NULL ) {
		if ( !s_consoleWindow.createFailed ) {
			Sys_Printf( "SDL system console disabled: failed to create window: %s\n", SDL_GetError() );
		}
		s_consoleWindow.createFailed = true;
		if ( s_consoleWindow.videoInitializedByConsole ) {
			SDL_QuitSubSystem( SDL_INIT_VIDEO );
			s_consoleWindow.videoInitializedByConsole = false;
		}
		return false;
	}

	s_consoleWindow.windowID = SDL_GetWindowID( s_consoleWindow.window );
	if ( s_consoleWindow.windowID == 0 ) {
		Sys_Printf( "SDL system console disabled: failed to resolve window id: %s\n", SDL_GetError() );
		SDL_DestroyWindow( s_consoleWindow.window );
		s_consoleWindow.window = NULL;
		s_consoleWindow.createFailed = true;
		if ( s_consoleWindow.videoInitializedByConsole ) {
			SDL_QuitSubSystem( SDL_INIT_VIDEO );
			s_consoleWindow.videoInitializedByConsole = false;
		}
		return false;
	}
	s_consoleWindow.renderer = Posix_CreateSupportRenderer( s_consoleWindow.window, "system console" );
	if ( s_consoleWindow.renderer == NULL ) {
		Sys_Printf( "SDL system console disabled: failed to create renderer: %s\n", SDL_GetError() );
		SDL_DestroyWindow( s_consoleWindow.window );
		s_consoleWindow.window = NULL;
		s_consoleWindow.windowID = 0;
		s_consoleWindow.createFailed = true;
		if ( s_consoleWindow.videoInitializedByConsole ) {
			SDL_QuitSubSystem( SDL_INIT_VIDEO );
			s_consoleWindow.videoInitializedByConsole = false;
		}
		return false;
	}

	s_consoleWindow.inputField.Clear();
	for ( int i = 0; i < POSIX_CONSOLE_HISTORY; ++i ) {
		s_consoleWindow.history[i].Clear();
	}
	s_consoleWindow.nextHistoryLine = 0;
	s_consoleWindow.historyLine = 0;
	s_consoleWindow.inputFocused = true;
	Posix_ConsoleUpdateLayout();

	return true;
}

static void Posix_ConsoleStartTextInput( void ) {
	if ( s_consoleWindow.window == NULL || !s_consoleWindow.visible ) {
		return;
	}

	const SDL_Rect inputRect = {
		static_cast<int>( s_consoleWindow.layout.inputRect.x ),
		static_cast<int>( s_consoleWindow.layout.inputRect.y ),
		static_cast<int>( s_consoleWindow.layout.inputRect.w ),
		static_cast<int>( s_consoleWindow.layout.inputRect.h )
	};
	int inputLength = 0;
	int inputCursor = 0;
	(void)Posix_ConsoleInputState( inputLength, inputCursor );
	(void)SDL_SetTextInputArea( s_consoleWindow.window, &inputRect, inputCursor * POSIX_CONSOLE_FONT_SIZE );
	(void)SDL_StartTextInput( s_consoleWindow.window );
}

static void Posix_ConsoleStopTextInput( void ) {
	if ( s_consoleWindow.window != NULL ) {
		(void)SDL_StopTextInput( s_consoleWindow.window );
	}
}

static void Posix_ConsoleHide( void ) {
	if ( s_consoleWindow.window == NULL ) {
		s_consoleWindow.visible = false;
		return;
	}

	Posix_ConsoleStopTextInput();
	(void)SDL_HideWindow( s_consoleWindow.window );
	s_consoleWindow.visible = false;
	sys_viewlog.SetInteger( 0 );
	sys_winViewlogAlias.SetInteger( 0 );
	sys_viewlog.ClearModified();
	sys_winViewlogAlias.ClearModified();
}

static void Posix_ConsoleSubmitInput( void ) {
	const char *command = s_consoleWindow.inputField.GetBuffer();
	if ( command == NULL || command[0] == '\0' ) {
		s_consoleWindow.inputField.Clear();
		return;
	}

	Sys_Printf( "]%s\n", command );
	Posix_ConsoleQueueCommand( command );

	s_consoleWindow.history[ s_consoleWindow.nextHistoryLine % POSIX_CONSOLE_HISTORY ] = s_consoleWindow.inputField;
	s_consoleWindow.nextHistoryLine++;
	s_consoleWindow.historyLine = s_consoleWindow.nextHistoryLine;
	s_consoleWindow.inputField.Clear();
	s_consoleWindow.scrollLines = 0;
}

static void Posix_ConsoleHistory( int direction ) {
	if ( direction < 0 ) {
		if ( s_consoleWindow.nextHistoryLine - s_consoleWindow.historyLine < POSIX_CONSOLE_HISTORY &&
			 s_consoleWindow.historyLine > 0 ) {
			s_consoleWindow.historyLine--;
		}
	} else if ( direction > 0 ) {
		if ( s_consoleWindow.historyLine == s_consoleWindow.nextHistoryLine ) {
			return;
		}
		s_consoleWindow.historyLine++;
	}

	s_consoleWindow.inputField = s_consoleWindow.history[ s_consoleWindow.historyLine % POSIX_CONSOLE_HISTORY ];
}

static void Posix_ConsoleAppendUTF8( const char *text, bool stopAtLineBreak ) {
	if ( text == NULL ) {
		return;
	}

	size_t remaining = SDL_strlen( text );
	while ( remaining > 0 ) {
		const Uint32 codepoint = SDL_StepUTF8( &text, &remaining );
		if ( codepoint == 0 ) {
			break;
		}
		if ( stopAtLineBreak && ( codepoint == '\n' || codepoint == '\r' ) ) {
			break;
		}
		if ( codepoint == SDL_INVALID_UNICODE_CODEPOINT || codepoint > 0xff ||
			 !idStr::CharIsPrintable( static_cast<byte>( codepoint ) ) ) {
			continue;
		}

		s_consoleWindow.inputField.CharEvent( static_cast<int>( codepoint ) );
		s_consoleWindow.inputField.ClearAutoComplete();
	}
}

static void Posix_ConsolePasteClipboard( void ) {
	char *clipboardText = SDL_GetClipboardText();
	if ( clipboardText == NULL ) {
		return;
	}

	Posix_ConsoleAppendUTF8( clipboardText, true );
	SDL_free( clipboardText );
}

static void Posix_ConsoleCopyAll( void ) {
	idStr text;
	Posix_ConsoleCopyBuffer( text );
	(void)SDL_SetClipboardText( text.c_str() );
}

static bool Posix_ConsolePointInRect( float x, float y, const SDL_FRect &rect ) {
	return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

static void Posix_ConsoleClickButton( float x, float y ) {
	if ( Posix_ConsolePointInRect( x, y, s_consoleWindow.layout.copyButtonRect ) ) {
		Posix_ConsoleCopyAll();
		return;
	}
	if ( Posix_ConsolePointInRect( x, y, s_consoleWindow.layout.clearButtonRect ) ) {
		Posix_ConsoleClearBuffer();
		return;
	}
	if ( Posix_ConsolePointInRect( x, y, s_consoleWindow.layout.quitButtonRect ) ) {
		if ( s_consoleWindow.quitOnClose ) {
			s_consoleWindow.exitRequested = true;
			return;
		}
		Posix_ConsoleQueueCommand( "quit" );
		return;
	}
	if ( Posix_ConsolePointInRect( x, y, s_consoleWindow.layout.inputRect ) ) {
		s_consoleWindow.inputFocused = true;
		Posix_ConsoleStartTextInput();
	}
}

static void Posix_ConsoleHandleClose( void ) {
	if ( s_consoleWindow.quitOnClose ) {
		s_consoleWindow.exitRequested = true;
	} else {
		Posix_ConsoleHide();
	}
}

static bool Posix_ConsoleHandleKey( const SDL_KeyboardEvent &keyEvent ) {
	if ( !keyEvent.down ) {
		return true;
	}

	const SDL_Keymod shortcutMod = SDL_KMOD_CTRL | SDL_KMOD_GUI;
	const bool hasShortcutMod = ( keyEvent.mod & shortcutMod ) != 0;

	if ( hasShortcutMod && keyEvent.key == SDLK_C ) {
		Posix_ConsoleCopyAll();
		return true;
	}
	if ( hasShortcutMod && keyEvent.key == SDLK_V ) {
		Posix_ConsolePasteClipboard();
		return true;
	}
	if ( hasShortcutMod && keyEvent.key == SDLK_L ) {
		Posix_ConsoleClearBuffer();
		return true;
	}

	switch ( keyEvent.key ) {
		case SDLK_ESCAPE:
			if ( !s_consoleWindow.quitOnClose ) {
				Posix_ConsoleHide();
			}
			return true;
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
			Posix_ConsoleSubmitInput();
			return true;
		case SDLK_TAB:
			s_consoleWindow.inputField.AutoComplete();
			return true;
		case SDLK_BACKSPACE:
			s_consoleWindow.inputField.CharEvent( K_BACKSPACE );
			return true;
		case SDLK_DELETE:
			s_consoleWindow.inputField.KeyDownEvent( K_DEL );
			return true;
		case SDLK_LEFT:
			s_consoleWindow.inputField.KeyDownEvent( K_LEFTARROW );
			return true;
		case SDLK_RIGHT:
			s_consoleWindow.inputField.KeyDownEvent( K_RIGHTARROW );
			return true;
		case SDLK_HOME:
			s_consoleWindow.inputField.SetCursor( 0 );
			return true;
		case SDLK_END:
			{
				int inputLength = 0;
				int inputCursor = 0;
				(void)Posix_ConsoleInputState( inputLength, inputCursor );
				s_consoleWindow.inputField.SetCursor( inputLength );
			}
			return true;
		case SDLK_UP:
			Posix_ConsoleHistory( -1 );
			return true;
		case SDLK_DOWN:
			Posix_ConsoleHistory( 1 );
			return true;
		case SDLK_PAGEUP:
			s_consoleWindow.scrollLines += 8;
			return true;
		case SDLK_PAGEDOWN:
			s_consoleWindow.scrollLines -= 8;
			if ( s_consoleWindow.scrollLines < 0 ) {
				s_consoleWindow.scrollLines = 0;
			}
			return true;
		default:
			break;
	}

	return true;
}

static void Posix_ConsoleHandleText( const char *text ) {
	Posix_ConsoleAppendUTF8( text, false );
}

static void Posix_ConsoleBuildLines( const char *text, int maxColumns, idList<idStr> &lines ) {
	lines.Clear();
	if ( maxColumns < 1 ) {
		maxColumns = 1;
	}

	idStr line;
	for ( const char *scan = text; scan != NULL && *scan != '\0'; ++scan ) {
		if ( *scan == '\r' ) {
			continue;
		}
		if ( *scan == '\n' ) {
			lines.Append( line );
			line.Clear();
			continue;
		}
		if ( *scan == '\t' ) {
			const int spaces = 4 - ( line.Length() & 3 );
			for ( int i = 0; i < spaces; ++i ) {
				if ( line.Length() >= maxColumns ) {
					lines.Append( line );
					line.Clear();
				}
				line.Append( ' ' );
			}
			continue;
		}

		if ( line.Length() >= maxColumns ) {
			lines.Append( line );
			line.Clear();
		}
		line.Append( *scan );
	}

	if ( line.Length() > 0 || lines.Num() == 0 ) {
		lines.Append( line );
	}
}

static void Posix_ConsoleDrawRect( const SDL_FRect &rect, Uint8 r, Uint8 g, Uint8 b, Uint8 a, bool filled ) {
	(void)SDL_SetRenderDrawColor( s_consoleWindow.renderer, r, g, b, a );
	if ( filled ) {
		(void)SDL_RenderFillRect( s_consoleWindow.renderer, &rect );
	} else {
		(void)SDL_RenderRect( s_consoleWindow.renderer, &rect );
	}
}

static void Posix_ConsoleDrawText( float x, float y, const char *text, Uint8 r, Uint8 g, Uint8 b, Uint8 a ) {
	(void)SDL_SetRenderDrawColor( s_consoleWindow.renderer, r, g, b, a );
	(void)SDL_RenderDebugText( s_consoleWindow.renderer, x, y, text != NULL ? text : "" );
}

static void Posix_ConsoleDrawButton( const SDL_FRect &rect, const char *label ) {
	Posix_ConsoleDrawRect( rect, 0x3a, 0x3f, 0x27, 0xff, true );
	Posix_ConsoleDrawRect( rect, 0x79, 0x82, 0x50, 0xff, false );

	const char *buttonLabel = label != NULL ? label : "";
	const int textWidth = strlen( buttonLabel ) * POSIX_CONSOLE_FONT_SIZE;
	const float textX = rect.x + Max( 4.0f, ( rect.w - static_cast<float>( textWidth ) ) * 0.5f );
	const float textY = rect.y + ( rect.h - static_cast<float>( POSIX_CONSOLE_FONT_SIZE ) ) * 0.5f;
	Posix_ConsoleDrawText( textX, textY, buttonLabel, 0xf0, 0x9e, 0x0d, 0xff );
}

static void Posix_ConsoleDrawStatus( const char *fatalText ) {
	const SDL_FRect &rect = s_consoleWindow.layout.statusRect;
	Posix_ConsoleDrawRect( rect, 0x1b, 0x20, 0x0a, 0xff, true );
	Posix_ConsoleDrawRect( rect, 0x5b, 0x66, 0x36, 0xff, false );

	idStr statusText;
	if ( fatalText != NULL && fatalText[0] != '\0' ) {
		const char *scan = fatalText;
		while ( *scan != '\0' && *scan != '\n' ) {
			statusText.Append( scan, 1 );
			scan++;
		}
	} else {
		statusText = "System console ready";
	}

	SDL_Rect clip = {
		static_cast<int>( rect.x + 5.0f ),
		static_cast<int>( rect.y + 4.0f ),
		Max( 1, static_cast<int>( rect.w - 10.0f ) ),
		Max( 1, static_cast<int>( rect.h - 8.0f ) )
	};
	SDL_SetRenderClipRect( s_consoleWindow.renderer, &clip );
	Posix_ConsoleDrawText(
		rect.x + 5.0f,
		rect.y + ( rect.h - static_cast<float>( POSIX_CONSOLE_FONT_SIZE ) ) * 0.5f,
		statusText.c_str(),
		0xf0,
		0x9e,
		0x0d,
		0xff
	);
	SDL_SetRenderClipRect( s_consoleWindow.renderer, NULL );
}

static void Posix_ConsoleRender( void ) {
	if ( s_consoleWindow.window == NULL || s_consoleWindow.renderer == NULL || !s_consoleWindow.visible ) {
		return;
	}

	Posix_ConsoleUpdateLayout();

	idStr snapshot;
	idStr fatalSnapshot;
	Posix_ConsoleCopyBuffer( snapshot );
	Posix_ConsoleCopyFatalError( fatalSnapshot );

	(void)SDL_SetRenderDrawColor( s_consoleWindow.renderer, 0x10, 0x13, 0x08, 0xff );
	(void)SDL_RenderClear( s_consoleWindow.renderer );

	Posix_ConsoleDrawStatus( fatalSnapshot.c_str() );

	Posix_ConsoleDrawRect( s_consoleWindow.layout.outputRect, 0x1b, 0x20, 0x0a, 0xff, true );
	Posix_ConsoleDrawRect( s_consoleWindow.layout.outputRect, 0x5b, 0x66, 0x36, 0xff, false );
	Posix_ConsoleDrawRect( s_consoleWindow.layout.inputRect, 0x0e, 0x10, 0x08, 0xff, true );
	Posix_ConsoleDrawRect( s_consoleWindow.layout.inputRect, 0x7b, 0x86, 0x50, 0xff, false );

	const int maxColumns = Max( 1, static_cast<int>( s_consoleWindow.layout.outputRect.w - 8.0f ) / POSIX_CONSOLE_FONT_SIZE );
	const int visibleLines = Max( 1, static_cast<int>( s_consoleWindow.layout.outputRect.h - 8.0f ) / POSIX_CONSOLE_FONT_SIZE );
	idList<idStr> lines;
	Posix_ConsoleBuildLines( snapshot.c_str(), maxColumns, lines );

	const int maxScroll = Max( 0, lines.Num() - visibleLines );
	s_consoleWindow.scrollLines = idMath::ClampInt( 0, maxScroll, s_consoleWindow.scrollLines );
	const int firstLine = Max( 0, lines.Num() - visibleLines - s_consoleWindow.scrollLines );
	const int lastLine = Min( lines.Num(), firstLine + visibleLines );
	float y = s_consoleWindow.layout.outputRect.y + 4.0f;
	for ( int i = firstLine; i < lastLine; ++i ) {
		Posix_ConsoleDrawText(
			s_consoleWindow.layout.outputRect.x + 4.0f,
			y,
			lines[i].c_str(),
			0xf0,
			0x9e,
			0x0d,
			0xff
		);
		y += POSIX_CONSOLE_FONT_SIZE;
	}

	int inputLength = 0;
	int inputCursor = 0;
	const char *inputBuffer = Posix_ConsoleInputState( inputLength, inputCursor );
	const int maxInputChars = Max( 1, static_cast<int>( s_consoleWindow.layout.inputRect.w - 10.0f ) / POSIX_CONSOLE_FONT_SIZE - 1 );
	int firstInputChar = 0;
	if ( inputLength > maxInputChars ) {
		firstInputChar = idMath::ClampInt( 0, inputLength - maxInputChars, inputCursor - maxInputChars + 1 );
	}
	const int visibleInputChars = Min( maxInputChars, inputLength - firstInputChar );
	idStr inputText = "]";
	inputText.Append( inputBuffer + firstInputChar, visibleInputChars );
	Posix_ConsoleDrawText(
		s_consoleWindow.layout.inputRect.x + 5.0f,
		s_consoleWindow.layout.inputRect.y + 8.0f,
		inputText.c_str(),
		0xf0,
		0x9e,
		0x0d,
		0xff
	);

	if ( s_consoleWindow.inputFocused ) {
		const float cursorX = s_consoleWindow.layout.inputRect.x + 5.0f +
			static_cast<float>( 1 + inputCursor - firstInputChar ) * POSIX_CONSOLE_FONT_SIZE;
		const SDL_FRect cursorRect = {
			cursorX,
			s_consoleWindow.layout.inputRect.y + 5.0f,
			1.0f,
			s_consoleWindow.layout.inputRect.h - 10.0f
		};
		Posix_ConsoleDrawRect( cursorRect, 0xf0, 0x9e, 0x0d, 0xff, true );
	}

	Posix_ConsoleDrawButton( s_consoleWindow.layout.copyButtonRect, "copy" );
	Posix_ConsoleDrawButton( s_consoleWindow.layout.clearButtonRect, "clear" );
	Posix_ConsoleDrawButton( s_consoleWindow.layout.quitButtonRect, "quit" );

	(void)SDL_RenderPresent( s_consoleWindow.renderer );
}

static void Posix_ConsoleShow( int visLevel, bool quitOnClose ) {
	Posix_SplashDestroy();
	s_consoleWindow.quitOnClose = quitOnClose;

	if ( visLevel < 0 || visLevel > 2 ) {
		Sys_Error( "Invalid visLevel %d sent to Sys_ShowConsole\n", visLevel );
		return;
	}

	if ( visLevel == 0 ) {
		Posix_ConsoleHide();
		return;
	}

	if ( !Posix_ConsoleCreateWindow() ) {
		return;
	}

	if ( visLevel == 1 ) {
		(void)SDL_ShowWindow( s_consoleWindow.window );
		(void)SDL_RaiseWindow( s_consoleWindow.window );
		s_consoleWindow.visible = true;
		s_consoleWindow.inputFocused = true;
		s_consoleWindow.exitRequested = false;
		s_consoleWindow.scrollLines = 0;
		Posix_ConsoleStartTextInput();
	} else if ( visLevel == 2 ) {
		(void)SDL_ShowWindow( s_consoleWindow.window );
		(void)SDL_MinimizeWindow( s_consoleWindow.window );
		s_consoleWindow.visible = true;
		s_consoleWindow.exitRequested = false;
		Posix_ConsoleStopTextInput();
	}
}
#endif

} // namespace

void Posix_ConsoleSetFatalError( const char *message ) {
	char cleaned[ POSIX_CONSOLE_FATAL_SIZE ];
	const int write = Posix_ConsoleCleanText( message, cleaned, sizeof( cleaned ) );

	Posix_ConsoleLockBuffer();
	if ( write <= 0 ) {
		s_consoleBuffer.fatalText[0] = '\0';
		s_consoleBuffer.fatalLength = 0;
	} else {
		idStr::Copynz( s_consoleBuffer.fatalText, cleaned, sizeof( s_consoleBuffer.fatalText ) );
		s_consoleBuffer.fatalLength = strlen( s_consoleBuffer.fatalText );
	}
	Posix_ConsoleUnlockBuffer();
}

void Posix_ConsoleAppendText( const char *message ) {
	char cleaned[ POSIX_CONSOLE_APPEND_SIZE ];
	const int write = Posix_ConsoleCleanText( message, cleaned, sizeof( cleaned ) );
	if ( write <= 0 ) {
		return;
	}

	Posix_ConsoleLockBuffer();
	if ( write >= POSIX_CONSOLE_BUFFER_SIZE ) {
		memcpy( s_consoleBuffer.text, cleaned + write - POSIX_CONSOLE_BUFFER_SIZE, POSIX_CONSOLE_BUFFER_SIZE );
		s_consoleBuffer.length = POSIX_CONSOLE_BUFFER_SIZE;
		s_consoleBuffer.text[ s_consoleBuffer.length ] = '\0';
		Posix_ConsoleUnlockBuffer();
		return;
	}

	const int overflow = s_consoleBuffer.length + write - POSIX_CONSOLE_BUFFER_SIZE;
	if ( overflow > 0 ) {
		memmove( s_consoleBuffer.text, s_consoleBuffer.text + overflow, s_consoleBuffer.length - overflow + 1 );
		s_consoleBuffer.length -= overflow;
	}

	memcpy( s_consoleBuffer.text + s_consoleBuffer.length, cleaned, write + 1 );
	s_consoleBuffer.length += write;
	Posix_ConsoleUnlockBuffer();
}

void Posix_ConsoleLateInit( void ) {
#if defined( USE_SDL3 )
	if ( !sys_consoleWindow.GetBool() ) {
		return;
	}

	const bool shouldShow =
		sys_viewlog.GetInteger() != 0 ||
		sys_winViewlogAlias.GetInteger() != 0 ||
		com_skipRenderer.GetBool() ||
		idAsyncNetwork::serverDedicated.GetInteger() != 0;

	Sys_ShowConsole( shouldShow ? 1 : 0, shouldShow );
#endif
}

bool Posix_ConsoleNeedsEventPump( void ) {
#if defined( USE_SDL3 )
	return s_consoleWindow.window != NULL && Posix_ConsoleVideoReady();
#else
	return false;
#endif
}

bool Posix_ConsoleProcessEvent( const void *eventData ) {
#if defined( USE_SDL3 )
	if ( eventData == NULL || s_consoleWindow.window == NULL ) {
		return false;
	}

	const SDL_Event &event = *static_cast<const SDL_Event *>( eventData );
	const SDL_WindowID windowID = Posix_ConsoleWindowID();

	switch ( event.type ) {
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			if ( event.window.windowID == windowID ) {
				Posix_ConsoleHandleClose();
				return true;
			}
			return false;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			if ( event.window.windowID == windowID ) {
				s_consoleWindow.inputFocused = true;
				Posix_ConsoleStartTextInput();
				return true;
			}
			return false;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			if ( event.window.windowID == windowID ) {
				s_consoleWindow.inputFocused = false;
				Posix_ConsoleStopTextInput();
				return true;
			}
			return false;
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			if ( event.window.windowID == windowID ) {
				Posix_ConsoleUpdateLayout();
				Posix_ConsoleStartTextInput();
				return true;
			}
			return false;
		default:
			if ( event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST ) {
				return event.window.windowID == windowID;
			}
			break;
	}

	switch ( event.type ) {
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
			if ( event.key.windowID == windowID ) {
				return Posix_ConsoleHandleKey( event.key );
			}
			return false;
		case SDL_EVENT_TEXT_INPUT:
			if ( event.text.windowID == windowID ) {
				Posix_ConsoleHandleText( event.text.text );
				return true;
			}
			return false;
		case SDL_EVENT_TEXT_EDITING:
			return event.edit.windowID == windowID;
		case SDL_EVENT_MOUSE_MOTION:
			return event.motion.windowID == windowID;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if ( event.button.windowID == windowID && event.button.button == SDL_BUTTON_LEFT ) {
				Posix_ConsoleClickButton( event.button.x, event.button.y );
				return true;
			}
			return event.button.windowID == windowID;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			return event.button.windowID == windowID;
		case SDL_EVENT_MOUSE_WHEEL:
			if ( event.wheel.windowID == windowID ) {
				if ( !std::isfinite( event.wheel.y ) ) {
					return true;
				}
				const float clampedWheelY = idMath::ClampFloat(
					-static_cast<float>( POSIX_CONSOLE_MAX_WHEEL_STEPS_PER_EVENT ),
					static_cast<float>( POSIX_CONSOLE_MAX_WHEEL_STEPS_PER_EVENT ),
					event.wheel.y );
				const int scrollDelta = static_cast<int>( clampedWheelY ) * 3;
				if ( scrollDelta < 0 && s_consoleWindow.scrollLines < -scrollDelta ) {
					s_consoleWindow.scrollLines = 0;
				} else if ( scrollDelta > 0 && s_consoleWindow.scrollLines > idMath::INT_MAX - scrollDelta ) {
					s_consoleWindow.scrollLines = idMath::INT_MAX;
				} else {
					s_consoleWindow.scrollLines += scrollDelta;
				}
				return true;
			}
			return false;
		default:
			break;
	}
#endif
	return false;
}

void Posix_ConsoleFrame( void ) {
#if defined( USE_SDL3 )
	if ( cvarSystem != NULL && cvarSystem->IsInitialized() ) {
		if ( sys_consoleWindow.IsModified() ) {
			if ( !sys_consoleWindow.GetBool() ) {
				Posix_ConsoleHide();
			}
			sys_consoleWindow.ClearModified();
		}
		if ( sys_viewlog.IsModified() || sys_winViewlogAlias.IsModified() ) {
			if ( !com_skipRenderer.GetBool() && idAsyncNetwork::serverDedicated.GetInteger() != 1 ) {
				const int visLevel = Max( sys_viewlog.GetInteger(), sys_winViewlogAlias.GetInteger() );
				Sys_ShowConsole( visLevel, false );
			}
			sys_viewlog.ClearModified();
			sys_winViewlogAlias.ClearModified();
		}
	}

	Posix_ConsoleRender();
#endif
}

void Posix_ConsoleFatalErrorWait( void ) {
#ifdef ID_DEDICATED
	// Headless servers already preserve fatal diagnostics on stderr, in the
	// engine log, and in the fatal breadcrumb. Never replace that path with an
	// SDL window that can block forever when no compositor is available.
	return;
#endif
#if defined( USE_SDL3 )
	if ( !Posix_IsMainThread() ) {
		// SDL window creation and event pumping are main-thread-only on
		// macOS. A worker-thread fatal error keeps its message on
		// stdout/stderr and the console buffer instead of crashing inside
		// Cocoa before the text is ever shown.
		Sys_Printf( "fatal error raised off the main thread; skipping the fatal error window\n" );
		return;
	}

	s_consoleWindow.forceFatalWindow = true;
	// An earlier transient console-window failure must not permanently
	// suppress the forced fatal-error window; give it one fresh attempt.
	if ( s_consoleWindow.window == NULL && s_consoleWindow.createFailed ) {
		s_consoleWindow.createFailed = false;
	}
	// Release a frozen (possibly exclusive-fullscreen) game window so the
	// error console is visible instead of stuck behind it.
	Sys_SDL_EmergencyReleaseGameWindow();
	Sys_ShowConsole( 1, true );
	if ( s_consoleWindow.window == NULL || !Posix_ConsoleVideoReady() ) {
		return;
	}

	while ( !s_consoleWindow.exitRequested ) {
		SDL_Event event;
		while ( SDL_PollEvent( &event ) ) {
			if ( event.type == SDL_EVENT_QUIT ) {
				s_consoleWindow.exitRequested = true;
				break;
			}
			(void)Posix_ConsoleProcessEvent( &event );
		}
		Posix_ConsoleRender();
		SDL_Delay( 16 );
	}
#endif
}

void Posix_ShutdownConsole( void ) {
#if defined( USE_SDL3 )
	Posix_SplashDestroy();
	Posix_ConsoleStopTextInput();
	if ( s_consoleWindow.renderer != NULL ) {
		SDL_DestroyRenderer( s_consoleWindow.renderer );
		s_consoleWindow.renderer = NULL;
	}
	if ( s_consoleWindow.window != NULL ) {
		SDL_DestroyWindow( s_consoleWindow.window );
		s_consoleWindow.window = NULL;
		s_consoleWindow.windowID = 0;
	}
	s_consoleWindow.visible = false;
	if ( s_consoleWindow.videoInitializedByConsole ) {
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
		s_consoleWindow.videoInitializedByConsole = false;
	}
#endif
}

void Sys_ShowSplash( void ) {
#if defined( USE_SDL3 ) && !defined( ID_DEDICATED )
	if ( s_splashWindow.window != NULL || s_splashWindow.createFailed ) {
		return;
	}
	if ( !Posix_SplashEnsureVideo() ) {
		return;
	}

	s_splashWindow.window = SDL_CreateWindow(
		GAME_NAME,
		POSIX_SPLASH_WIDTH,
		POSIX_SPLASH_HEIGHT,
		SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS
	);
	if ( s_splashWindow.window == NULL ) {
		Sys_Printf( "SDL splash disabled: failed to create window: %s\n", SDL_GetError() );
		s_splashWindow.createFailed = true;
		if ( s_splashWindow.videoInitializedBySplash ) {
			SDL_QuitSubSystem( SDL_INIT_VIDEO );
			s_splashWindow.videoInitializedBySplash = false;
		}
		return;
	}

	s_splashWindow.windowID = SDL_GetWindowID( s_splashWindow.window );
	if ( s_splashWindow.windowID == 0 ) {
		Sys_Printf( "SDL splash disabled: failed to resolve window id: %s\n", SDL_GetError() );
		Posix_SplashDestroy();
		s_splashWindow.createFailed = true;
		return;
	}
	s_splashWindow.renderer = Posix_CreateSupportRenderer( s_splashWindow.window, "splash" );
	if ( s_splashWindow.renderer == NULL ) {
		Sys_Printf( "SDL splash disabled: failed to create renderer: %s\n", SDL_GetError() );
		Posix_SplashDestroy();
		s_splashWindow.createFailed = true;
		return;
	}

	(void)Posix_SplashLoadTexture();
	(void)SDL_SetWindowPosition( s_splashWindow.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED );
	(void)SDL_ShowWindow( s_splashWindow.window );
	(void)SDL_RaiseWindow( s_splashWindow.window );
	Posix_SplashRender();
	SDL_PumpEvents();
#endif
}

void Sys_DestroySplash( void ) {
#if defined( USE_SDL3 )
	Posix_SplashDestroy();
#endif
}

void Sys_ShowConsole( int visLevel, bool quitOnClose ) {
#if defined( USE_SDL3 )
	Posix_ConsoleShow( visLevel, quitOnClose );
#else
	if ( visLevel < 0 || visLevel > 2 ) {
		Sys_Error( "Invalid visLevel %d sent to Sys_ShowConsole\n", visLevel );
	}
	(void)quitOnClose;
#endif
}
