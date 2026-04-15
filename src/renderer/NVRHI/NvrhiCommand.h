#ifndef OPENQ4_RENDERER_NVRHI_COMMAND_H
#define OPENQ4_RENDERER_NVRHI_COMMAND_H

#include "../GraphicsAPI.h"

bool OpenQ4_RunNvrhiBootstrapFrames(
	openq4GraphicsApi_t requestedApi,
	int frames,
	bool hidden,
	bool vsync,
	openq4GraphicsApi_t &resolvedApi,
	const char *&error );

#endif
