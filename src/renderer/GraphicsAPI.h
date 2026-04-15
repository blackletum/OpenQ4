#ifndef OPENQ4_RENDERER_GRAPHICS_API_H
#define OPENQ4_RENDERER_GRAPHICS_API_H

enum class openq4GraphicsApi_t {
	Auto,
	OpenGL,
	D3D12,
	Vulkan,
};

extern const char *openq4GraphicsApiArgs[];
extern const char *openq4GraphicsApiNvrhiArgs[];
extern const char *openq4GraphicsApiRuntimeArgs[];

const char *OpenQ4_GraphicsApiName( openq4GraphicsApi_t api );
bool OpenQ4_ParseGraphicsApi( const char *text, openq4GraphicsApi_t &api );
bool OpenQ4_IsExplicitGraphicsApi( openq4GraphicsApi_t api );

#endif
