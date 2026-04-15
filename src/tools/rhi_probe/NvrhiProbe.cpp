#include <cstdlib>

#include "NvrhiProbe.h"

static bool NvrhiProbe_IsRuntimeChoiceSupported( openq4GraphicsApi_t api ) {
	return api == openq4GraphicsApi_t::Auto
		|| api == openq4GraphicsApi_t::D3D12
		|| api == openq4GraphicsApi_t::Vulkan;
}

static bool ParseInt( const char *value, int &parsedValue ) {
	if ( value == NULL || value[0] == '\0' ) {
		return false;
	}

	char *end = NULL;
	const long parsed = strtol( value, &end, 10 );
	if ( end == value || end == NULL || *end != '\0' ) {
		return false;
	}

	parsedValue = static_cast<int>( parsed );
	return true;
}

static bool ParseSizeArg( const char *value, int &width, int &height ) {
	if ( value == NULL ) {
		return false;
	}

	char *end = NULL;
	const long parsedWidth = strtol( value, &end, 10 );
	if ( end == value || end == NULL || ( *end != 'x' && *end != 'X' ) ) {
		return false;
	}

	char *heightEnd = NULL;
	const long parsedHeight = strtol( end + 1, &heightEnd, 10 );
	if ( heightEnd == end + 1 || heightEnd == NULL || *heightEnd != '\0' ) {
		return false;
	}

	width = static_cast<int>( parsedWidth );
	height = static_cast<int>( parsedHeight );
	return true;
}

bool NvrhiProbe_ParseOptions( int argc, char **argv, nvrhiProbeOptions_t &options, std::string &error ) {
	for ( int i = 1; i < argc; ++i ) {
		const char *arg = argv[ i ];
		if ( !SDL_strcmp( arg, "--help" ) || !SDL_strcmp( arg, "-h" ) ) {
			options.showUsage = true;
			return true;
		}

		if ( !SDL_strcmp( arg, "--hidden" ) ) {
			options.hidden = true;
			continue;
		}

		if ( !SDL_strcmp( arg, "--no-vsync" ) ) {
			options.vsync = false;
			continue;
		}

		if ( !SDL_strncmp( arg, "--api=", 6 ) ) {
			const char *value = arg + 6;
			openq4GraphicsApi_t parsedApi = openq4GraphicsApi_t::Auto;
			if ( !OpenQ4_ParseGraphicsApi( value, parsedApi ) ) {
				error = "Unknown value for --api: " + std::string( value );
				return false;
			}

			if ( !NvrhiProbe_IsRuntimeChoiceSupported( parsedApi ) ) {
				error = "Unsupported value for --api in the NVRHI probe: " + std::string( value );
				return false;
			}

			options.api = parsedApi;
			continue;
		}

		if ( !SDL_strncmp( arg, "--frames=", 9 ) ) {
			if ( !ParseInt( arg + 9, options.frames ) || options.frames < 0 ) {
				error = "Invalid value for --frames: " + std::string( arg + 9 );
				return false;
			}
			continue;
		}

		if ( !SDL_strncmp( arg, "--size=", 7 ) ) {
			if ( !ParseSizeArg( arg + 7, options.width, options.height ) || options.width <= 0 || options.height <= 0 ) {
				error = "Invalid value for --size: " + std::string( arg + 7 );
				return false;
			}
			continue;
		}

		error = "Unknown argument: " + std::string( arg );
		return false;
	}

	return true;
}

void NvrhiProbe_PrintUsage( void ) {
	SDL_Log(
		"OpenQ4 NVRHI probe\n"
		"Usage: OpenQ4-rhi-probe_<arch> [options]\n"
		"  --api=auto|d3d12|vulkan  Select the graphics API (default: auto)\n"
		"  --frames=<count>         Number of frames to render (0 = until closed, default: 300)\n"
		"  --size=<width>x<height>  Initial window size in pixels (default: 1280x720)\n"
		"  --hidden                 Create a hidden window for smoke tests\n"
		"  --no-vsync               Present without v-sync when the backend supports it\n"
		"  --help                   Show this help text\n" );
}
