#ifndef OPENQ4_RENDERER_NVRHI_BOOTSTRAP_H
#define OPENQ4_RENDERER_NVRHI_BOOTSTRAP_H

#include <SDL3/SDL.h>

#include "../GraphicsAPI.h"

struct openq4NvrhiBootstrapOptions_t {
	openq4GraphicsApi_t api = openq4GraphicsApi_t::Auto;
	int width = 1280;
	int height = 720;
	bool hidden = false;
	bool vsync = true;
};

class idNvrhiBootstrapBackend {
public:
	virtual ~idNvrhiBootstrapBackend() = default;

	virtual const char *GetName() const = 0;
	virtual bool Initialize( SDL_Window *window, const openq4NvrhiBootstrapOptions_t &options, const char *&error ) = 0;
	virtual bool RenderFrame( double timeSeconds, const char *&error ) = 0;
	virtual void Shutdown() = 0;
};

bool OpenQ4_NvrhiBootstrapIsApiCompiled( openq4GraphicsApi_t api );
openq4GraphicsApi_t OpenQ4_NvrhiBootstrapGetDefaultApi( void );
idNvrhiBootstrapBackend *OpenQ4_CreateNvrhiBootstrapBackend( openq4GraphicsApi_t api, const char *&error );
idNvrhiBootstrapBackend *OpenQ4_CreateNvrhiBootstrapD3D12Backend( void );
idNvrhiBootstrapBackend *OpenQ4_CreateNvrhiBootstrapVulkanBackend( void );

#endif
