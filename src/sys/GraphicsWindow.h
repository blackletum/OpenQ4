#ifndef OPENQ4_SYS_GRAPHICS_WINDOW_H
#define OPENQ4_SYS_GRAPHICS_WINDOW_H

#include <SDL3/SDL.h>

#include "../renderer/GraphicsAPI.h"

SDL_WindowFlags OpenQ4_GetGraphicsWindowFlags( openq4GraphicsApi_t api );
bool OpenQ4_EnsureGraphicsVideoSubsystem( bool &initializedHere, const char *&error );
void OpenQ4_ReleaseGraphicsVideoSubsystem( bool initializedHere );
bool OpenQ4_PrepareGraphicsWindowRuntime( openq4GraphicsApi_t api, bool &preparedRuntime, const char *&error );
void OpenQ4_ReleaseGraphicsWindowRuntime( openq4GraphicsApi_t api, bool preparedRuntime );
bool OpenQ4_GetGraphicsWindowSizeInPixels( SDL_Window *window, int &width, int &height, const char *&error );
bool OpenQ4_CreateGraphicsWindow(
	const char *title,
	openq4GraphicsApi_t api,
	int width,
	int height,
	SDL_WindowFlags extraFlags,
	SDL_Window *&window,
	const char *&error );
void OpenQ4_SetPrimaryGraphicsWindow( SDL_Window *window );
SDL_Window *OpenQ4_GetPrimaryGraphicsWindow( void );
bool OpenQ4_GetPrimaryGraphicsWindowSizeInPixels( int &width, int &height, const char *&error );
void OpenQ4_PositionSecondaryGraphicsWindow( SDL_Window *window );
void OpenQ4_DestroyGraphicsWindow( SDL_Window *&window );

#if defined( _WIN32 )
bool OpenQ4_GetWindowWin32Handle( SDL_Window *window, void *&nativeHandle, const char *&error );
#endif

#endif
