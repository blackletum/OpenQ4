// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __IMAGETOOLS_H__
#define __IMAGETOOLS_H__

/*
===============================================================================

	CPU-side image utilities shared with engine code.

	These functions read, resample, and write raw pixel data without touching
	GPU state. Engine consumers (session compositing, material-type maps, GUI
	minigames) include this header instead of the renderer-internal Image.h;
	when the renderer moves behind the module seam these utilities become a
	static library linked by both sides
	(docs/dev/plans/2026-07-16-vulkan-renderer.md, Phase B2).

===============================================================================
*/

// loads an image file into a static-allocated RGBA8 buffer; *pic receives
// NULL when the image could not be loaded
void	R_LoadImage( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool makePowerOf2 );

byte *	R_ResampleTexture( const byte *in, int inwidth, int inheight, int outwidth, int outheight );

// default arguments stay on the renderer-internal declaration in Image.h
// until the Phase B2 library carve; callers of this header pass all arguments
void	R_WriteTGA( const char *filename, const byte *data, int width, int height, bool flipVertical, const char *basePath );

// malloc with error checking; pairs with R_StaticFree
void *	R_StaticAlloc( int bytes );
void *	R_ClearedStaticAlloc( int bytes );
void	R_StaticFree( void *data );

#endif /* !__IMAGETOOLS_H__ */
