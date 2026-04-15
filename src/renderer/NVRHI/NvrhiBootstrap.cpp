#include "NvrhiBootstrap.h"

#include "NvrhiError.h"

#include <string>

bool OpenQ4_NvrhiBootstrapIsApiCompiled( openq4GraphicsApi_t api ) {
	switch ( api ) {
		case openq4GraphicsApi_t::D3D12:
#if defined( OPENQ4_NVRHI_HAS_D3D12 )
			return true;
#else
			return false;
#endif
		case openq4GraphicsApi_t::Vulkan:
#if defined( OPENQ4_NVRHI_HAS_VULKAN )
			return true;
#else
			return false;
#endif
		default:
			return false;
	}
}

openq4GraphicsApi_t OpenQ4_NvrhiBootstrapGetDefaultApi( void ) {
#if defined( OPENQ4_NVRHI_HAS_D3D12 )
	return openq4GraphicsApi_t::D3D12;
#elif defined( OPENQ4_NVRHI_HAS_VULKAN )
	return openq4GraphicsApi_t::Vulkan;
#else
	return openq4GraphicsApi_t::Auto;
#endif
}

idNvrhiBootstrapBackend *OpenQ4_CreateNvrhiBootstrapBackend( openq4GraphicsApi_t api, const char *&error ) {
	if ( api == openq4GraphicsApi_t::Auto ) {
		api = OpenQ4_NvrhiBootstrapGetDefaultApi();
	}

	if ( !OpenQ4_NvrhiBootstrapIsApiCompiled( api ) ) {
		error = OpenQ4_NvrhiMakeError(
			std::string( "Requested NVRHI bootstrap backend is not compiled in: " )
			+ OpenQ4_GraphicsApiName( api ) );
		return NULL;
	}

	switch ( api ) {
		case openq4GraphicsApi_t::D3D12:
			return OpenQ4_CreateNvrhiBootstrapD3D12Backend();
		case openq4GraphicsApi_t::Vulkan:
			return OpenQ4_CreateNvrhiBootstrapVulkanBackend();
		default:
			error = OpenQ4_NvrhiMakeError( "No NVRHI bootstrap backend is available." );
			return NULL;
	}
}
