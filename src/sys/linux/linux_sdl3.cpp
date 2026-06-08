/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#include "../../renderer/tr_local.h"
#include "local.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

extern "C" {
#include "libXNVCtrl/NVCtrlLib.h"
}

#define OPENQ4_SDL3_LINUX_HOST 1
#include "../sdl3/sdl3_backend.cpp"

Display *dpy = NULL;
Window win = 0;
bool dga_found = false;

idCVar sys_videoRam(
	"sys_videoRam",
	"0",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"Texture memory on the video card (in megabytes) - 0: autodetect",
	0,
	512
);

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

int Sys_GetVideoRam(void) {
	static int cachedVideoRam = 0;
	if (cachedVideoRam != 0) {
		return cachedVideoRam;
	}

	if (sys_videoRam.GetInteger() > 0) {
		cachedVideoRam = sys_videoRam.GetInteger();
		return cachedVideoRam;
	}

	common->Printf("guessing video ram ( use +set sys_videoRam to force ) ..\n");

	Display *queryDisplay = dpy;
	const bool ownsDisplay = (queryDisplay == NULL);
	if (queryDisplay == NULL && getenv("DISPLAY") != NULL) {
		queryDisplay = XOpenDisplay(NULL);
	}

	if (queryDisplay != NULL) {
		const int screen = DefaultScreen(queryDisplay);
		int major = 0;
		int minor = 0;
		int value = 0;

		if (XNVCTRLQueryVersion(queryDisplay, &major, &minor)) {
			common->Printf("found XNVCtrl extension %d.%d\n", major, minor);
			if (XNVCTRLIsNvScreen(queryDisplay, screen) &&
				XNVCTRLQueryAttribute(queryDisplay, screen, 0, NV_CTRL_VIDEO_RAM, &value)) {
				cachedVideoRam = value / 1024;
			}
		}

		if (ownsDisplay) {
			XCloseDisplay(queryDisplay);
		}

		if (cachedVideoRam > 0) {
			return cachedVideoRam;
		}
	}

	const int fd = open("/proc/dri/0/umm", O_RDONLY);
	if (fd != -1) {
		char ummBuffer[1024];
		const int len = read(fd, ummBuffer, sizeof(ummBuffer));
		close(fd);

		if (len > 1) {
			ummBuffer[len - 1] = '\0';
			int total = 0;
			for (char *line = strtok(ummBuffer, "\n"); line != NULL; line = strtok(NULL, "\n")) {
				if (strlen(line) >= 13 && strstr(line, "max   LFB =") == line) {
					total += atoi(line + 12);
				} else if (strlen(line) >= 13 && strstr(line, "max   Inv =") == line) {
					total += atoi(line + 12);
				}
			}
			if (total > 0) {
				cachedVideoRam = (total / 1048576) & ~15;
				return cachedVideoRam;
			}
		} else if (len == -1) {
			common->Printf("read /proc/dri/0/umm failed: %s\n", strerror(errno));
		}
	}

	common->Printf("guess failed, return default low-end VRAM setting ( 64MB VRAM )\n");
	cachedVideoRam = 64;
	return cachedVideoRam;
}
