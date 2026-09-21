// Copyright (C) 2026 DarkMatter Productions
#ifndef __GL_PIXEL_TRANSFER_SCOPE_H__
#define __GL_PIXEL_TRANSFER_SCOPE_H__

#include "GLStateCache.h"

// CPU image transfers must not inherit a streaming PBO, row stride, byte swap,
// or pixel offset from another pass. Restore the actual state, including the
// texture bindings used for readback, so diagnostic captures are transparent.
class idGLPixelTransferScope {
public:
	idGLPixelTransferScope() {
		glGetIntegerv( GL_ACTIVE_TEXTURE, &activeTexture );
		R_GLStateCache().ActiveTextureUnit( 0 );
		glGetIntegerv( GL_TEXTURE_BINDING_2D, &texture2D );
		glGetIntegerv( GL_TEXTURE_BINDING_CUBE_MAP, &textureCube );
		glGetIntegerv( GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer );
		glGetIntegerv( GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer );
		R_GLStateCache().BindBuffer( GL_PIXEL_PACK_BUFFER, 0 );
		R_GLStateCache().BindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
		for ( int i = 0; i < 16; ++i ) {
			glGetIntegerv( StoreName( i ), &stores[i] );
			glPixelStorei( StoreName( i ), i % 8 == 0 ? 1 : 0 );
		}
	}
	~idGLPixelTransferScope() {
		for ( int i = 0; i < 16; ++i ) {
			glPixelStorei( StoreName( i ), stores[i] );
		}
		R_GLStateCache().BindBuffer( GL_PIXEL_PACK_BUFFER, packBuffer );
		R_GLStateCache().BindBuffer( GL_PIXEL_UNPACK_BUFFER, unpackBuffer );
		R_GLStateCache().BindTexture( 0, GL_TEXTURE_2D, texture2D );
		R_GLStateCache().BindTexture( 0, GL_TEXTURE_CUBE_MAP, textureCube );
		R_GLStateCache().ActiveTextureUnit( activeTexture - GL_TEXTURE0 );
	}
	static GLenum StoreName( int i ) {
		static const GLenum names[16] = {
			GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS,
			GL_PACK_SKIP_PIXELS, GL_PACK_IMAGE_HEIGHT, GL_PACK_SKIP_IMAGES,
			GL_PACK_SWAP_BYTES, GL_PACK_LSB_FIRST,
			GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS,
			GL_UNPACK_SKIP_PIXELS, GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_IMAGES,
			GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST
		};
		return names[i];
	}
private:
	idGLPixelTransferScope( const idGLPixelTransferScope & ) = delete;
	idGLPixelTransferScope &operator=( const idGLPixelTransferScope & ) = delete;
	GLint activeTexture, texture2D, textureCube, packBuffer, unpackBuffer;
	GLint stores[16];
};

#endif
