// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __VK_IMAGE_H__
#define __VK_IMAGE_H__

/*
===============================================================================

	Vulkan idImage backing (Phase D). texnum indexes the module-side image
	table; the executor caches descriptors keyed on the generation counter.

===============================================================================
*/

#include "volk.h"

struct VmaAllocation_T;
typedef struct VmaAllocation_T *VmaAllocation;

typedef struct vkImageEntry_s {
	bool			inUse;
	VkImage			image;
	VmaAllocation	allocation;
	VkImageView		view;
	// Depth/stencil images need a depth-only sampled view and a combined
	// depth+stencil attachment view. Color and depth-only images alias this
	// to view.
	VkImageView		attachmentView;
	// Cube sampling uses the full cube view; rendering attaches one 2D face.
	VkImageView		cubeAttachmentViews[ 6 ];
	VkSampler		sampler;
	VkFormat		format;
	VkImageUsageFlags usage;
	VkImageAspectFlags aspectMask;
	VkImageLayout	layout;
	VkSampleCountFlagBits samples;
	int				width;
	int				height;
	int				numMips;
	int				numLayers;		// 6 for cube maps
	bool			isCube;
	bool			everUploaded;	// first upload transitions from UNDEFINED
	// generation counter for executor-side descriptor caching
	unsigned int	generation;
} vkImageEntry_t;

static const int VK_MAX_IMAGES = 4096;
// Match the five draw buffers exposed by idRenderTexture's OpenGL backend.
// Admission also honors the physical device's (possibly lower) limit.
static const int VK_MAX_COLOR_ATTACHMENTS = 5;

struct vkRenderTargetAttachments_t {
	uint32_t colorCount;
	vkImageEntry_t *colors[ VK_MAX_COLOR_ATTACHMENTS ];
	VkImageView colorViews[ VK_MAX_COLOR_ATTACHMENTS ];
	vkImageEntry_t *depth;
	VkImageView depthView;
	VkExtent2D extent;
	VkSampleCountFlagBits samples;
};

vkImageEntry_t *VK_Image_GetEntry( unsigned int texnum );
VkImageView VK_Image_GetAttachmentView( vkImageEntry_t *entry, int cubeFace );
// Does not call EnsureDeviceHandle: the backend uses this to initialize it.
bool VK_Image_GetRenderTargetAttachments( const idRenderTexture *target, int cubeFace,
		vkRenderTargetAttachments_t &attachments );
// Re-backs an idImage with exact-format, single-sample depth storage suitable
// for vkCmdCopyImage feedback captures while keeping the idImage's public
// dimensions in sync with the copied region.
bool	VK_Image_MakeDepthCopyTarget( idImage *image, int width, int height,
			VkFormat depthFormat );
// Single-sample scratch storage in the source's exact format, used to resolve
// a multisampled scene target before the format-converting feedback blit.
vkImageEntry_t *VK_Image_AcquireResolveScratch( int width, int height,
			VkFormat format );
// Single-sample depth attachment in the source's exact format, the resolve
// target for a multisampled depth capture.
vkImageEntry_t *VK_Image_AcquireDepthResolveScratch( int width, int height,
			VkFormat format );
void	VK_Image_ShutdownAll( void );

#endif /* !__VK_IMAGE_H__ */
