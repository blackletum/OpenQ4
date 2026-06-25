/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/





#include "tr_local.h"
#include "DXT/DXTCodec.h"

idCVar image_usePrecompressedTextures(
	"image_usePrecompressedTextures",
	"1",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"automatic DDS texture replacements: 0 = off, 1 = any supported DDS format, 2 = BC7/BPTC DDS only",
	0,
	2 );

/*

This file only has a single entry point:

void R_LoadImage( const char *name, byte **pic, int *width, int *height, bool makePowerOf2 );

*/

/*
 * Include file for users of JPEG library.
 * You will need to have included system headers that define at least
 * the typedefs FILE and size_t before you can include jpeglib.h.
 * (stdio.h is sufficient on ANSI-conforming systems.)
 * You may also wish to include "jerror.h".
 */

extern "C" {
#include "jpeg-6/jpeglib.h"


// hooks from jpeg lib to our system

void jpg_Error( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[2048];

	va_start (argptr,fmt);
	idStr::vsnPrintf( msg, sizeof( msg ), fmt, argptr );
	va_end (argptr);

	common->FatalError( "%s", msg );
}

void jpg_Printf( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[2048];

	va_start (argptr,fmt);
	idStr::vsnPrintf( msg, sizeof( msg ), fmt, argptr );
	va_end (argptr);

	common->Printf( "%s", msg );
}
};

/*
================
R_WriteTGA
================
*/
void R_WriteTGA( const char *filename, const byte *data, int width, int height, bool flipVertical, const char * basePath ) {
	byte	*buffer;
	int		i;
	int		bufferSize = width*height*4 + 18;
	int     imgStart = 18;

	idTempArray<byte> buf( bufferSize );
	buffer = (byte *)buf.Ptr();
	memset( buffer, 0, 18 );
	buffer[2] = 2;		// uncompressed type
	buffer[12] = width&255;
	buffer[13] = width>>8;
	buffer[14] = height&255;
	buffer[15] = height>>8;
	buffer[16] = 32;	// pixel size
	if ( !flipVertical ) {
		buffer[17] = (1<<5);	// flip bit, for normal top to bottom raster order
	}

	// swap rgb to bgr
	for ( i=imgStart ; i<bufferSize ; i+=4 ) {
		buffer[i] = data[i-imgStart+2];		// blue
		buffer[i+1] = data[i-imgStart+1];		// green
		buffer[i+2] = data[i-imgStart+0];		// red
		buffer[i+3] = data[i-imgStart+3];		// alpha
	}

	fileSystem->WriteFile( filename, buffer, bufferSize, basePath );
}

static void LoadTGA( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp );
static void LoadJPG( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp );
static void LoadDDS( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool decodeRXGBNormalMap );

/*
========================================================================

TGA files are used for 24/32 bit images

========================================================================
*/

typedef struct _TargaHeader {
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} TargaHeader;


/*
=========================================================

TARGA LOADING

=========================================================
*/

/*
=============
LoadTGA
=============
*/
static void LoadTGA( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp ) {
	int		columns, rows, numPixels, fileSize, numBytes;
	byte	*pixbuf;
	int		row, column;
	byte	*buf_p;
	byte	*buffer;
	TargaHeader	targa_header;
	byte		*targa_rgba;

	if ( !pic ) {
		fileSystem->ReadFile( name, NULL, timestamp );
		return;	// just getting timestamp
	}

	*pic = NULL;

	//
	// load the file
	//
	fileSize = fileSystem->ReadFile( name, (void **)&buffer, timestamp );
	if ( !buffer ) {
		return;
	}

	buf_p = buffer;

	if ( fileSize < 18 ) {
		fileSystem->FreeFile( buffer );
		common->Error( "LoadTGA( %s ): incomplete file\n", name );
	}

	targa_header.id_length = *buf_p++;
	targa_header.colormap_type = *buf_p++;
	targa_header.image_type = *buf_p++;
	
	targa_header.colormap_index = LittleShort ( *(short *)buf_p );
	buf_p += 2;
	targa_header.colormap_length = LittleShort ( *(short *)buf_p );
	buf_p += 2;
	targa_header.colormap_size = *buf_p++;
	targa_header.x_origin = LittleShort ( *(short *)buf_p );
	buf_p += 2;
	targa_header.y_origin = LittleShort ( *(short *)buf_p );
	buf_p += 2;
	targa_header.width = LittleShort ( *(short *)buf_p );
	buf_p += 2;
	targa_header.height = LittleShort ( *(short *)buf_p );
	buf_p += 2;
	targa_header.pixel_size = *buf_p++;
	targa_header.attributes = *buf_p++;

	if ( fileSize < 18 + targa_header.id_length ) {
		fileSystem->FreeFile( buffer );
		common->Error( "LoadTGA( %s ): incomplete file\n", name );
	}

	if ( targa_header.image_type != 2 && targa_header.image_type != 10 && targa_header.image_type != 3 ) {
		common->Error( "LoadTGA( %s ): Only type 2 (RGB), 3 (gray), and 10 (RGB) TGA images supported\n", name );
	}

	if ( targa_header.colormap_type != 0 ) {
		common->Error( "LoadTGA( %s ): colormaps not supported\n", name );
	}

	if ( ( targa_header.pixel_size != 32 && targa_header.pixel_size != 24 ) && targa_header.image_type != 3 ) {
		common->Error( "LoadTGA( %s ): Only 32 or 24 bit images supported (no colormaps)\n", name );
	}

	// width * height * 4 can overflow a 32-bit int on crafted headers, wrapping the
	// allocation below while the decode loops still write the full extent
	if ( (int64)targa_header.width * targa_header.height * 4 > 0x7FFFFFFF ) {
		fileSystem->FreeFile( buffer );
		common->Error( "LoadTGA( %s ): dimensions too large (%i x %i)\n", name, targa_header.width, targa_header.height );
	}

	if ( targa_header.image_type == 2 || targa_header.image_type == 3 ) {
		numBytes = targa_header.width * targa_header.height * ( targa_header.pixel_size >> 3 );
		if ( numBytes > fileSize - 18 - targa_header.id_length ) {
			common->Error( "LoadTGA( %s ): incomplete file\n", name );
		}
	}

	columns = targa_header.width;
	rows = targa_header.height;
	numPixels = columns * rows;

	if ( width ) {
		*width = columns;
	}
	if ( height ) {
		*height = rows;
	}

	targa_rgba = (byte *)R_StaticAlloc(numPixels*4);
	*pic = targa_rgba;

	if ( targa_header.id_length != 0 ) {
		buf_p += targa_header.id_length;  // skip TARGA image comment
	}
	
	if ( targa_header.image_type == 2 || targa_header.image_type == 3 )
	{ 
		// Uncompressed RGB or gray scale image
		for( row = rows - 1; row >= 0; row-- )
		{
			pixbuf = targa_rgba + row*columns*4;
			for( column = 0; column < columns; column++)
			{
				unsigned char red,green,blue,alphabyte;
				switch( targa_header.pixel_size )
				{
					
				case 8:
					blue = *buf_p++;
					green = blue;
					red = blue;
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = 255;
					break;

				case 24:
					blue = *buf_p++;
					green = *buf_p++;
					red = *buf_p++;
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = 255;
					break;
				case 32:
					blue = *buf_p++;
					green = *buf_p++;
					red = *buf_p++;
					alphabyte = *buf_p++;
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = alphabyte;
					break;
				default:
					common->Error( "LoadTGA( %s ): illegal pixel_size '%d'\n", name, targa_header.pixel_size );
					break;
				}
			}
		}
	}
	else if ( targa_header.image_type == 10 ) {   // Runlength encoded RGB images
		unsigned char red,green,blue,alphabyte,packetHeader,packetSize,j;
		const byte	*buf_end = buffer + fileSize;

		red = 0;
		green = 0;
		blue = 0;
		alphabyte = 0xff;

		for( row = rows - 1; row >= 0; row-- ) {
			pixbuf = targa_rgba + row*columns*4;
			for( column = 0; column < columns; ) {
				if ( buf_p >= buf_end ) {
					R_StaticFree( targa_rgba );
					*pic = NULL;
					fileSystem->FreeFile( buffer );
					common->Error( "LoadTGA( %s ): incomplete file\n", name );
				}
				packetHeader= *buf_p++;
				packetSize = 1 + (packetHeader & 0x7f);
				if ( packetHeader & 0x80 ) {        // run-length packet
					if ( buf_p + ( targa_header.pixel_size >> 3 ) > buf_end ) {
						R_StaticFree( targa_rgba );
						*pic = NULL;
						fileSystem->FreeFile( buffer );
						common->Error( "LoadTGA( %s ): incomplete file\n", name );
					}
					switch( targa_header.pixel_size ) {
						case 24:
								blue = *buf_p++;
								green = *buf_p++;
								red = *buf_p++;
								alphabyte = 255;
								break;
						case 32:
								blue = *buf_p++;
								green = *buf_p++;
								red = *buf_p++;
								alphabyte = *buf_p++;
								break;
						default:
							common->Error( "LoadTGA( %s ): illegal pixel_size '%d'\n", name, targa_header.pixel_size );
							break;
					}
	
					for( j = 0; j < packetSize; j++ ) {
						*pixbuf++=red;
						*pixbuf++=green;
						*pixbuf++=blue;
						*pixbuf++=alphabyte;
						column++;
						if ( column == columns ) { // run spans across rows
							column = 0;
							if ( row > 0) {
								row--;
							}
							else {
								goto breakOut;
							}
							pixbuf = targa_rgba + row*columns*4;
						}
					}
				}
				else {                            // non run-length packet
					if ( buf_p + packetSize * ( targa_header.pixel_size >> 3 ) > buf_end ) {
						R_StaticFree( targa_rgba );
						*pic = NULL;
						fileSystem->FreeFile( buffer );
						common->Error( "LoadTGA( %s ): incomplete file\n", name );
					}
					for( j = 0; j < packetSize; j++ ) {
						switch( targa_header.pixel_size ) {
							case 24:
									blue = *buf_p++;
									green = *buf_p++;
									red = *buf_p++;
									*pixbuf++ = red;
									*pixbuf++ = green;
									*pixbuf++ = blue;
									*pixbuf++ = 255;
									break;
							case 32:
									blue = *buf_p++;
									green = *buf_p++;
									red = *buf_p++;
									alphabyte = *buf_p++;
									*pixbuf++ = red;
									*pixbuf++ = green;
									*pixbuf++ = blue;
									*pixbuf++ = alphabyte;
									break;
							default:
								common->Error( "LoadTGA( %s ): illegal pixel_size '%d'\n", name, targa_header.pixel_size );
								break;
						}
						column++;
						if ( column == columns ) { // pixel packet run spans across rows
							column = 0;
							if ( row > 0 ) {
								row--;
							}
							else {
								goto breakOut;
							}
							pixbuf = targa_rgba + row*columns*4;
						}						
					}
				}
			}
			breakOut: ;
		}
	}

	if ( (targa_header.attributes & (1<<5)) ) {			// image flp bit
		if ( width != NULL && height != NULL ) {
			R_VerticalFlip( *pic, *width, *height );
		}
	}

	fileSystem->FreeFile( buffer );
}

/*
=========================================================

JPG LOADING

Interfaces with the huge libjpeg
=========================================================
*/

/*
=============
LoadJPG
=============
*/
static void LoadJPG( const char *filename, unsigned char **pic, int *width, int *height, ID_TIME_T *timestamp ) {
  /* This struct contains the JPEG decompression parameters and pointers to
   * working space (which is allocated as needed by the JPEG library).
   */
  struct jpeg_decompress_struct cinfo;
  /* We use our private extension JPEG error handler.
   * Note that this struct must live as long as the main JPEG parameter
   * struct, to avoid dangling-pointer problems.
   */
  /* This struct represents a JPEG error handler.  It is declared separately
   * because applications often want to supply a specialized error handler
   * (see the second half of this file for an example).  But here we just
   * take the easy way out and use the standard error handler, which will
   * print a message on stderr and call exit() if compression fails.
   * Note that this struct must live as long as the main JPEG parameter
   * struct, to avoid dangling-pointer problems.
   */
  struct jpeg_error_mgr jerr;
  /* More stuff */
  JSAMPARRAY buffer;		/* Output row buffer */
  int row_stride;		/* physical row width in output buffer */
  unsigned char *out;
  byte	*fbuffer;
  byte  *bbuf;

  /* In this example we want to open the input file before doing anything else,
   * so that the setjmp() error recovery below can assume the file is open.
   * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
   * requires it in order to read binary files.
   */

	// JDC: because fill_input_buffer() blindly copies INPUT_BUF_SIZE bytes,
	// we need to make sure the file buffer is padded or it may crash
  if ( pic ) {
	*pic = NULL;		// until proven otherwise
  }
  {
		int		len;
		idFile *f;

		f = fileSystem->OpenFileRead( filename );
		if ( !f ) {
			return;
		}
		len = f->Length();
		if ( timestamp ) {
			*timestamp = f->Timestamp();
		}
		if ( !pic ) {
			fileSystem->CloseFile( f );
			return;	// just getting timestamp
		}
		fbuffer = (byte *)Mem_ClearedAlloc( len + 4096 );
		f->Read( fbuffer, len );
		fileSystem->CloseFile( f );
  }


  /* Step 1: allocate and initialize JPEG decompression object */

  /* We have to set up the error handler first, in case the initialization
   * step fails.  (Unlikely, but it could happen if you are out of memory.)
   * This routine fills in the contents of struct jerr, and returns jerr's
   * address which we place into the link field in cinfo.
   */
  cinfo.err = jpeg_std_error(&jerr);

  /* Now we can initialize the JPEG decompression object. */
  jpeg_create_decompress(&cinfo);

  /* Step 2: specify data source (eg, a file) */

  jpeg_stdio_src(&cinfo, fbuffer);

  /* Step 3: read file parameters with jpeg_read_header() */

  (void) jpeg_read_header(&cinfo, true );
  /* We can ignore the return value from jpeg_read_header since
   *   (a) suspension is not possible with the stdio data source, and
   *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
   * See libjpeg.doc for more info.
   */

  /* Step 4: set parameters for decompression */

  /* In this example, we don't need to change any of the defaults set by
   * jpeg_read_header(), so we do nothing here.
   */

  /* Step 5: Start decompressor */

  (void) jpeg_start_decompress(&cinfo);
  /* We can ignore the return value since suspension is not possible
   * with the stdio data source.
   */

  /* We may need to do some setup of our own at this point before reading
   * the data.  After jpeg_start_decompress() we have the correct scaled
   * output image dimensions available, as well as the output colormap
   * if we asked for color quantization.
   * In this example, we need to make an output work buffer of the right size.
   */ 
  /* JSAMPLEs per row in output buffer */
  row_stride = cinfo.output_width * cinfo.output_components;

  if (cinfo.output_components!=4) {
		common->DWarning( "JPG %s is unsupported color depth (%d)", 
			filename, cinfo.output_components);
  }
  out = (byte *)R_StaticAlloc(cinfo.output_width*cinfo.output_height*4);

  *pic = out;
  *width = cinfo.output_width;
  *height = cinfo.output_height;

  /* Step 6: while (scan lines remain to be read) */
  /*           jpeg_read_scanlines(...); */

  /* Here we use the library's state variable cinfo.output_scanline as the
   * loop counter, so that we don't have to keep track ourselves.
   */
  while (cinfo.output_scanline < cinfo.output_height) {
    /* jpeg_read_scanlines expects an array of pointers to scanlines.
     * Here the array is only one element long, but you could ask for
     * more than one scanline at a time if that's more convenient.
     */
	bbuf = ((out+(row_stride*cinfo.output_scanline)));
	buffer = &bbuf;
    (void) jpeg_read_scanlines(&cinfo, buffer, 1);
  }

  // clear all the alphas to 255
  {
	  int	i, j;
		byte	*buf;

		buf = *pic;

	  j = cinfo.output_width * cinfo.output_height * 4;
	  for ( i = 3 ; i < j ; i+=4 ) {
		  buf[i] = 255;
	  }
  }

  /* Step 7: Finish decompression */

  (void) jpeg_finish_decompress(&cinfo);
  /* We can ignore the return value since suspension is not possible
   * with the stdio data source.
   */

  /* Step 8: Release JPEG decompression object */

  /* This is an important step since it will release a good deal of memory. */
  jpeg_destroy_decompress(&cinfo);

  /* After finish_decompress, we can close the input file.
   * Here we postpone it until after no more JPEG errors are possible,
   * so as to simplify the setjmp error logic above.  (Actually, I don't
   * think that jpeg_destroy can do an error exit, but why assume anything...)
   */
  Mem_Free( fbuffer );

  /* At this point you may want to check to see whether any corrupt-data
   * warnings occurred (test whether jerr.pub.num_warnings is nonzero).
   */

  /* And we're done! */
}

//===================================================================

static ID_INLINE uint32 R_ReadLittleUInt32( const byte *data ) {
	return ( (uint32)data[0] ) | ( (uint32)data[1] << 8 ) | ( (uint32)data[2] << 16 ) | ( (uint32)data[3] << 24 );
}

static ID_INLINE uint32 R_MakeFourCC( const char a, const char b, const char c, const char d ) {
	return ( (uint32)(byte)a ) | ( (uint32)(byte)b << 8 ) | ( (uint32)(byte)c << 16 ) | ( (uint32)(byte)d << 24 );
}

static const uint32 DDS_PIXELFORMAT_FOURCC = 0x00000004;
static const uint32 DDS_HEADER_FLAG_MIPMAPCOUNT = 0x00020000;
static const int DDS_HEADER_BYTES = 128;
static const int DDS_DXT10_HEADER_BYTES = 20;
static const int DDS_DXGI_FORMAT_BC7_UNORM = 98;
static const int DDS_DXGI_FORMAT_BC7_UNORM_SRGB = 99;
static const int DDS_DX10_RESOURCE_DIMENSION_TEXTURE2D = 3;
static const uint32 DDS_DX10_RESOURCE_MISC_TEXTURECUBE = 0x00000004;
static const int DDS_MAX_MIP_LEVELS = 32;
static const uint32 DDS_CAPS2_CUBEMAP = 0x00000200;
static const uint32 DDS_CAPS2_VOLUME = 0x00200000;

enum ddsStoredFormat_t {
	DDS_STORED_FORMAT_INVALID,
	DDS_STORED_FORMAT_DXT1,
	DDS_STORED_FORMAT_DXT5,
	DDS_STORED_FORMAT_BC7
};

static int R_DDSBlockSizeForStoredFormat( ddsStoredFormat_t format ) {
	switch ( format ) {
		case DDS_STORED_FORMAT_DXT1:
			return 8;
		case DDS_STORED_FORMAT_DXT5:
		case DDS_STORED_FORMAT_BC7:
			return 16;
		default:
			return 0;
	}
}

static int R_DDSMaxMipLevelsForSize( uint32 width, uint32 height ) {
	if ( width == 0 || height == 0 ) {
		return 0;
	}

	int levels = 1;
	while ( ( width > 1 || height > 1 ) && levels < DDS_MAX_MIP_LEVELS ) {
		width = Max( (uint32)1, width >> 1 );
		height = Max( (uint32)1, height >> 1 );
		levels++;
	}
	return levels;
}

static int R_DDSStoredMipLevelCount( uint32 headerFlags, uint32 mipMapCount, uint32 width, uint32 height ) {
	if ( ( headerFlags & DDS_HEADER_FLAG_MIPMAPCOUNT ) == 0 || mipMapCount == 0 ) {
		return 1;
	}

	const int maxLevelsForSize = R_DDSMaxMipLevelsForSize( width, height );
	if ( maxLevelsForSize <= 0 ) {
		return 0;
	}

	const int headerLevels = mipMapCount > (uint32)DDS_MAX_MIP_LEVELS ? DDS_MAX_MIP_LEVELS : (int)mipMapCount;
	return Min( headerLevels, maxLevelsForSize );
}

static bool R_DDSLevelSizeIsAvailable( int fileSize, int dataOffset, uint32 width, uint32 height, ddsStoredFormat_t format ) {
	const int blockSize = R_DDSBlockSizeForStoredFormat( format );
	if ( blockSize <= 0 || width == 0 || height == 0 || width > 0x7fffffff || height > 0x7fffffff ) {
		return false;
	}
	const int64 blocksWide = Max( (int64)1, ( (int64)width + 3 ) >> 2 );
	const int64 blocksHigh = Max( (int64)1, ( (int64)height + 3 ) >> 2 );
	const int64 levelSize = blocksWide * blocksHigh * blockSize;
	return levelSize > 0 && levelSize <= 0x7fffffff && dataOffset >= 0 && dataOffset <= fileSize - (int)levelSize;
}

static bool R_DDSComputeLevelLayout( int fileSize, int dataOffset, uint32 width, uint32 height, ddsStoredFormat_t format, int numLevels, idList<int> *levelOffsets, idList<int> *levelSizes ) {
	const int blockSize = R_DDSBlockSizeForStoredFormat( format );
	if ( blockSize <= 0 || numLevels <= 0 || numLevels > DDS_MAX_MIP_LEVELS || width == 0 || height == 0 ||
		 width > 0x7fffffff || height > 0x7fffffff ) {
		return false;
	}

	if ( levelOffsets != NULL ) {
		levelOffsets->SetNum( numLevels );
	}
	if ( levelSizes != NULL ) {
		levelSizes->SetNum( numLevels );
	}

	int offset = dataOffset;
	uint32 levelWidth = width;
	uint32 levelHeight = height;
	for ( int level = 0; level < numLevels; level++ ) {
		const int64 blocksWide = Max( (int64)1, ( (int64)levelWidth + 3 ) >> 2 );
		const int64 blocksHigh = Max( (int64)1, ( (int64)levelHeight + 3 ) >> 2 );
		const int64 levelSize = blocksWide * blocksHigh * blockSize;
		if ( levelSize <= 0 || levelSize > 0x7fffffff || offset < 0 || offset > fileSize - (int)levelSize ) {
			return false;
		}

		if ( levelOffsets != NULL ) {
			( *levelOffsets )[ level ] = offset;
		}
		if ( levelSizes != NULL ) {
			( *levelSizes )[ level ] = (int)levelSize;
		}

		offset += (int)levelSize;
		levelWidth = Max( (uint32)1, levelWidth >> 1 );
		levelHeight = Max( (uint32)1, levelHeight >> 1 );
	}

	return true;
}

static ddsStoredFormat_t R_GetDDSStoredFormatFromBuffer( const byte *buffer, int fileSize, int *dataOffset, uint32 *width, uint32 *height, uint32 *mipMapCount ) {
	if ( buffer == NULL || fileSize < DDS_HEADER_BYTES ) {
		return DDS_STORED_FORMAT_INVALID;
	}

	const uint32 magic = R_ReadLittleUInt32( buffer + 0 );
	const uint32 headerSize = R_ReadLittleUInt32( buffer + 4 );
	const uint32 ddsHeight = R_ReadLittleUInt32( buffer + 12 );
	const uint32 ddsWidth = R_ReadLittleUInt32( buffer + 16 );
	const uint32 ddsMipMapCount = R_ReadLittleUInt32( buffer + 28 );
	const uint32 pixelFormatSize = R_ReadLittleUInt32( buffer + 76 );
	const uint32 pixelFormatFlags = R_ReadLittleUInt32( buffer + 80 );
	const uint32 fourCC = R_ReadLittleUInt32( buffer + 84 );
	const uint32 caps2 = R_ReadLittleUInt32( buffer + 112 );

	if ( magic != R_MakeFourCC( 'D', 'D', 'S', ' ' ) ||
		 headerSize != 124 ||
		 pixelFormatSize != 32 ||
		 ddsWidth == 0 ||
		 ddsHeight == 0 ||
		 ddsWidth > 0x7fffffff ||
		 ddsHeight > 0x7fffffff ||
		 ( caps2 & ( DDS_CAPS2_CUBEMAP | DDS_CAPS2_VOLUME ) ) != 0 ||
		 ( pixelFormatFlags & DDS_PIXELFORMAT_FOURCC ) == 0 ) {
		return DDS_STORED_FORMAT_INVALID;
	}

	int localDataOffset = DDS_HEADER_BYTES;
	ddsStoredFormat_t format = DDS_STORED_FORMAT_INVALID;
	if ( fourCC == R_MakeFourCC( 'D', 'X', 'T', '1' ) ) {
		format = DDS_STORED_FORMAT_DXT1;
	} else if ( fourCC == R_MakeFourCC( 'D', 'X', 'T', '5' ) ) {
		format = DDS_STORED_FORMAT_DXT5;
	} else if ( fourCC == R_MakeFourCC( 'D', 'X', '1', '0' ) ) {
		if ( fileSize < DDS_HEADER_BYTES + DDS_DXT10_HEADER_BYTES ) {
			return DDS_STORED_FORMAT_INVALID;
		}
		const uint32 dxgiFormat = R_ReadLittleUInt32( buffer + DDS_HEADER_BYTES );
		const uint32 resourceDimension = R_ReadLittleUInt32( buffer + DDS_HEADER_BYTES + 4 );
		const uint32 miscFlag = R_ReadLittleUInt32( buffer + DDS_HEADER_BYTES + 8 );
		const uint32 arraySize = R_ReadLittleUInt32( buffer + DDS_HEADER_BYTES + 12 );
		if ( ( dxgiFormat == DDS_DXGI_FORMAT_BC7_UNORM || dxgiFormat == DDS_DXGI_FORMAT_BC7_UNORM_SRGB ) &&
			 resourceDimension == DDS_DX10_RESOURCE_DIMENSION_TEXTURE2D &&
			 ( miscFlag & DDS_DX10_RESOURCE_MISC_TEXTURECUBE ) == 0 &&
			 arraySize == 1 ) {
			format = DDS_STORED_FORMAT_BC7;
			localDataOffset += DDS_DXT10_HEADER_BYTES;
		}
	} else if ( fourCC == R_MakeFourCC( 'B', 'C', '7', '0' ) ||
				fourCC == R_MakeFourCC( 'B', 'C', '7', 'L' ) ||
				fourCC == R_MakeFourCC( 'B', 'C', '7', ' ' ) ) {
		format = DDS_STORED_FORMAT_BC7;
	}

	if ( !R_DDSLevelSizeIsAvailable( fileSize, localDataOffset, ddsWidth, ddsHeight, format ) ) {
		return DDS_STORED_FORMAT_INVALID;
	}

	if ( dataOffset != NULL ) {
		*dataOffset = localDataOffset;
	}
	if ( width != NULL ) {
		*width = ddsWidth;
	}
	if ( height != NULL ) {
		*height = ddsHeight;
	}
	if ( mipMapCount != NULL ) {
		*mipMapCount = ddsMipMapCount;
	}
	return format;
}

static ddsStoredFormat_t R_GetDDSStoredFormatFromFile( const char *name, ID_TIME_T *timestamp, bool requireCompleteMipChain = false ) {
	byte *buffer = NULL;
	const int fileSize = fileSystem->ReadFile( name, (void **)&buffer, timestamp );
	if ( buffer == NULL ) {
		return DDS_STORED_FORMAT_INVALID;
	}
	int dataOffset = DDS_HEADER_BYTES;
	uint32 ddsWidth = 0;
	uint32 ddsHeight = 0;
	uint32 mipMapCount = 0;
	ddsStoredFormat_t format = R_GetDDSStoredFormatFromBuffer( buffer, fileSize, &dataOffset, &ddsWidth, &ddsHeight, &mipMapCount );
	if ( format != DDS_STORED_FORMAT_INVALID && requireCompleteMipChain ) {
		const uint32 headerFlags = R_ReadLittleUInt32( buffer + 8 );
		const int numLevels = R_DDSStoredMipLevelCount( headerFlags, mipMapCount, ddsWidth, ddsHeight );
		if ( numLevels <= 0 || !R_DDSComputeLevelLayout( fileSize, dataOffset, ddsWidth, ddsHeight, format, numLevels, NULL, NULL ) ) {
			format = DDS_STORED_FORMAT_INVALID;
		}
	}
	fileSystem->FreeFile( buffer );
	return format;
}

static bool R_ImageNameHasDDSShadowPrefix( const idStr &name ) {
	return name.Length() >= 4 && name[0] == 'd' && name[1] == 'd' && name[2] == 's' && name[3] == '/';
}

static bool R_TryResolvePreferredDDSImageSource( const idStr &candidateName, idStr &ddsName, ID_TIME_T *timestamp, bool allowPrecompressedDDS, bool *precompressedDDS ) {
	ID_TIME_T ddsTimestamp = FILE_NOT_FOUND_TIMESTAMP;
	const ddsStoredFormat_t format = R_GetDDSStoredFormatFromFile( candidateName.c_str(), &ddsTimestamp );
	const int usePrecompressedTextures = image_usePrecompressedTextures.GetInteger();

	if ( format == DDS_STORED_FORMAT_DXT1 || format == DDS_STORED_FORMAT_DXT5 ) {
		if ( usePrecompressedTextures >= 2 ) {
			return false;
		}
		ddsName = candidateName;
		if ( timestamp != NULL ) {
			*timestamp = ddsTimestamp;
		}
		if ( precompressedDDS != NULL ) {
			*precompressedDDS = false;
		}
		return true;
	}

	if ( format == DDS_STORED_FORMAT_BC7 ) {
		if ( !allowPrecompressedDDS || !glConfig.bptcTextureCompressionAvailable ) {
			return false;
		}
		if ( R_GetDDSStoredFormatFromFile( candidateName.c_str(), NULL, true ) != DDS_STORED_FORMAT_BC7 ) {
			return false;
		}
		ddsName = candidateName;
		if ( timestamp != NULL ) {
			*timestamp = ddsTimestamp;
		}
		if ( precompressedDDS != NULL ) {
			*precompressedDDS = true;
		}
		return true;
	}

	return false;
}

/*
=============
R_ResolvePreferredDDSImageSource
=============
*/
bool R_ResolvePreferredDDSImageSource( const char *cname, idStr &ddsName, ID_TIME_T *timestamp, bool allowPrecompressedDDS, bool *precompressedDDS ) {
	if ( precompressedDDS != NULL ) {
		*precompressedDDS = false;
	}
	if ( timestamp != NULL ) {
		*timestamp = FILE_NOT_FOUND_TIMESTAMP;
	}
	if ( cname == NULL || cname[0] == '\0' ) {
		return false;
	}

	if ( image_usePrecompressedTextures.GetInteger() <= 0 ) {
		return false;
	}

	idStr sourceName = cname;
	sourceName.DefaultFileExtension( ".tga" );
	sourceName.BackSlashesToSlashes();
	sourceName.ToLower();

	idStr ext;
	sourceName.ExtractFileExtension( ext );
	if ( ext != "tga" && ext != "jpg" ) {
		return false;
	}

	if ( !R_ImageNameHasDDSShadowPrefix( sourceName ) ) {
		idStr shadowDDSName = "dds/";
		shadowDDSName += sourceName;
		shadowDDSName.StripFileExtension();
		shadowDDSName.DefaultFileExtension( ".dds" );
		if ( R_TryResolvePreferredDDSImageSource( shadowDDSName, ddsName, timestamp, allowPrecompressedDDS, precompressedDDS ) ) {
			return true;
		}
	}

	idStr siblingDDSName = sourceName;
	siblingDDSName.StripFileExtension();
	siblingDDSName.DefaultFileExtension( ".dds" );
	return R_TryResolvePreferredDDSImageSource( siblingDDSName, ddsName, timestamp, allowPrecompressedDDS, precompressedDDS );
}

/*
=============
R_LoadPrecompressedDDS
=============
*/
bool R_LoadPrecompressedDDS( const char *cname, idBinaryImage &image, ID_TIME_T *timestamp, textureUsage_t usage ) {
	if ( cname == NULL || cname[0] == '\0' ) {
		return false;
	}

	idStr name = cname;
	name.ToLower();

	idStr ext;
	name.ExtractFileExtension( ext );
	if ( ext != "dds" ) {
		return false;
	}

	byte *buffer = NULL;
	const int fileSize = fileSystem->ReadFile( name.c_str(), (void **)&buffer, timestamp );
	if ( buffer == NULL || fileSize < DDS_HEADER_BYTES ) {
		if ( buffer != NULL ) {
			fileSystem->FreeFile( buffer );
		}
		return false;
	}

	bool loaded = false;
	do {
		const uint32 headerFlags = R_ReadLittleUInt32( buffer + 8 );
		int dataOffset = DDS_HEADER_BYTES;
		uint32 ddsWidth = 0;
		uint32 ddsHeight = 0;
		uint32 mipMapCount = 0;
		const ddsStoredFormat_t parsedFormat = R_GetDDSStoredFormatFromBuffer( buffer, fileSize, &dataOffset, &ddsWidth, &ddsHeight, &mipMapCount );
		if ( parsedFormat != DDS_STORED_FORMAT_BC7 ) {
			break;
		}

		if ( !glConfig.bptcTextureCompressionAvailable ) {
			idLib::Warning( "Image file '%s' uses BC7/BPTC DDS data, but this OpenGL renderer does not support GL_ARB_texture_compression_bptc", name.c_str() );
			break;
		}

		const int numLevels = R_DDSStoredMipLevelCount( headerFlags, mipMapCount, ddsWidth, ddsHeight );
		if ( numLevels <= 0 ) {
			idLib::Warning( "Image file '%s' has invalid BC7/BPTC DDS mip metadata", name.c_str() );
			break;
		}
		idList<int> levelOffsets;
		idList<int> levelSizes;
		if ( !R_DDSComputeLevelLayout( fileSize, dataOffset, ddsWidth, ddsHeight, DDS_STORED_FORMAT_BC7, numLevels, &levelOffsets, &levelSizes ) ) {
			idLib::Warning( "Image file '%s' has an incomplete BC7/BPTC DDS mip chain", name.c_str() );
			break;
		}

		const textureColor_t colorFormat = usage == TD_BUMP ? CFM_NORMAL_DXT5 : CFM_DEFAULT;
		image.Load2DFromCompressedData( (int)ddsWidth, (int)ddsHeight, numLevels, FMT_BC7, colorFormat, buffer, levelOffsets.Ptr(), levelSizes.Ptr() );
		loaded = true;
	} while ( false );

	fileSystem->FreeFile( buffer );
	return loaded;
}

/*
=============
LoadDDS
=============
*/
static void LoadDDS( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool decodeRXGBNormalMap ) {
	int fileSize;
	byte *buffer;

	if ( !pic ) {
		fileSystem->ReadFile( name, NULL, timestamp );
		return;	// just getting timestamp
	}

	*pic = NULL;

	fileSize = fileSystem->ReadFile( name, (void **)&buffer, timestamp );
	if ( !buffer ) {
		return;
	}

	// DDS magic + 124-byte header
	if ( fileSize < 128 ) {
		fileSystem->FreeFile( buffer );
		return;
	}

	const uint32 magic = R_ReadLittleUInt32( buffer + 0 );
	const uint32 headerSize = R_ReadLittleUInt32( buffer + 4 );
	if ( magic != R_MakeFourCC( 'D', 'D', 'S', ' ' ) || headerSize != 124 ) {
		fileSystem->FreeFile( buffer );
		return;
	}

	const uint32 ddsHeight = R_ReadLittleUInt32( buffer + 12 );
	const uint32 ddsWidth = R_ReadLittleUInt32( buffer + 16 );
	const uint32 pixelFormatFlags = R_ReadLittleUInt32( buffer + 80 );
	const uint32 fourCC = R_ReadLittleUInt32( buffer + 84 );

	if ( ddsWidth == 0 || ddsHeight == 0 || ( pixelFormatFlags & 0x4 ) == 0 ) {
		fileSystem->FreeFile( buffer );
		return;
	}

	int blockSize = 0;
	enum ddsCompression_t {
		DDS_COMPRESSION_DXT1,
		DDS_COMPRESSION_DXT5
	} compression;

	if ( fourCC == R_MakeFourCC( 'D', 'X', 'T', '1' ) ) {
		blockSize = 8;
		compression = DDS_COMPRESSION_DXT1;
	} else if ( fourCC == R_MakeFourCC( 'D', 'X', 'T', '5' ) ) {
		blockSize = 16;
		compression = DDS_COMPRESSION_DXT5;
	} else {
		fileSystem->FreeFile( buffer );
		return;
	}

	const int blocksWide = Max( 1, (int)( ( ddsWidth + 3 ) >> 2 ) );
	const int blocksHigh = Max( 1, (int)( ( ddsHeight + 3 ) >> 2 ) );
	const int levelSize = blocksWide * blocksHigh * blockSize;
	const int dataOffset = 128;

	if ( fileSize < ( dataOffset + levelSize ) ) {
		fileSystem->FreeFile( buffer );
		return;
	}

	const int realWidth = (int)ddsWidth;
	const int realHeight = (int)ddsHeight;
	const int paddedWidth = blocksWide * 4;
	const int paddedHeight = blocksHigh * 4;
	const int64 realBytes = (int64)realWidth * realHeight * 4;
	const int64 paddedBytes = (int64)paddedWidth * paddedHeight * 4;
	if ( realBytes <= 0 || realBytes > 0x7FFFFFFF || paddedBytes <= 0 || paddedBytes > 0x7FFFFFFF ) {
		fileSystem->FreeFile( buffer );
		return;
	}

	byte *decodedRgba = (byte *)R_StaticAlloc( (int)paddedBytes );
	idDxtDecoder decoder;
	if ( compression == DDS_COMPRESSION_DXT1 ) {
		decoder.DecompressImageDXT1( buffer + dataOffset, decodedRgba, paddedWidth, paddedHeight );
	} else if ( decodeRXGBNormalMap ) {
		decoder.DecompressNormalMapDXT5( buffer + dataOffset, decodedRgba, paddedWidth, paddedHeight );
	} else {
		decoder.DecompressImageDXT5( buffer + dataOffset, decodedRgba, paddedWidth, paddedHeight );
	}

	byte *ddsRgba = decodedRgba;
	if ( paddedWidth != realWidth || paddedHeight != realHeight ) {
		ddsRgba = (byte *)R_StaticAlloc( (int)realBytes );
		for ( int y = 0; y < realHeight; y++ ) {
			memcpy( ddsRgba + y * realWidth * 4, decodedRgba + y * paddedWidth * 4, realWidth * 4 );
		}
		R_StaticFree( decodedRgba );
	}

	*pic = ddsRgba;
	if ( width ) {
		*width = realWidth;
	}
	if ( height ) {
		*height = realHeight;
	}

	fileSystem->FreeFile( buffer );
}

//===================================================================

static bool R_LoadImageSucceeded( byte **pic, ID_TIME_T *timestamp ) {
	if ( pic != NULL ) {
		return *pic != NULL;
	}
	if ( timestamp != NULL ) {
		return *timestamp != FILE_NOT_FOUND_TIMESTAMP;
	}
	return false;
}

/*
=================
R_LoadImage

Loads any of the supported image types into a cannonical
32 bit format.

Automatically attempts to load .jpg / .dds files if .tga files fail to load.

*pic will be NULL if the load failed.

Anything that is going to make this into a texture would use
makePowerOf2 = true, but something loading an image as a lookup
table of some sort would leave it in identity form.

It is important to do this at image load time instead of texture load
time for bump maps.

Timestamp may be NULL if the value is going to be ignored

If pic is NULL, the image won't actually be loaded, it will just find the
timestamp.
=================
*/
static void R_LoadImageInternal( const char *cname, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool makePowerOf2, textureUsage_t usage ) {
	idStr name = cname;
	const bool decodeRXGBNormalMap = ( usage == TD_BUMP );

	if ( pic ) {
		*pic = NULL;
	}
	if ( timestamp ) {
		*timestamp = FILE_NOT_FOUND_TIMESTAMP;
	}
	if ( width ) {
		*width = 0;
	}
	if ( height ) {
		*height = 0;
	}

	name.DefaultFileExtension( ".tga" );

	if (name.Length()<5) {
		return;
	}

	name.ToLower();
	idStr ext;
	name.ExtractFileExtension( ext );

	idStr preferredDDSName;
	if ( ext != "dds" && R_ResolvePreferredDDSImageSource( name.c_str(), preferredDDSName, NULL, false, NULL ) ) {
		LoadDDS( preferredDDSName.c_str(), pic, width, height, timestamp, decodeRXGBNormalMap );
		if ( R_LoadImageSucceeded( pic, timestamp ) ) {
			return;
		}
	}

	if ( ext == "tga" ) {
		LoadTGA( name.c_str(), pic, width, height, timestamp );            // try tga first
		if ( ( pic && *pic == 0 ) || ( timestamp && *timestamp == -1 ) ) { //-V595
			name.StripFileExtension();
			name.DefaultFileExtension( ".jpg" );
			LoadJPG( name.c_str(), pic, width, height, timestamp );
		}
		if ( ( pic && *pic == 0 ) || ( timestamp && *timestamp == -1 ) ) { //-V595
			name.StripFileExtension();
			name.DefaultFileExtension( ".dds" );
			LoadDDS( name.c_str(), pic, width, height, timestamp, decodeRXGBNormalMap );
		}
	} else if ( ext == "jpg" ) {
		LoadJPG( name.c_str(), pic, width, height, timestamp );
		if ( ( pic && *pic == 0 ) || ( timestamp && *timestamp == -1 ) ) { //-V595
			name.StripFileExtension();
			name.DefaultFileExtension( ".dds" );
			LoadDDS( name.c_str(), pic, width, height, timestamp, decodeRXGBNormalMap );
		}
	} else if ( ext == "dds" ) {
		LoadDDS( name.c_str(), pic, width, height, timestamp, decodeRXGBNormalMap );
	}

	if ( ( width && *width < 1 ) || ( height && *height < 1 ) ) {
		if ( pic && *pic ) {
			R_StaticFree( *pic );
			*pic = 0;
		}
	}

	//
	// convert to exact power of 2 sizes
	//
	/*
	if ( pic && *pic && makePowerOf2 ) {
		int		w, h;
		int		scaled_width, scaled_height;
		byte	*resampledBuffer;

		w = *width;
		h = *height;

		for (scaled_width = 1 ; scaled_width < w ; scaled_width<<=1)
			;
		for (scaled_height = 1 ; scaled_height < h ; scaled_height<<=1)
			;

		if ( scaled_width != w || scaled_height != h ) {
			resampledBuffer = R_ResampleTexture( *pic, w, h, scaled_width, scaled_height );
			R_StaticFree( *pic );
			*pic = resampledBuffer;
			*width = scaled_width;
			*height = scaled_height;
		}
	}
	*/
}

void R_LoadImage( const char *cname, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool makePowerOf2 ) {
	R_LoadImageInternal( cname, pic, width, height, timestamp, makePowerOf2, TD_DEFAULT );
}

void R_LoadImageForUsage( const char *cname, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool makePowerOf2, textureUsage_t usage ) {
	R_LoadImageInternal( cname, pic, width, height, timestamp, makePowerOf2, usage );
}


/*
=======================
R_LoadCubeImages

Loads six files with proper extensions
=======================
*/
bool R_LoadCubeImages( const char *imgName, cubeFiles_t extensions, byte *pics[6], int *outSize, ID_TIME_T *timestamp ) {
	int		i, j;
	char	*cameraSides[6] =  { "_forward.tga", "_back.tga", "_left.tga", "_right.tga", 
		"_up.tga", "_down.tga" };
	char	*axisSides[6] =  { "_px.tga", "_nx.tga", "_py.tga", "_ny.tga", 
		"_pz.tga", "_nz.tga" };
	char	**sides;
	char	fullName[MAX_IMAGE_NAME];
	int		width, height, size = 0;

	if ( extensions == CF_CAMERA ) {
		sides = cameraSides;
	} else {
		sides = axisSides;
	}

	// FIXME: precompressed cube map files
	if ( pics ) {
		memset( pics, 0, 6*sizeof(pics[0]) );
	}
	if ( timestamp ) {
		*timestamp = 0;
	}

	for ( i = 0 ; i < 6 ; i++ ) {
		idStr::snPrintf( fullName, sizeof( fullName ), "%s%s", imgName, sides[i] );

		ID_TIME_T thisTime;
		if ( !pics ) {
			// just checking timestamps
			R_LoadImageProgram( fullName, NULL, &width, &height, &thisTime );
		} else {
			R_LoadImageProgram( fullName, &pics[i], &width, &height, &thisTime );
		}
		if ( thisTime == FILE_NOT_FOUND_TIMESTAMP ) {
			break;
		}
		if ( i == 0 ) {
			size = width;
		}
		if ( width != size || height != size ) {
			common->Warning( "Mismatched sizes on cube map '%s'", imgName );
			break;
		}
		if ( timestamp ) {
			if ( thisTime > *timestamp ) {
				*timestamp = thisTime;
			}
		}
		if ( pics && extensions == CF_CAMERA ) {
			// convert from "camera" images to native cube map images
			switch( i ) {
			case 0:	// forward
				R_RotatePic( pics[i], width);
				break;
			case 1:	// back
				R_RotatePic( pics[i], width);
				R_HorizontalFlip( pics[i], width, height );
				R_VerticalFlip( pics[i], width, height );
				break;
			case 2:	// left
				R_VerticalFlip( pics[i], width, height );
				break;
			case 3:	// right
				R_HorizontalFlip( pics[i], width, height );
				break;
			case 4:	// up
				R_RotatePic( pics[i], width);
				break;
			case 5: // down
				R_RotatePic( pics[i], width);
				break;
			}
		}
	}

	if ( i != 6 ) {
		// we had an error, so free everything
		// pics[i] is live on the size-mismatch break, NULL on the not-found break
		if ( pics ) {
			for ( j = 0 ; j <= i && j < 6 ; j++ ) {
				if ( pics[j] ) {
					R_StaticFree( pics[j] );
					pics[j] = NULL;
				}
			}
		}

		if ( timestamp ) {
			*timestamp = 0;
		}
		return false;
	}

	if ( outSize ) {
		*outSize = size;
	}
	return true;
}
