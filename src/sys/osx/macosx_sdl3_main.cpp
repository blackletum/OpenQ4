/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code. See docs/legal for details.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#include "../../framework/Common.h"
#include "../posix/posix_public.h"
#include "../sys_public.h"

static void Sys_HandlePendingQuitSignal(void) {
	const int quitSignal = Posix_ConsumeQuitSignal();
	if (quitSignal == 0) {
		return;
	}

	Posix_SetExit(128 + quitSignal);
	common->Printf("Exiting on %s\n", Posix_SignalName(quitSignal));
	common->Quit();
}

int main(int argc, const char **argv) {
	Posix_EarlyInit();
	Sys_ShowSplash();

	if (argc > 1) {
		common->Init(argc - 1, &argv[1], NULL);
	} else {
		common->Init(0, NULL, NULL);
	}

	Sys_DestroySplash();
	Sys_HandlePendingQuitSignal();
	Posix_LateInit();

	while (1) {
		Sys_HandlePendingQuitSignal();
		common->Frame();
	}
}
