// Copyright (C) 2026 DarkMatter Productions
//

#include "ImageTools.h"

/*
===============================================================================

	Library-owned state for the shared image utilities: static allocation
	(relocated from the renderer's tr_main.cpp) plus the renderer-pushed
	compression capabilities and allocation-counter hooks.

===============================================================================
*/

static imageToolsCompressionCaps_t it_compressionCaps;
static imageToolsAllocHooks_t it_allocHooks;

void ImageTools_SetCompressionCaps( const imageToolsCompressionCaps_t &caps ) {
	it_compressionCaps = caps;
}

const imageToolsCompressionCaps_t &ImageTools_GetCompressionCaps( void ) {
	return it_compressionCaps;
}

void ImageTools_SetStaticAllocHooks( const imageToolsAllocHooks_t &hooks ) {
	it_allocHooks = hooks;
}

/*
=================
R_StaticAlloc
=================
*/
void *R_StaticAlloc( int bytes ) {
	void	*buf;

	if ( it_allocHooks.onStaticAlloc != NULL ) {
		it_allocHooks.onStaticAlloc( bytes );
	}

	buf = Mem_Alloc( bytes );

	// don't exit on failure on zero length allocations since the old code didn't
	if ( !buf && ( bytes != 0 ) ) {
		common->FatalError( "R_StaticAlloc failed on %i bytes", bytes );
	}
	return buf;
}

/*
=================
R_ClearedStaticAlloc
=================
*/
void *R_ClearedStaticAlloc( int bytes ) {
	void	*buf;

	buf = R_StaticAlloc( bytes );
	SIMDProcessor->Memset( buf, 0, bytes );
	return buf;
}

/*
=================
R_StaticFree
=================
*/
void R_StaticFree( void *data ) {
	if ( it_allocHooks.onStaticFree != NULL ) {
		it_allocHooks.onStaticFree();
	}
	Mem_Free( data );
}
