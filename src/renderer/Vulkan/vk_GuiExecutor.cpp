// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	Vulkan 2D/GUI executor (Phase D,
	docs/dev/plans/2026-07-18-vulkan-phase-d.md).

	Draws the engine's 2D views (main menu, console, HUD overlays, ROQ
	cinematics) on the swapchain. The contract mirrors the GL reference
	(RB_STD_DrawShaderPasses): 2D views arrive as RC_DRAW_VIEW commands
	whose viewDef->viewEntitys is NULL, with a front-end-built ortho
	projection, pre-evaluated shader registers, paint-order drawSurfs, and
	CPU-resident geometry (the vertex cache runs CPU-backed under Vulkan).

	Frame shape: the first clear/draw of a frame opens the swapchain
	rendering; RC_SWAP_BUFFERS closes and presents. Geometry streams
	through per-frame-in-flight host-visible rings; images bind through a
	per-image descriptor cache keyed on the image generation.

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
#include "shaders/gui_shaders_spv.h"

extern idCVar r_skipDynamicTextures;

// no module-owned vertex-cache GPU state exists: the engine cache runs
// CPU-backed under Vulkan and the executor streams into its own rings
void VK_VertexCache_Shutdown( void ) {
}

/*
====================
Executor state
====================
*/
static const int VK_VERTEX_RING_BYTES = 8 * 1024 * 1024;
static const int VK_INDEX_RING_BYTES = 2 * 1024 * 1024;
static const int VK_MAX_GUI_PIPELINES = 64;
static const int VK_MAX_DESCRIPTOR_SETS = 4096;

typedef struct vkRing_s {
	VkBuffer		buffer;
	VmaAllocation	allocation;
	unsigned char *	mapped;
	int				capacity;
	int				cursor;
} vkRing_t;

typedef struct vkGuiPipeline_s {
	int				blendBits;		// GLS src|dst blend bits
	VkPipeline		pipeline;
} vkGuiPipeline_t;

typedef struct vkGuiPushConstants_s {
	float			mvp[ 16 ];
	float			stageColor[ 4 ];
	float			texMatrixS[ 4 ];
	float			texMatrixT[ 4 ];
	float			params[ 4 ];	// x: vertexColorMode, y: alphaTest, z: alphaTestRef, w: texMatrix enable
} vkGuiPushConstants_t;

typedef struct vkDescriptorCacheEntry_s {
	VkDescriptorSet	set;
	unsigned int	generation;
} vkDescriptorCacheEntry_t;

typedef struct vkGuiExecutor_s {
	bool				initialized;

	VkShaderModule		vertModule;
	VkShaderModule		fragModule;
	VkDescriptorSetLayout setLayout;
	VkDescriptorPool	descriptorPool;
	VkPipelineLayout	pipelineLayout;
	vkGuiPipeline_t		pipelines[ VK_MAX_GUI_PIPELINES ];
	int					numPipelines;
	VkFormat			pipelineTargetFormat;	// swapchain format the pipelines were built for

	vkRing_t			vertexRings[ VK_FRAMES_IN_FLIGHT ];
	vkRing_t			indexRings[ VK_FRAMES_IN_FLIGHT ];

	vkDescriptorCacheEntry_t descriptorCache[ 4096 ];	// parallel to the image table

	// frame-in-progress state
	bool				frameOpen;
	int					frameSlot;
	uint32_t			swapImageIndex;
	VkCommandBuffer		cmd;
	float				clearColor[ 4 ];
} vkGuiExecutor_t;

static vkGuiExecutor_t vkExec;

/*
====================
Rings
====================
*/
static bool VK_Ring_Create( vkRing_t &ring, int capacity, VkBufferUsageFlags usage ) {
	VkBufferCreateInfo bci;
	memset( &bci, 0, sizeof( bci ) );
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = (VkDeviceSize)capacity;
	bci.usage = usage;

	VmaAllocationCreateInfo vaci;
	memset( &vaci, 0, sizeof( vaci ) );
	vaci.usage = VMA_MEMORY_USAGE_AUTO;
	vaci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo info;
	if ( vmaCreateBuffer( vkCtx.allocator, &bci, &vaci, &ring.buffer, &ring.allocation, &info ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: ring buffer creation failed (%d bytes)", capacity );
		return false;
	}
	ring.mapped = (unsigned char *)info.pMappedData;
	ring.capacity = capacity;
	ring.cursor = 0;
	return true;
}

static int VK_Ring_Alloc( vkRing_t &ring, const void *data, int bytes, int alignment ) {
	int offset = ( ring.cursor + alignment - 1 ) & ~( alignment - 1 );
	if ( offset + bytes > ring.capacity ) {
		common->Warning( "Vulkan: frame geometry ring overflow (%d + %d > %d)", offset, bytes, ring.capacity );
		return -1;
	}
	memcpy( ring.mapped + offset, data, bytes );
	ring.cursor = offset + bytes;
	return offset;
}

/*
====================
Pipelines
====================
*/
static VkBlendFactor VK_BlendFactorFromGLSSrc( int bits ) {
	switch ( bits & GLS_SRCBLEND_BITS ) {
		case GLS_SRCBLEND_ZERO:					return VK_BLEND_FACTOR_ZERO;
		case GLS_SRCBLEND_DST_COLOR:			return VK_BLEND_FACTOR_DST_COLOR;
		case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:	return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		case GLS_SRCBLEND_SRC_ALPHA:			return VK_BLEND_FACTOR_SRC_ALPHA;
		case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case GLS_SRCBLEND_DST_ALPHA:			return VK_BLEND_FACTOR_DST_ALPHA;
		case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		case GLS_SRCBLEND_ALPHA_SATURATE:		return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
		case GLS_SRCBLEND_SRC_COLOR:			return VK_BLEND_FACTOR_SRC_COLOR;
		case GLS_SRCBLEND_ONE_MINUS_SRC_COLOR:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		default:								return VK_BLEND_FACTOR_ONE;
	}
}

static VkBlendFactor VK_BlendFactorFromGLSDst( int bits ) {
	switch ( bits & GLS_DSTBLEND_BITS ) {
		case GLS_DSTBLEND_ONE:					return VK_BLEND_FACTOR_ONE;
		case GLS_DSTBLEND_SRC_COLOR:			return VK_BLEND_FACTOR_SRC_COLOR;
		case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case GLS_DSTBLEND_SRC_ALPHA:			return VK_BLEND_FACTOR_SRC_ALPHA;
		case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case GLS_DSTBLEND_DST_ALPHA:			return VK_BLEND_FACTOR_DST_ALPHA;
		case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:	return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		default:								return VK_BLEND_FACTOR_ZERO;
	}
}

static VkPipeline VK_GuiExecutor_GetPipeline( int stateBits ) {
	const int blendBits = stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );

	for ( int i = 0; i < vkExec.numPipelines; i++ ) {
		if ( vkExec.pipelines[ i ].blendBits == blendBits ) {
			return vkExec.pipelines[ i ].pipeline;
		}
	}
	if ( vkExec.numPipelines >= VK_MAX_GUI_PIPELINES ) {
		common->Warning( "Vulkan: GUI pipeline cache exhausted" );
		return vkExec.pipelines[ 0 ].pipeline;
	}

	VkPipelineShaderStageCreateInfo stages[ 2 ];
	memset( stages, 0, sizeof( stages ) );
	stages[ 0 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[ 0 ].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[ 0 ].module = vkExec.vertModule;
	stages[ 0 ].pName = "main";
	stages[ 1 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[ 1 ].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[ 1 ].module = vkExec.fragModule;
	stages[ 1 ].pName = "main";

	// idDrawVert: xyz@0, color ubyte4@12, st@56 (64-byte stride)
	VkVertexInputBindingDescription binding;
	memset( &binding, 0, sizeof( binding ) );
	binding.stride = sizeof( idDrawVert );
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attrs[ 3 ];
	memset( attrs, 0, sizeof( attrs ) );
	attrs[ 0 ].location = 0;
	attrs[ 0 ].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[ 0 ].offset = 0;
	attrs[ 1 ].location = 1;
	attrs[ 1 ].format = VK_FORMAT_R8G8B8A8_UNORM;
	attrs[ 1 ].offset = 12;
	attrs[ 2 ].location = 2;
	attrs[ 2 ].format = VK_FORMAT_R32G32_SFLOAT;
	attrs[ 2 ].offset = 56;

	VkPipelineVertexInputStateCreateInfo vertexInput;
	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &binding;
	vertexInput.vertexAttributeDescriptionCount = 3;
	vertexInput.pVertexAttributeDescriptions = attrs;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly;
	memset( &inputAssembly, 0, sizeof( inputAssembly ) );
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewportState;
	memset( &viewportState, 0, sizeof( viewportState ) );
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo raster;
	memset( &raster, 0, sizeof( raster ) );
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;	// 2D
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample;
	memset( &multisample, 0, sizeof( multisample ) );
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blendAttachment;
	memset( &blendAttachment, 0, sizeof( blendAttachment ) );
	blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
			| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	const VkBlendFactor srcFactor = VK_BlendFactorFromGLSSrc( blendBits );
	const VkBlendFactor dstFactor = VK_BlendFactorFromGLSDst( blendBits );
	if ( !( srcFactor == VK_BLEND_FACTOR_ONE && dstFactor == VK_BLEND_FACTOR_ZERO ) ) {
		blendAttachment.blendEnable = VK_TRUE;
		blendAttachment.srcColorBlendFactor = srcFactor;
		blendAttachment.dstColorBlendFactor = dstFactor;
		blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachment.srcAlphaBlendFactor = srcFactor;
		blendAttachment.dstAlphaBlendFactor = dstFactor;
		blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	}

	VkPipelineColorBlendStateCreateInfo blendState;
	memset( &blendState, 0, sizeof( blendState ) );
	blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendState.attachmentCount = 1;
	blendState.pAttachments = &blendAttachment;

	VkDynamicState dynamicStates[ 2 ] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState;
	memset( &dynamicState, 0, sizeof( dynamicState ) );
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineRenderingCreateInfo rendering;
	memset( &rendering, 0, sizeof( rendering ) );
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachmentFormats = &vkCtx.swapchainFormat;

	VkGraphicsPipelineCreateInfo gpci;
	memset( &gpci, 0, sizeof( gpci ) );
	gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.pNext = &rendering;
	gpci.stageCount = 2;
	gpci.pStages = stages;
	gpci.pVertexInputState = &vertexInput;
	gpci.pInputAssemblyState = &inputAssembly;
	gpci.pViewportState = &viewportState;
	gpci.pRasterizationState = &raster;
	gpci.pMultisampleState = &multisample;
	gpci.pColorBlendState = &blendState;
	gpci.pDynamicState = &dynamicState;
	gpci.layout = vkExec.pipelineLayout;

	VkPipeline pipeline = VK_NULL_HANDLE;
	if ( vkCreateGraphicsPipelines( vkCtx.device, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: GUI pipeline creation failed (blend 0x%x)", blendBits );
		return vkExec.numPipelines > 0 ? vkExec.pipelines[ 0 ].pipeline : VK_NULL_HANDLE;
	}
	vkExec.pipelines[ vkExec.numPipelines ].blendBits = blendBits;
	vkExec.pipelines[ vkExec.numPipelines ].pipeline = pipeline;
	vkExec.numPipelines++;
	return pipeline;
}

/*
====================
Descriptors
====================
*/
static VkDescriptorSet VK_GuiExecutor_GetImageDescriptor( unsigned int texnum ) {
	vkImageEntry_t *entry = VK_Image_GetEntry( texnum );
	if ( entry == NULL || entry->view == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}

	vkDescriptorCacheEntry_t &cached = vkExec.descriptorCache[ texnum ];
	if ( cached.set != VK_NULL_HANDLE && cached.generation == entry->generation ) {
		return cached.set;
	}

	if ( cached.set == VK_NULL_HANDLE ) {
		VkDescriptorSetAllocateInfo dsai;
		memset( &dsai, 0, sizeof( dsai ) );
		dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		dsai.descriptorPool = vkExec.descriptorPool;
		dsai.descriptorSetCount = 1;
		dsai.pSetLayouts = &vkExec.setLayout;
		if ( vkAllocateDescriptorSets( vkCtx.device, &dsai, &cached.set ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: descriptor set allocation failed" );
			cached.set = VK_NULL_HANDLE;
			return VK_NULL_HANDLE;
		}
	}

	VkDescriptorImageInfo imageInfo;
	memset( &imageInfo, 0, sizeof( imageInfo ) );
	imageInfo.sampler = entry->sampler;
	imageInfo.imageView = entry->view;
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet write;
	memset( &write, 0, sizeof( write ) );
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = cached.set;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &imageInfo;
	vkUpdateDescriptorSets( vkCtx.device, 1, &write, 0, NULL );

	cached.generation = entry->generation;
	return cached.set;
}

/*
====================
Init / Shutdown
====================
*/
static bool VK_GuiExecutor_Init( void ) {
	if ( vkExec.initialized ) {
		return true;
	}
	memset( &vkExec, 0, sizeof( vkExec ) );

	VkShaderModuleCreateInfo smci;
	memset( &smci, 0, sizeof( smci ) );
	smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	smci.codeSize = vk_gui_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_gui_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.vertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: GUI vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_gui_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_gui_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.fragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: GUI fragment shader module creation failed" );
		return false;
	}

	VkDescriptorSetLayoutBinding bindingInfo;
	memset( &bindingInfo, 0, sizeof( bindingInfo ) );
	bindingInfo.binding = 0;
	bindingInfo.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindingInfo.descriptorCount = 1;
	bindingInfo.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo dslci;
	memset( &dslci, 0, sizeof( dslci ) );
	dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dslci.bindingCount = 1;
	dslci.pBindings = &bindingInfo;
	if ( vkCreateDescriptorSetLayout( vkCtx.device, &dslci, NULL, &vkExec.setLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: descriptor set layout creation failed" );
		return false;
	}

	VkDescriptorPoolSize poolSize;
	poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSize.descriptorCount = VK_MAX_DESCRIPTOR_SETS;
	VkDescriptorPoolCreateInfo dpci;
	memset( &dpci, 0, sizeof( dpci ) );
	dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	dpci.maxSets = VK_MAX_DESCRIPTOR_SETS;
	dpci.poolSizeCount = 1;
	dpci.pPoolSizes = &poolSize;
	if ( vkCreateDescriptorPool( vkCtx.device, &dpci, NULL, &vkExec.descriptorPool ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: descriptor pool creation failed" );
		return false;
	}

	VkPushConstantRange pushRange;
	pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pushRange.offset = 0;
	pushRange.size = sizeof( vkGuiPushConstants_t );

	VkPipelineLayoutCreateInfo plci;
	memset( &plci, 0, sizeof( plci ) );
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &vkExec.setLayout;
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pushRange;
	if ( vkCreatePipelineLayout( vkCtx.device, &plci, NULL, &vkExec.pipelineLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: pipeline layout creation failed" );
		return false;
	}

	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		if ( !VK_Ring_Create( vkExec.vertexRings[ i ], VK_VERTEX_RING_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT )
				|| !VK_Ring_Create( vkExec.indexRings[ i ], VK_INDEX_RING_BYTES, VK_BUFFER_USAGE_INDEX_BUFFER_BIT ) ) {
			return false;
		}
	}

	vkExec.pipelineTargetFormat = vkCtx.swapchainFormat;
	vkExec.clearColor[ 0 ] = 0.0f;
	vkExec.clearColor[ 1 ] = 0.0f;
	vkExec.clearColor[ 2 ] = 0.0f;
	vkExec.clearColor[ 3 ] = 1.0f;
	vkExec.initialized = true;
	common->Printf( "Vulkan: GUI executor initialized\n" );
	return true;
}

void VK_GuiExecutor_Shutdown( void ) {
	if ( vkCtx.device == VK_NULL_HANDLE ) {
		memset( &vkExec, 0, sizeof( vkExec ) );
		return;
	}
	for ( int i = 0; i < vkExec.numPipelines; i++ ) {
		if ( vkExec.pipelines[ i ].pipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device, vkExec.pipelines[ i ].pipeline, NULL );
		}
	}
	if ( vkExec.pipelineLayout != VK_NULL_HANDLE ) {
		vkDestroyPipelineLayout( vkCtx.device, vkExec.pipelineLayout, NULL );
	}
	if ( vkExec.descriptorPool != VK_NULL_HANDLE ) {
		vkDestroyDescriptorPool( vkCtx.device, vkExec.descriptorPool, NULL );
	}
	if ( vkExec.setLayout != VK_NULL_HANDLE ) {
		vkDestroyDescriptorSetLayout( vkCtx.device, vkExec.setLayout, NULL );
	}
	if ( vkExec.vertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.vertModule, NULL );
	}
	if ( vkExec.fragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.fragModule, NULL );
	}
	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		if ( vkExec.vertexRings[ i ].buffer != VK_NULL_HANDLE ) {
			vmaDestroyBuffer( vkCtx.allocator, vkExec.vertexRings[ i ].buffer, vkExec.vertexRings[ i ].allocation );
		}
		if ( vkExec.indexRings[ i ].buffer != VK_NULL_HANDLE ) {
			vmaDestroyBuffer( vkCtx.allocator, vkExec.indexRings[ i ].buffer, vkExec.indexRings[ i ].allocation );
		}
	}
	memset( &vkExec, 0, sizeof( vkExec ) );
}

/*
====================
Frame lifecycle
====================
*/
void VK_GuiExecutor_SetClearColor( const float color[ 4 ] ) {
	vkExec.clearColor[ 0 ] = color[ 0 ];
	vkExec.clearColor[ 1 ] = color[ 1 ];
	vkExec.clearColor[ 2 ] = color[ 2 ];
	vkExec.clearColor[ 3 ] = color[ 3 ];
}

static bool VK_GuiExecutor_BeginFrame( void ) {
	static bool loggedNotInitialized = false;
	static bool loggedInitFailed = false;
	if ( vkExec.frameOpen ) {
		return true;
	}
	if ( !vkCtx.initialized ) {
		if ( !loggedNotInitialized ) {
			loggedNotInitialized = true;
			common->Printf( "Vulkan: GUI executor BeginFrame before device init\n" );
		}
		return false;
	}
	if ( !VK_GuiExecutor_Init() ) {
		if ( !loggedInitFailed ) {
			loggedInitFailed = true;
			common->Printf( "Vulkan: GUI executor init failed; frames skipped\n" );
		}
		return false;
	}
	// swapchain format changes (rare) invalidate the pipeline set
	if ( vkExec.pipelineTargetFormat != vkCtx.swapchainFormat ) {
		vkDeviceWaitIdle( vkCtx.device );
		for ( int i = 0; i < vkExec.numPipelines; i++ ) {
			vkDestroyPipeline( vkCtx.device, vkExec.pipelines[ i ].pipeline, NULL );
		}
		vkExec.numPipelines = 0;
		vkExec.pipelineTargetFormat = vkCtx.swapchainFormat;
	}

	const int slot = vkCtx.frameSlot;
	vkCtx.frameSlot = ( vkCtx.frameSlot + 1 ) % VK_FRAMES_IN_FLIGHT;

	vkWaitForFences( vkCtx.device, 1, &vkCtx.frameFences[ slot ], VK_TRUE, UINT64_MAX );
	VK_Device_FlushDeferredDestroys( slot );

	uint32_t imageIndex = 0;
	VkResult res = vkAcquireNextImageKHR( vkCtx.device, vkCtx.swapchain, UINT64_MAX,
			vkCtx.acquireSemaphores[ slot ], VK_NULL_HANDLE, &imageIndex );
	if ( res == VK_ERROR_OUT_OF_DATE_KHR ) {
		if ( !VK_Device_RecreateSwapchain() ) {
			return false;
		}
		res = vkAcquireNextImageKHR( vkCtx.device, vkCtx.swapchain, UINT64_MAX,
				vkCtx.acquireSemaphores[ slot ], VK_NULL_HANDLE, &imageIndex );
	}
	if ( res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR ) {
		return false;
	}

	vkResetFences( vkCtx.device, 1, &vkCtx.frameFences[ slot ] );

	VkCommandBuffer cmd = vkCtx.commandBuffers[ slot ];
	vkResetCommandBuffer( cmd, 0 );
	VkCommandBufferBeginInfo cbbi;
	memset( &cbbi, 0, sizeof( cbbi ) );
	cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer( cmd, &cbbi );

	VkImageMemoryBarrier2 toColor;
	memset( &toColor, 0, sizeof( toColor ) );
	toColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toColor.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
	toColor.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	toColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	toColor.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	toColor.image = vkCtx.swapchainImages[ imageIndex ];
	toColor.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toColor.subresourceRange.levelCount = 1;
	toColor.subresourceRange.layerCount = 1;
	VkDependencyInfo dep;
	memset( &dep, 0, sizeof( dep ) );
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &toColor;
	vkCmdPipelineBarrier2( cmd, &dep );

	VkRenderingAttachmentInfo color;
	memset( &color, 0, sizeof( color ) );
	color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color.imageView = vkCtx.swapchainViews[ imageIndex ];
	color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color.clearValue.color.float32[ 0 ] = vkExec.clearColor[ 0 ];
	color.clearValue.color.float32[ 1 ] = vkExec.clearColor[ 1 ];
	color.clearValue.color.float32[ 2 ] = vkExec.clearColor[ 2 ];
	color.clearValue.color.float32[ 3 ] = vkExec.clearColor[ 3 ];

	VkRenderingInfo ri;
	memset( &ri, 0, sizeof( ri ) );
	ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	ri.renderArea.extent = vkCtx.swapchainExtent;
	ri.layerCount = 1;
	ri.colorAttachmentCount = 1;
	ri.pColorAttachments = &color;
	vkCmdBeginRendering( cmd, &ri );

	vkExec.frameOpen = true;
	vkExec.frameSlot = slot;
	vkExec.swapImageIndex = imageIndex;
	vkExec.cmd = cmd;
	vkExec.vertexRings[ slot ].cursor = 0;
	vkExec.indexRings[ slot ].cursor = 0;
	return true;
}

bool VK_GuiExecutor_EndFrameAndPresent( void ) {
	if ( !vkExec.frameOpen ) {
		return false;
	}
	const int slot = vkExec.frameSlot;
	const uint32_t imageIndex = vkExec.swapImageIndex;
	VkCommandBuffer cmd = vkExec.cmd;

	vkCmdEndRendering( cmd );

	VkImageMemoryBarrier2 toPresent;
	memset( &toPresent, 0, sizeof( toPresent ) );
	toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	toPresent.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
	toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	toPresent.image = vkCtx.swapchainImages[ imageIndex ];
	toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toPresent.subresourceRange.levelCount = 1;
	toPresent.subresourceRange.layerCount = 1;
	VkDependencyInfo dep;
	memset( &dep, 0, sizeof( dep ) );
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &toPresent;
	vkCmdPipelineBarrier2( cmd, &dep );

	vkEndCommandBuffer( cmd );

	VkSemaphoreSubmitInfo waitInfo;
	memset( &waitInfo, 0, sizeof( waitInfo ) );
	waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	waitInfo.semaphore = vkCtx.acquireSemaphores[ slot ];
	waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSemaphoreSubmitInfo signalInfo;
	memset( &signalInfo, 0, sizeof( signalInfo ) );
	signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signalInfo.semaphore = vkCtx.renderFinishedSemaphores[ imageIndex ];
	signalInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkCommandBufferSubmitInfo cmdInfo;
	memset( &cmdInfo, 0, sizeof( cmdInfo ) );
	cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmdInfo.commandBuffer = cmd;
	VkSubmitInfo2 si;
	memset( &si, 0, sizeof( si ) );
	si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	si.waitSemaphoreInfoCount = 1;
	si.pWaitSemaphoreInfos = &waitInfo;
	si.commandBufferInfoCount = 1;
	si.pCommandBufferInfos = &cmdInfo;
	si.signalSemaphoreInfoCount = 1;
	si.pSignalSemaphoreInfos = &signalInfo;
	vkQueueSubmit2( vkCtx.graphicsQueue, 1, &si, vkCtx.frameFences[ slot ] );

	VkPresentInfoKHR pi;
	memset( &pi, 0, sizeof( pi ) );
	pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	pi.waitSemaphoreCount = 1;
	pi.pWaitSemaphores = &vkCtx.renderFinishedSemaphores[ imageIndex ];
	pi.swapchainCount = 1;
	pi.pSwapchains = &vkCtx.swapchain;
	pi.pImageIndices = &imageIndex;
	const VkResult res = vkQueuePresentKHR( vkCtx.graphicsQueue, &pi );
	vkExec.frameOpen = false;
	if ( res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR ) {
		VK_Device_RecreateSwapchain();
	}
	return true;
}

/*
====================
VK_GuiExecutor_Draw2DView

The RB_STD_DrawShaderPasses contract for 2D views, on the swapchain.
====================
*/
void VK_GuiExecutor_Draw2DView( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->numDrawSurfs <= 0 ) {
		return;
	}
	if ( !VK_GuiExecutor_BeginFrame() ) {
		return;
	}

	backEnd.viewDef = (viewDef_t *)viewDef;

	VkCommandBuffer cmd = vkExec.cmd;
	const int slot = vkExec.frameSlot;
	const int fbHeight = (int)vkCtx.swapchainExtent.height;

	// GL bottom-left viewport -> Vulkan negative-height viewport
	const int vpX = viewDef->viewport.x1;
	const int vpYGL = viewDef->viewport.y1;
	const int vpW = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int vpH = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;

	VkViewport viewport;
	viewport.x = (float)vpX;
	viewport.y = (float)( fbHeight - vpYGL );
	viewport.width = (float)vpW;
	viewport.height = -(float)vpH;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport( cmd, 0, 1, &viewport );

	for ( int surfNum = 0; surfNum < viewDef->numDrawSurfs; surfNum++ ) {
		const drawSurf_t *drawSurf = viewDef->drawSurfs[ surfNum ];
		const idMaterial *shader = drawSurf->material;
		if ( shader == NULL || !shader->HasAmbient() || shader->IsPortalSky() ) {
			continue;
		}
		const srfTriangles_t *tri = drawSurf->geo;
		if ( tri == NULL || tri->numIndexes <= 0 || tri->indexes == NULL ) {
			continue;
		}
		const float *regs = drawSurf->shaderRegisters;

		// geometry into the frame rings (CPU-backed vertex cache)
		const idDrawVert *verts = (const idDrawVert *)vertexCache.Position( tri->ambientCache );
		if ( verts == NULL ) {
			continue;
		}
		const int vertexOffset = VK_Ring_Alloc( vkExec.vertexRings[ slot ], verts, tri->numVerts * (int)sizeof( idDrawVert ), 64 );
		const int indexOffset = VK_Ring_Alloc( vkExec.indexRings[ slot ], tri->indexes, tri->numIndexes * (int)sizeof( glIndex_t ), 4 );
		if ( vertexOffset < 0 || indexOffset < 0 ) {
			continue;
		}

		VkDeviceSize vertexBindOffset = (VkDeviceSize)vertexOffset;
		vkCmdBindVertexBuffers( cmd, 0, 1, &vkExec.vertexRings[ slot ].buffer, &vertexBindOffset );
		vkCmdBindIndexBuffer( cmd, vkExec.indexRings[ slot ].buffer, (VkDeviceSize)indexOffset, VK_INDEX_TYPE_UINT32 );

		// per-surface scissor: viewport base + drawSurf scissor (GL bottom-left)
		VkRect2D scissor;
		if ( !drawSurf->scissorRect.IsEmpty() ) {
			const int scX = viewDef->viewport.x1 + drawSurf->scissorRect.x1;
			const int scYGL = viewDef->viewport.y1 + drawSurf->scissorRect.y1;
			const int scW = drawSurf->scissorRect.x2 - drawSurf->scissorRect.x1 + 1;
			const int scH = drawSurf->scissorRect.y2 - drawSurf->scissorRect.y1 + 1;
			scissor.offset.x = scX > 0 ? scX : 0;
			scissor.offset.y = fbHeight - scYGL - scH;
			if ( scissor.offset.y < 0 ) {
				scissor.offset.y = 0;
			}
			scissor.extent.width = (uint32_t)( scW > 0 ? scW : 0 );
			scissor.extent.height = (uint32_t)( scH > 0 ? scH : 0 );
		} else {
			scissor.offset.x = vpX;
			scissor.offset.y = fbHeight - vpYGL - vpH;
			scissor.extent.width = (uint32_t)vpW;
			scissor.extent.height = (uint32_t)vpH;
		}
		vkCmdSetScissor( cmd, 0, 1, &scissor );

		for ( int stageNum = 0; stageNum < shader->GetNumStages(); stageNum++ ) {
			const shaderStage_t *pStage = shader->GetStage( stageNum );

			if ( regs != NULL && regs[ pStage->conditionRegister ] == 0 ) {
				continue;
			}
			if ( pStage->lighting != SL_AMBIENT ) {
				continue;
			}
			if ( ( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE ) ) {
				continue;	// alpha-mask stage
			}
			if ( pStage->newStage != NULL ) {
				continue;	// program stages: Phase E territory
			}

			float color[ 4 ];
			if ( regs != NULL ) {
				color[ 0 ] = regs[ pStage->color.registers[ 0 ] ];
				color[ 1 ] = regs[ pStage->color.registers[ 1 ] ];
				color[ 2 ] = regs[ pStage->color.registers[ 2 ] ];
				color[ 3 ] = regs[ pStage->color.registers[ 3 ] ];
			} else {
				color[ 0 ] = color[ 1 ] = color[ 2 ] = color[ 3 ] = 1.0f;
			}

			// skip stages that can't change the framebuffer
			const int blendBits = pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
			if ( color[ 0 ] <= 0 && color[ 1 ] <= 0 && color[ 2 ] <= 0
					&& blendBits == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE ) ) {
				continue;
			}
			if ( color[ 3 ] <= 0 && blendBits == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA ) ) {
				continue;
			}

			// texture: cinematic or static image (RB_BindVariableStageImage contract)
			idImage *stageImage = NULL;
			if ( pStage->texture.cinematic != NULL ) {
				if ( r_skipDynamicTextures.GetBool() ) {
					stageImage = globalImages->defaultImage;
				} else {
					cinData_t cin = pStage->texture.cinematic->ImageForTime(
							(int)( 1000 * ( viewDef->floatTime + viewDef->renderView.shaderParms[ 11 ] ) ) );
					if ( cin.image != NULL ) {
						globalImages->cinematicImage->UploadScratch( cin.image, cin.imageWidth, cin.imageHeight );
						stageImage = globalImages->cinematicImage;
					} else {
						stageImage = globalImages->blackImage;
					}
				}
			} else {
				stageImage = pStage->texture.image;
			}
			if ( stageImage == NULL ) {
				continue;
			}
			VkDescriptorSet descriptor = VK_GuiExecutor_GetImageDescriptor( stageImage->GetDeviceHandle() );
			if ( descriptor == VK_NULL_HANDLE ) {
				continue;
			}

			vkGuiPushConstants_t push;
			memcpy( push.mvp, viewDef->projectionMatrix, sizeof( push.mvp ) );
			push.stageColor[ 0 ] = color[ 0 ];
			push.stageColor[ 1 ] = color[ 1 ];
			push.stageColor[ 2 ] = color[ 2 ];
			push.stageColor[ 3 ] = color[ 3 ];

			// texture matrix (2x3 from the stage registers)
			if ( pStage->texture.hasMatrix && regs != NULL ) {
				push.texMatrixS[ 0 ] = regs[ pStage->texture.matrix[ 0 ][ 0 ] ];
				push.texMatrixS[ 1 ] = regs[ pStage->texture.matrix[ 0 ][ 1 ] ];
				push.texMatrixS[ 2 ] = 0.0f;
				push.texMatrixS[ 3 ] = regs[ pStage->texture.matrix[ 0 ][ 2 ] ];
				push.texMatrixT[ 0 ] = regs[ pStage->texture.matrix[ 1 ][ 0 ] ];
				push.texMatrixT[ 1 ] = regs[ pStage->texture.matrix[ 1 ][ 1 ] ];
				push.texMatrixT[ 2 ] = 0.0f;
				push.texMatrixT[ 3 ] = regs[ pStage->texture.matrix[ 1 ][ 2 ] ];
				push.params[ 3 ] = 1.0f;
			} else {
				push.texMatrixS[ 0 ] = 1.0f; push.texMatrixS[ 1 ] = 0.0f; push.texMatrixS[ 2 ] = 0.0f; push.texMatrixS[ 3 ] = 0.0f;
				push.texMatrixT[ 0 ] = 0.0f; push.texMatrixT[ 1 ] = 1.0f; push.texMatrixT[ 2 ] = 0.0f; push.texMatrixT[ 3 ] = 0.0f;
				push.params[ 3 ] = 0.0f;
			}

			switch ( pStage->vertexColor ) {
				case SVC_MODULATE:			push.params[ 0 ] = 1.0f; break;
				case SVC_INVERSE_MODULATE:	push.params[ 0 ] = 2.0f; break;
				default:					push.params[ 0 ] = 0.0f; break;
			}

			// alpha test from the GLS bits
			switch ( pStage->drawStateBits & GLS_ATEST_BITS ) {
				case GLS_ATEST_GE_128:
					push.params[ 1 ] = 1.0f;
					push.params[ 2 ] = 0.5f - ( 1.0f / 255.0f );
					break;
				case GLS_ATEST_LT_128:
					// inverted compare is rare in 2D; approximate as none
					push.params[ 1 ] = 0.0f;
					push.params[ 2 ] = 0.0f;
					break;
				case GLS_ATEST_EQ_255:
					push.params[ 1 ] = 1.0f;
					push.params[ 2 ] = 1.0f - ( 1.0f / 255.0f );
					break;
				default:
					push.params[ 1 ] = 0.0f;
					push.params[ 2 ] = 0.0f;
					break;
			}

			VkPipeline pipeline = VK_GuiExecutor_GetPipeline( pStage->drawStateBits );
			if ( pipeline == VK_NULL_HANDLE ) {
				continue;
			}
			vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkExec.pipelineLayout, 0, 1, &descriptor, 0, NULL );
			vkCmdPushConstants( cmd, vkExec.pipelineLayout,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
			vkCmdDrawIndexed( cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );
		}
	}
}

/*
====================
VK_GuiExecutor_EnsureFrameOpen

Clear-only frames (no 2D view was drawn) still need a presentable image.
====================
*/
bool VK_GuiExecutor_EnsureFrameOpen( void ) {
	return VK_GuiExecutor_BeginFrame();
}

bool VK_GuiExecutor_FrameIsOpen( void ) {
	return vkExec.frameOpen;
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
