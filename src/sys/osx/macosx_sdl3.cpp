/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code. See docs/legal for details.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#include "../../renderer/tr_local.h"

#include <ApplicationServices/ApplicationServices.h>

#define OPENQ4_SDL3_DARWIN_HOST 1
#include "../sdl3/sdl3_backend.cpp"

bool QGL_Init(const char *dllname) {
	(void)dllname;
	return true;
}

void QGL_Shutdown(void) {
}

bool Sys_GetDesktopResolution(int *width, int *height) {
	if (width == NULL || height == NULL) {
		return false;
	}

	const sdl3DisplaySelection_t selectedDisplay = SDL3_ResolveTargetDisplay(false);
	const SDL_DisplayID display = (selectedDisplay.id != 0) ? selectedDisplay.id : SDL_GetPrimaryDisplay();
	const SDL_DisplayMode *desktopMode = SDL_GetDesktopDisplayMode(display);
	if (desktopMode == NULL) {
		return false;
	}

	*width = desktopMode->w;
	*height = desktopMode->h;
	return (*width > 0 && *height > 0);
}

CGDirectDisplayID Sys_DisplayToUse(void) {
	return CGMainDisplayID();
}
