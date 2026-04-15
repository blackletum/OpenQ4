#ifndef OPENQ4_RENDERER_NVRHI_SESSION_H
#define OPENQ4_RENDERER_NVRHI_SESSION_H

#include "../GraphicsAPI.h"

bool OpenQ4_StartNvrhiBootstrapSession(
	openq4GraphicsApi_t requestedApi,
	int width,
	int height,
	bool hidden,
	bool vsync,
	openq4GraphicsApi_t &resolvedApi,
	const char *&error );
bool OpenQ4_TickNvrhiBootstrapSession( const char *&error );
void OpenQ4_StopNvrhiBootstrapSession( void );
bool OpenQ4_IsNvrhiBootstrapSessionActive( void );
openq4GraphicsApi_t OpenQ4_GetNvrhiBootstrapSessionApi( void );
int OpenQ4_GetNvrhiBootstrapSessionFrameCount( void );
const char *OpenQ4_GetNvrhiBootstrapSessionBackendName( void );
bool OpenQ4_IsNvrhiBootstrapSessionHidden( void );
unsigned int OpenQ4_GetNvrhiBootstrapSessionWindowId( void );
bool OpenQ4_GetNvrhiBootstrapSessionWindowSizeInPixels( int &width, int &height );

#endif
