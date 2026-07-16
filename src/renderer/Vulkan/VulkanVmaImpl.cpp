// Copyright (C) 2026 DarkMatter Productions
//
// Vulkan Memory Allocator implementation translation unit. Kept separate so
// the library's warnings and template instantiation stay out of module code.

#if defined( _MSC_VER )
	#pragma warning( push )
	#pragma warning( disable : 4100 4127 4189 4324 4505 )
#elif defined( __GNUC__ ) || defined( __clang__ )
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wunused-parameter"
	#pragma GCC diagnostic ignored "-Wunused-variable"
	#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include "volk.h"

// VMA_STATIC_VULKAN_FUNCTIONS=0 / VMA_DYNAMIC_VULKAN_FUNCTIONS=1 come from
// the build so every translation unit sees one configuration
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#if defined( _MSC_VER )
	#pragma warning( pop )
#elif defined( __GNUC__ ) || defined( __clang__ )
	#pragma GCC diagnostic pop
#endif
