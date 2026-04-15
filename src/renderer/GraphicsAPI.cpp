#include "GraphicsAPI.h"

namespace {

static char OpenQ4_ToLowerAscii( char c ) {
	if ( c >= 'A' && c <= 'Z' ) {
		return static_cast<char>( c - 'A' + 'a' );
	}

	return c;
}

static bool OpenQ4_EqualsAsciiNoCase( const char *lhs, const char *rhs ) {
	if ( lhs == nullptr || rhs == nullptr ) {
		return false;
	}

	for ( ; *lhs != '\0' && *rhs != '\0'; ++lhs, ++rhs ) {
		if ( OpenQ4_ToLowerAscii( *lhs ) != OpenQ4_ToLowerAscii( *rhs ) ) {
			return false;
		}
	}

	return *lhs == '\0' && *rhs == '\0';
}

} // namespace

const char *openq4GraphicsApiArgs[] = {
	"auto",
	"opengl",
	"d3d12",
	"vulkan",
	nullptr,
};

const char *openq4GraphicsApiNvrhiArgs[] = {
	"auto",
	"d3d12",
	"vulkan",
	nullptr,
};

const char *openq4GraphicsApiRuntimeArgs[] = {
	"opengl",
	"d3d12",
	"vulkan",
	nullptr,
};

const char *OpenQ4_GraphicsApiName( openq4GraphicsApi_t api ) {
	switch ( api ) {
		case openq4GraphicsApi_t::Auto:
			return "auto";
		case openq4GraphicsApi_t::OpenGL:
			return "opengl";
		case openq4GraphicsApi_t::D3D12:
			return "d3d12";
		case openq4GraphicsApi_t::Vulkan:
			return "vulkan";
		default:
			return "unknown";
	}
}

bool OpenQ4_ParseGraphicsApi( const char *text, openq4GraphicsApi_t &api ) {
	if ( text == nullptr || text[ 0 ] == '\0' ) {
		return false;
	}

	if ( OpenQ4_EqualsAsciiNoCase( text, "auto" ) ) {
		api = openq4GraphicsApi_t::Auto;
		return true;
	}
	if ( OpenQ4_EqualsAsciiNoCase( text, "opengl" ) ) {
		api = openq4GraphicsApi_t::OpenGL;
		return true;
	}
	if ( OpenQ4_EqualsAsciiNoCase( text, "d3d12" ) ) {
		api = openq4GraphicsApi_t::D3D12;
		return true;
	}
	if ( OpenQ4_EqualsAsciiNoCase( text, "vulkan" ) ) {
		api = openq4GraphicsApi_t::Vulkan;
		return true;
	}

	return false;
}

bool OpenQ4_IsExplicitGraphicsApi( openq4GraphicsApi_t api ) {
	return api != openq4GraphicsApi_t::Auto;
}
