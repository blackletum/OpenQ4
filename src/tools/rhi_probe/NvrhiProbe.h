#ifndef OPENQ4_TOOLS_RHI_PROBE_H
#define OPENQ4_TOOLS_RHI_PROBE_H

#include <SDL3/SDL.h>

#include <string>

#include "renderer/NVRHI/NvrhiBootstrap.h"

struct nvrhiProbeOptions_t {
	openq4GraphicsApi_t api = openq4GraphicsApi_t::Auto;
	int width = 1280;
	int height = 720;
	int frames = 300;
	bool hidden = false;
	bool showUsage = false;
	bool vsync = true;
};

bool NvrhiProbe_ParseOptions( int argc, char **argv, nvrhiProbeOptions_t &options, std::string &error );
void NvrhiProbe_PrintUsage( void );

#endif
