// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	Vulkan idImage backend (Phase D,
	docs/dev/plans/2026-07-18-vulkan-phase-d.md).

	Implements the idImage GPU half the GL build keeps in
	OpenGL/gl_Image.cpp. texnum indexes a module-side image table; every
	mip level arrives pre-generated from the CPU side (imagetools), so the
	upload path is staging-buffer copies only. Sampler-side component
	swizzles (fonts' green-alpha, the R8-backed alpha/intensity formats)
	are expressed on the VkImageView.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../RenderModuleAPI.h"

#undef snprintf
#undef vsnprintf
#ifndef INT_MAX
#define INT_MAX		2147483647
#endif
#ifndef INT_MIN
#define INT_MIN		( -2147483647 - 1 )
#endif
#ifndef UINT_MAX
#define UINT_MAX	0xffffffffu
#endif
#include <cstdio>
#include "volk.h"
#include "vk_mem_alloc.h"

#include "VulkanDevice.h"
#include "vk_Image.h"

extern idCVar image_anisotropy;

/*
====================
Image table
====================
*/
static vkImageEntry_t vkImages[ VK_MAX_IMAGES ];
static unsigned int vkImageGenerationCounter = 1;

vkImageEntry_t *VK_Image_GetEntry( unsigned int texnum ) {
	if ( texnum == 0xFFFFFFFFu /* TEXTURE_NOT_LOADED */ || texnum >= VK_MAX_IMAGES || !vkImages[ texnum ].inUse ) {
		return NULL;
	}
	return &vkImages[ texnum ];
}

static int VK_Image_AllocSlot( void ) {
	for ( int i = 0; i < VK_MAX_IMAGES; i++ ) {
		if ( !vkImages[ i ].inUse ) {
			return i;
		}
	}
	common->Warning( "Vulkan: image table exhausted (%d)", VK_MAX_IMAGES );
	return -1;
}

/*
====================
Format mapping

CPU-side packing (imagetools BinaryImage) is authoritative; this table only
selects the matching VkFormat and the view swizzle that reproduces the GL
sampler-side component mappings.
====================
*/
typedef struct vkFormatInfo_s {
	VkFormat			format;
	int					bytesPerBlock;	// compressed block or texel size
	int					blockDim;		// 1 for uncompressed, 4 for BC
	VkComponentMapping	swizzle;
} vkFormatInfo_t;

static const VkComponentMapping VK_SWIZZLE_IDENTITY = {
	VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
	VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };

static bool VK_Image_GetFormatInfo( const idImageOpts &opts, vkFormatInfo_t &info ) {
	info.format = VK_FORMAT_UNDEFINED;
	info.bytesPerBlock = 4;
	info.blockDim = 1;
	info.swizzle = VK_SWIZZLE_IDENTITY;

	switch ( opts.format ) {
		case FMT_RGBA8:
		case FMT_XRGB8:
			info.format = VK_FORMAT_R8G8B8A8_UNORM;
			info.bytesPerBlock = 4;
			break;
		case FMT_DXT1:
			info.format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
			info.bytesPerBlock = 8;
			info.blockDim = 4;
			break;
		case FMT_DXT5:
			info.format = VK_FORMAT_BC3_UNORM_BLOCK;
			info.bytesPerBlock = 16;
			info.blockDim = 4;
			break;
		case FMT_BC7:
			info.format = VK_FORMAT_BC7_UNORM_BLOCK;
			info.bytesPerBlock = 16;
			info.blockDim = 4;
			break;
		case FMT_ALPHA:
			// alpha in the red channel: RGB=1, A=R
			info.format = VK_FORMAT_R8_UNORM;
			info.bytesPerBlock = 1;
			info.swizzle.r = VK_COMPONENT_SWIZZLE_ONE;
			info.swizzle.g = VK_COMPONENT_SWIZZLE_ONE;
			info.swizzle.b = VK_COMPONENT_SWIZZLE_ONE;
			info.swizzle.a = VK_COMPONENT_SWIZZLE_R;
			break;
		case FMT_LUM8:
			// luminance: RGB=R, A=1
			info.format = VK_FORMAT_R8_UNORM;
			info.bytesPerBlock = 1;
			info.swizzle.r = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.g = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.b = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.a = VK_COMPONENT_SWIZZLE_ONE;
			break;
		case FMT_INT8:
			// intensity: RGBA=R
			info.format = VK_FORMAT_R8_UNORM;
			info.bytesPerBlock = 1;
			info.swizzle.r = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.g = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.b = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.a = VK_COMPONENT_SWIZZLE_R;
			break;
		case FMT_L8A8:
			// luminance + alpha: RGB=R, A=G
			info.format = VK_FORMAT_R8G8_UNORM;
			info.bytesPerBlock = 2;
			info.swizzle.r = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.g = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.b = VK_COMPONENT_SWIZZLE_R;
			info.swizzle.a = VK_COMPONENT_SWIZZLE_G;
			break;
		case FMT_RGBA16F:
			info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
			info.bytesPerBlock = 8;
			break;
		case FMT_DEPTH:
			info.format = VK_FORMAT_D32_SFLOAT;
			info.bytesPerBlock = 4;
			break;
		case FMT_DEPTH_STENCIL:
			info.format = VK_FORMAT_D24_UNORM_S8_UINT;
			info.bytesPerBlock = 4;
			break;
		case FMT_RGB565:
			// CPU packs big-endian byte pairs (GL compensates with
			// UNPACK_SWAP_BYTES); gameplay-only format, byte-swap when the
			// upload path first meets it (Phase E lightgrid work)
			info.format = VK_FORMAT_R5G6B5_UNORM_PACK16;
			info.bytesPerBlock = 2;
			break;
		default:
			common->Warning( "Vulkan: unsupported textureFormat_t %d", (int)opts.format );
			return false;
	}

	// colorFormat refinements
	if ( opts.colorFormat == CFM_GREEN_ALPHA ) {
		// RGB=1, A=G (fonts and green-alpha DXT content)
		info.swizzle.r = VK_COMPONENT_SWIZZLE_ONE;
		info.swizzle.g = VK_COMPONENT_SWIZZLE_ONE;
		info.swizzle.b = VK_COMPONENT_SWIZZLE_ONE;
		info.swizzle.a = VK_COMPONENT_SWIZZLE_G;
	}
	return true;
}

/*
====================
Sampler cache
====================
*/
typedef struct vkSamplerKey_s {
	textureFilter_t	filter;
	textureRepeat_t	repeat;
	bool			mips;
	int				anisotropy;
} vkSamplerKey_t;

static const int VK_MAX_SAMPLERS = 64;
static vkSamplerKey_t vkSamplerKeys[ VK_MAX_SAMPLERS ];
static VkSampler vkSamplers[ VK_MAX_SAMPLERS ];
static int vkNumSamplers = 0;

static VkSampler VK_Image_GetSampler( textureFilter_t filter, textureRepeat_t repeat, bool mips ) {
	int anisotropy = 0;
	if ( filter == TF_DEFAULT && mips ) {
		anisotropy = image_anisotropy.GetInteger();
		if ( anisotropy < 0 ) {
			anisotropy = 0;
		}
		if ( anisotropy > (int)vkCtx.deviceProperties.limits.maxSamplerAnisotropy ) {
			anisotropy = (int)vkCtx.deviceProperties.limits.maxSamplerAnisotropy;
		}
	}

	for ( int i = 0; i < vkNumSamplers; i++ ) {
		if ( vkSamplerKeys[ i ].filter == filter && vkSamplerKeys[ i ].repeat == repeat
				&& vkSamplerKeys[ i ].mips == mips && vkSamplerKeys[ i ].anisotropy == anisotropy ) {
			return vkSamplers[ i ];
		}
	}
	if ( vkNumSamplers >= VK_MAX_SAMPLERS ) {
		common->Warning( "Vulkan: sampler cache exhausted" );
		return vkSamplers[ 0 ];
	}

	VkSamplerCreateInfo sci;
	memset( &sci, 0, sizeof( sci ) );
	sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	switch ( filter ) {
		case TF_NEAREST:
			sci.magFilter = VK_FILTER_NEAREST;
			sci.minFilter = VK_FILTER_NEAREST;
			sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			break;
		case TF_LINEAR:
			sci.magFilter = VK_FILTER_LINEAR;
			sci.minFilter = VK_FILTER_LINEAR;
			sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			break;
		default:	// TF_DEFAULT
			sci.magFilter = VK_FILTER_LINEAR;
			sci.minFilter = VK_FILTER_LINEAR;
			sci.mipmapMode = mips ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
			break;
	}
	sci.maxLod = mips ? VK_LOD_CLAMP_NONE : 0.25f;

	VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	switch ( repeat ) {
		case TR_CLAMP:
			addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			break;
		case TR_CLAMP_TO_BORDER:
			addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			break;
		case TR_CLAMP_TO_ZERO:
			addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			break;
		case TR_CLAMP_TO_ZERO_ALPHA:
			addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			sci.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			break;
		default:	// TR_REPEAT / TR_MIRRORED_REPEAT
			addressMode = ( repeat == TR_MIRRORED_REPEAT ) ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : VK_SAMPLER_ADDRESS_MODE_REPEAT;
			break;
	}
	sci.addressModeU = addressMode;
	sci.addressModeV = addressMode;
	sci.addressModeW = addressMode;

	if ( anisotropy > 1 ) {
		sci.anisotropyEnable = VK_TRUE;
		sci.maxAnisotropy = (float)anisotropy;
	}

	VkSampler sampler = VK_NULL_HANDLE;
	if ( vkCreateSampler( vkCtx.device, &sci, NULL, &sampler ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: sampler creation failed" );
		return vkNumSamplers > 0 ? vkSamplers[ 0 ] : VK_NULL_HANDLE;
	}
	vkSamplerKeys[ vkNumSamplers ].filter = filter;
	vkSamplerKeys[ vkNumSamplers ].repeat = repeat;
	vkSamplerKeys[ vkNumSamplers ].mips = mips;
	vkSamplerKeys[ vkNumSamplers ].anisotropy = anisotropy;
	vkSamplers[ vkNumSamplers ] = sampler;
	vkNumSamplers++;
	return sampler;
}

/*
====================
VK_Image_ShutdownAll

Device-shutdown hook: destroys every live image and sampler immediately
(the device is idle by contract when this runs).
====================
*/
void VK_Image_ShutdownAll( void ) {
	for ( int i = 0; i < VK_MAX_IMAGES; i++ ) {
		if ( !vkImages[ i ].inUse ) {
			continue;
		}
		if ( vkImages[ i ].view != VK_NULL_HANDLE ) {
			vkDestroyImageView( vkCtx.device, vkImages[ i ].view, NULL );
		}
		if ( vkImages[ i ].image != VK_NULL_HANDLE && vkCtx.allocator != NULL ) {
			vmaDestroyImage( vkCtx.allocator, vkImages[ i ].image, vkImages[ i ].allocation );
		}
		memset( &vkImages[ i ], 0, sizeof( vkImages[ i ] ) );
	}
	for ( int i = 0; i < vkNumSamplers; i++ ) {
		vkDestroySampler( vkCtx.device, vkSamplers[ i ], NULL );
	}
	vkNumSamplers = 0;
}

/*
====================
idImage::PurgeImage
====================
*/
void idImage::PurgeImage( void ) {
	vkImageEntry_t *entry = VK_Image_GetEntry( texnum );
	if ( entry != NULL ) {
		// the image may still be referenced by an in-flight frame
		VK_Device_DeferDestroy( entry->image, entry->view, VK_NULL_HANDLE, entry->allocation );
		memset( entry, 0, sizeof( *entry ) );
	}
	texnum = TEXTURE_NOT_LOADED;
}

/*
====================
idImage::AllocImage

Creates the VkImage + view + sampler from opts. Before the device exists
(engine boots decls/materials first) the image stays TEXTURE_NOT_LOADED,
exactly like the GL half without a context; the InitOpenGL seam reloads.
====================
*/
void idImage::AllocImage( void ) {
	PurgeImage();
	storageGeneration++;

	if ( !vkCtx.initialized ) {
		return;
	}
	if ( opts.width <= 0 || opts.height <= 0 ) {
		return;
	}

	vkFormatInfo_t info;
	if ( !VK_Image_GetFormatInfo( opts, info ) ) {
		return;
	}

	const bool isCube = opts.textureType == TT_CUBIC;
	const int numMips = opts.numLevels > 0 ? opts.numLevels : 1;

	VkImageCreateInfo ici;
	memset( &ici, 0, sizeof( ici ) );
	ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = info.format;
	ici.extent.width = (uint32_t)opts.width;
	ici.extent.height = (uint32_t)opts.height;
	ici.extent.depth = 1;
	ici.mipLevels = (uint32_t)numMips;
	ici.arrayLayers = isCube ? 6 : 1;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if ( isCube ) {
		ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	}

	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;

	const int slot = VK_Image_AllocSlot();
	if ( slot < 0 ) {
		return;
	}
	vkImageEntry_t &entry = vkImages[ slot ];
	memset( &entry, 0, sizeof( entry ) );

	if ( vmaCreateImage( vkCtx.allocator, &ici, &vaci, &entry.image, &entry.allocation, NULL ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: image creation failed (%dx%d fmt %d)", opts.width, opts.height, (int)opts.format );
		return;
	}

	VkImageViewCreateInfo ivci;
	memset( &ivci, 0, sizeof( ivci ) );
	ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	ivci.image = entry.image;
	ivci.viewType = isCube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
	ivci.format = info.format;
	ivci.components = info.swizzle;
	ivci.subresourceRange.aspectMask = ( opts.format == FMT_DEPTH || opts.format == FMT_DEPTH_STENCIL )
			? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	ivci.subresourceRange.levelCount = (uint32_t)numMips;
	ivci.subresourceRange.layerCount = isCube ? 6 : 1;
	if ( vkCreateImageView( vkCtx.device, &ivci, NULL, &entry.view ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: image view creation failed" );
		vmaDestroyImage( vkCtx.allocator, entry.image, entry.allocation );
		memset( &entry, 0, sizeof( entry ) );
		return;
	}

	entry.inUse = true;
	entry.format = info.format;
	entry.width = opts.width;
	entry.height = opts.height;
	entry.numMips = numMips;
	entry.numLayers = isCube ? 6 : 1;
	entry.isCube = isCube;
	entry.everUploaded = false;
	entry.generation = vkImageGenerationCounter++;
	entry.sampler = VK_Image_GetSampler( filter, repeat, numMips > 1 );

	texnum = (unsigned int)slot;
}

/*
====================
idImage::SubImageUpload

Staging-buffer copy of one mip/face region, immediate-submitted so the data
is resident before the recording frame is submitted.
====================
*/
typedef struct vkUploadContext_s {
	vkImageEntry_t *entry;
	VkBuffer		staging;
	int				mipLevel;
	int				layer;
	int				x, y;
	int				width, height;
	uint32_t		bufferRowLengthTexels;
	bool			firstUseOfImage;
} vkUploadContext_t;

static void VK_Image_RecordUpload( VkCommandBuffer cmd, void *user ) {
	const vkUploadContext_t *ctx = (const vkUploadContext_t *)user;

	VkImageMemoryBarrier2 toTransfer;
	memset( &toTransfer, 0, sizeof( toTransfer ) );
	toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	toTransfer.oldLayout = ctx->firstUseOfImage ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toTransfer.image = ctx->entry->image;
	toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toTransfer.subresourceRange.baseMipLevel = 0;
	toTransfer.subresourceRange.levelCount = (uint32_t)ctx->entry->numMips;
	toTransfer.subresourceRange.baseArrayLayer = 0;
	toTransfer.subresourceRange.layerCount = (uint32_t)ctx->entry->numLayers;

	VkDependencyInfo dep;
	memset( &dep, 0, sizeof( dep ) );
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &toTransfer;
	vkCmdPipelineBarrier2( cmd, &dep );

	VkBufferImageCopy region;
	memset( &region, 0, sizeof( region ) );
	region.bufferRowLength = ctx->bufferRowLengthTexels;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = (uint32_t)ctx->mipLevel;
	region.imageSubresource.baseArrayLayer = (uint32_t)ctx->layer;
	region.imageSubresource.layerCount = 1;
	region.imageOffset.x = ctx->x;
	region.imageOffset.y = ctx->y;
	region.imageExtent.width = (uint32_t)ctx->width;
	region.imageExtent.height = (uint32_t)ctx->height;
	region.imageExtent.depth = 1;
	vkCmdCopyBufferToImage( cmd, ctx->staging, ctx->entry->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

	VkImageMemoryBarrier2 toShader = toTransfer;
	toShader.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	toShader.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	toShader.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
	toShader.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
	toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	dep.pImageMemoryBarriers = &toShader;
	vkCmdPipelineBarrier2( cmd, &dep );
}

void idImage::SubImageUpload( int mipLevel, int x, int y, int z, int width, int height, const void *pic, int pixelPitch ) const {
	vkImageEntry_t *entry = VK_Image_GetEntry( texnum );
	if ( entry == NULL || pic == NULL || width <= 0 || height <= 0 ) {
		return;
	}

	vkFormatInfo_t info;
	if ( !VK_Image_GetFormatInfo( opts, info ) ) {
		return;
	}

	// data size: compressed rows are block rows padded to block alignment
	size_t dataBytes;
	uint32_t rowLengthTexels = 0;
	if ( info.blockDim > 1 ) {
		const int blocksWide = ( width + info.blockDim - 1 ) / info.blockDim;
		const int blocksHigh = ( height + info.blockDim - 1 ) / info.blockDim;
		dataBytes = (size_t)blocksWide * blocksHigh * info.bytesPerBlock;
	} else {
		const int rowTexels = pixelPitch > 0 ? pixelPitch : width;
		rowLengthTexels = pixelPitch > 0 ? (uint32_t)pixelPitch : 0;
		dataBytes = (size_t)rowTexels * height * info.bytesPerBlock;
	}

	// transient staging buffer, freed via the deferred queue
	VkBufferCreateInfo bci;
	memset( &bci, 0, sizeof( bci ) );
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = dataBytes;
	bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;
	vaci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VkBuffer staging = VK_NULL_HANDLE;
	VmaAllocation stagingAlloc = NULL;
	VmaAllocationInfo stagingInfo;
	if ( vmaCreateBuffer( vkCtx.allocator, &bci, &vaci, &staging, &stagingAlloc, &stagingInfo ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: staging buffer creation failed (%d bytes)", (int)dataBytes );
		return;
	}
	memcpy( stagingInfo.pMappedData, pic, dataBytes );

	vkUploadContext_t ctx;
	ctx.entry = entry;
	ctx.staging = staging;
	ctx.mipLevel = mipLevel;
	ctx.layer = entry->isCube ? z : 0;
	ctx.x = x;
	ctx.y = y;
	ctx.width = width;
	ctx.height = height;
	ctx.bufferRowLengthTexels = rowLengthTexels;
	ctx.firstUseOfImage = !entry->everUploaded;

	if ( VK_Device_ImmediateSubmit( VK_Image_RecordUpload, &ctx ) ) {
		entry->everUploaded = true;
	}

	VK_Device_DeferDestroy( VK_NULL_HANDLE, VK_NULL_HANDLE, staging, stagingAlloc );
}

/*
====================
idImage::SetTexParameters

Filter/repeat changes re-resolve the sampler; the generation bump lets the
executor's descriptor cache re-bind.
====================
*/
void idImage::SetTexParameters( void ) {
	vkImageEntry_t *entry = VK_Image_GetEntry( texnum );
	if ( entry == NULL ) {
		return;
	}
	entry->sampler = VK_Image_GetSampler( filter, repeat, entry->numMips > 1 );
	entry->generation = vkImageGenerationCounter++;
}

/*
====================
idImage::Resize
====================
*/
void idImage::Resize( int width, int height ) {
	if ( opts.width == width && opts.height == height ) {
		return;
	}
	opts.width = width;
	opts.height = height;
	AllocImage();
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
