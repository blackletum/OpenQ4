#ifndef OPENQ4_RENDERER_NVRHI_ERROR_H
#define OPENQ4_RENDERER_NVRHI_ERROR_H

#include <string>

inline const char *OpenQ4_NvrhiMakeError( const std::string &message ) {
	static thread_local std::string storage;
	storage = message;
	return storage.c_str();
}

inline const char *OpenQ4_NvrhiMakeError( const char *message ) {
	static thread_local std::string storage;
	storage = ( message != nullptr ) ? message : "Unknown error.";
	return storage.c_str();
}

#endif
