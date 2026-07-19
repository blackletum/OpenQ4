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

// vk_Interactions.cpp: the Phase F1 per-light interaction pass, inserted
// between the depth fill and the ambient walks
void VK_Interactions_DrawLights( const viewDef_t *viewDef );

// vk_ShadowMap.cpp: module-owned shadow atlas teardown (Phase F2a)
void VK_ShadowMap_Shutdown( void );

// frame-scope split (Phase F2a): the swapchain rendering scope can be
// suspended for the shadow atlas caster pass and resumed with loadOp LOAD
bool VK_Exec_BeginMainRendering( bool clearColorDepth );
void VK_Exec_EndMainRendering( void );

// no module-owned vertex-cache GPU state exists: the engine cache runs
// CPU-backed under Vulkan and the executor streams into its own rings
void VK_VertexCache_Shutdown( void ) {
}

/*
====================
Executor state
====================
*/
// world views stream all visible geometry through the rings each frame
// (the vertex cache is CPU-backed); sized for q4dm2-scale 3D views
static const int VK_VERTEX_RING_BYTES = 32 * 1024 * 1024;
static const int VK_INDEX_RING_BYTES = 8 * 1024 * 1024;
static const int VK_MAX_GUI_PIPELINES = 64;
static const int VK_MAX_CUBE_PIPELINES = 32;
static const int VK_MAX_DESCRIPTOR_SETS = 4096;
// per-draw interaction blocks stream through a dynamic uniform ring;
// slices align to 256 (the guaranteed minUniformBufferOffsetAlignment
// ceiling) and the descriptor range is one slice, not WHOLE_SIZE, so the
// effective range stays under maxUniformBufferRange everywhere
static const int VK_UNIFORM_RING_BYTES = 2 * 1024 * 1024;
static const int VK_UNIFORM_SLICE_BYTES = 256;

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

typedef struct vkCubePipeline_s {
	int				blendBits;		// GLS src|dst blend bits
	bool			dirFromNormal;	// TG_DIFFUSE_CUBE: dir attribute reads the idDrawVert normal
	VkPipeline		pipeline;
} vkCubePipeline_t;

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

// per-frame memo of surfaces already streamed into the rings: the depth
// fill and the two ambient walks visit the same tris, and re-uploading
// triples ring traffic. Direct-mapped on the geometry pointers; a
// collision just re-uploads.
static const int VK_TRI_MEMO_SIZE = 1024;	// power of two
typedef struct vkVertUpload_s {
	const void *	vertKey;
	int				vertexOffset;
} vkVertUpload_t;
typedef struct vkIdxUpload_s {
	const void *	idxKey;
	int				indexOffset;
} vkIdxUpload_t;

typedef struct vkGuiExecutor_s {
	bool				initialized;

	VkShaderModule		vertModule;
	VkShaderModule		fragModule;
	VkShaderModule		skyVertModule;
	VkShaderModule		skyFragModule;
	VkShaderModule		interactionVertModule;
	VkShaderModule		interactionFragModule;
	VkShaderModule		interactionShadowVertModule;
	VkShaderModule		interactionShadowFragModule;
	VkShaderModule		casterVertModule;
	VkShaderModule		casterFragModule;
	VkDescriptorSetLayout setLayout;
	VkDescriptorSetLayout uboSetLayout;		// one dynamic uniform buffer (interaction block ring)
	// shadow receiver set: binding 0 = atlas + compare sampler (fragment),
	// binding 1 = dynamic uniform buffer (per-space shadow block ring slice)
	VkDescriptorSetLayout shadowSetLayout;
	VkDescriptorPool	descriptorPool;
	VkPipelineLayout	pipelineLayout;
	// interactions: 6 single-combined-sampler sets (0=specTable, 1=bump,
	// 2=falloff, 3=lightProjection, 4=diffuse, 5=specular) + set 6 dynamic UBO
	VkPipelineLayout	interactionPipelineLayout;
	// shadow-receiving interactions add set 7 (atlas compare sampler + shadow UBO)
	VkPipelineLayout	shadowInteractionPipelineLayout;
	VkPipeline			interactionPipeline;	// lazily built; ONE/ONE additive
	VkPipeline			shadowInteractionPipeline;	// lazily built; ONE/ONE additive + atlas sampling
	VkPipeline			casterPipeline;			// lazily built; depth-only atlas caster
	vkGuiPipeline_t		pipelines[ VK_MAX_GUI_PIPELINES ];
	int					numPipelines;
	vkCubePipeline_t	cubePipelines[ VK_MAX_CUBE_PIPELINES ];
	int					numCubePipelines;
	VkFormat			pipelineTargetFormat;	// swapchain format the pipelines were built for

	vkRing_t			vertexRings[ VK_FRAMES_IN_FLIGHT ];
	vkRing_t			indexRings[ VK_FRAMES_IN_FLIGHT ];
	vkRing_t			uniformRings[ VK_FRAMES_IN_FLIGHT ];
	VkDescriptorSet		uniformRingSets[ VK_FRAMES_IN_FLIGHT ];
	VkDescriptorSet		shadowSets[ VK_FRAMES_IN_FLIGHT ];
	bool				shadowSetsHaveAtlas;	// binding 0 written with a live atlas view

	vkDescriptorCacheEntry_t descriptorCache[ 4096 ];	// parallel to the image table

	// frame-in-progress state
	bool				frameOpen;
	bool				mainScopeOpen;		// the swapchain dynamic-rendering scope is recording
	int					frameSlot;
	uint32_t			swapImageIndex;
	VkCommandBuffer		cmd;
	float				clearColor[ 4 ];
	int					boundVertexOffset;	// binding-0 ring offset of the last VK_Exec_BindTriGeometry

	vkVertUpload_t		vertMemo[ VK_TRI_MEMO_SIZE ];
	vkIdxUpload_t		idxMemo[ VK_TRI_MEMO_SIZE ];
} vkGuiExecutor_t;

static vkGuiExecutor_t vkExec;

/*
====================
VK_FixupClipSpaceZ

The front-end builds GL-convention projections (NDC z in [-1,1]; the 2D
ortho even lands gui verts at exactly -1). Vulkan clips to 0 <= z <= w, so
every MVP is remapped at assembly: row2 = (row2 + row3) / 2. Window depth
then matches GL's glDepthRange(0,1), keeping depth-compare parity for the
world passes. Column-major float[16]: row2 = elements 2,6,10,14.
====================
*/
void VK_FixupClipSpaceZ( float dst[ 16 ], const float src[ 16 ] ) {
	if ( dst != src ) {
		memcpy( dst, src, 16 * sizeof( float ) );
	}
	for ( int col = 0; col < 4; col++ ) {
		dst[ col * 4 + 2 ] = 0.5f * ( src[ col * 4 + 2 ] + src[ col * 4 + 3 ] );
	}
}

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

// shared graphics-pipeline assembly for the GUI, cube-texgen, interaction,
// and shadow-caster variants: everything except the shader modules, vertex
// input, and pipeline layout is identical (dynamic depth/cull/bias state,
// blend from the GLS bits, dynamic rendering against the swapchain + depth
// formats). depthOnly pipelines target the shadow atlas: zero color
// attachments, same depth/stencil format (the atlas reuses vkCtx.depthFormat)
static VkPipeline VK_Exec_CreatePipeline( VkShaderModule vertModule, VkShaderModule fragModule,
		const VkPipelineVertexInputStateCreateInfo *vertexInput, int blendBits, VkPipelineLayout layout,
		bool depthOnly ) {
	VkPipelineShaderStageCreateInfo stages[ 2 ];
	memset( stages, 0, sizeof( stages ) );
	stages[ 0 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[ 0 ].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[ 0 ].module = vertModule;
	stages[ 0 ].pName = "main";
	stages[ 1 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[ 1 ].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[ 1 ].module = fragModule;
	stages[ 1 ].pName = "main";

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
	blendState.attachmentCount = depthOnly ? 0 : 1;
	blendState.pAttachments = depthOnly ? NULL : &blendAttachment;

	// depth/cull/bias are core-1.3 dynamic state, so one pipeline per blend
	// combination serves 2D (depth off) and the world passes (per-stage
	// depth func/write, per-material cull, polygon-offset)
	VkDynamicState dynamicStates[ 9 ] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
		VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
		VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
		VK_DYNAMIC_STATE_CULL_MODE,
		VK_DYNAMIC_STATE_FRONT_FACE,
		VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
		VK_DYNAMIC_STATE_DEPTH_BIAS,
	};
	VkPipelineDynamicStateCreateInfo dynamicState;
	memset( &dynamicState, 0, sizeof( dynamicState ) );
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 9;
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineDepthStencilStateCreateInfo depthStencil;
	memset( &depthStencil, 0, sizeof( depthStencil ) );
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	// test/write/compare are dynamic; stencil stays off until Phase G

	VkPipelineRenderingCreateInfo rendering;
	memset( &rendering, 0, sizeof( rendering ) );
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = depthOnly ? 0 : 1;
	rendering.pColorAttachmentFormats = depthOnly ? NULL : &vkCtx.swapchainFormat;
	rendering.depthAttachmentFormat = vkCtx.depthFormat;
	rendering.stencilAttachmentFormat = vkCtx.depthFormat;

	VkGraphicsPipelineCreateInfo gpci;
	memset( &gpci, 0, sizeof( gpci ) );
	gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.pNext = &rendering;
	gpci.stageCount = 2;
	gpci.pStages = stages;
	gpci.pVertexInputState = vertexInput;
	gpci.pInputAssemblyState = &inputAssembly;
	gpci.pViewportState = &viewportState;
	gpci.pRasterizationState = &raster;
	gpci.pMultisampleState = &multisample;
	gpci.pDepthStencilState = &depthStencil;
	gpci.pColorBlendState = &blendState;
	gpci.pDynamicState = &dynamicState;
	gpci.layout = layout;

	VkPipeline pipeline = VK_NULL_HANDLE;
	if ( vkCreateGraphicsPipelines( vkCtx.device, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: pipeline creation failed (blend 0x%x)", blendBits );
		return VK_NULL_HANDLE;
	}
	return pipeline;
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

	VkPipeline pipeline = VK_Exec_CreatePipeline( vkExec.vertModule, vkExec.fragModule, &vertexInput, blendBits, vkExec.pipelineLayout, false );
	if ( pipeline == VK_NULL_HANDLE ) {
		return vkExec.numPipelines > 0 ? vkExec.pipelines[ 0 ].pipeline : VK_NULL_HANDLE;
	}
	vkExec.pipelines[ vkExec.numPipelines ].blendBits = blendBits;
	vkExec.pipelines[ vkExec.numPipelines ].pipeline = pipeline;
	vkExec.numPipelines++;
	return pipeline;
}

// cube-texgen pipelines (TG_SKYBOX_CUBE / TG_WOBBLESKY_CUBE / TG_DIFFUSE_CUBE):
// position from the idDrawVert stream plus a vec3 direction attribute — the
// front-end texgen's tightly packed stream on binding 1 for the skies, or
// the idDrawVert normal straight off binding 0 for diffuse cube maps
static VkPipeline VK_GuiExecutor_GetCubePipeline( int stateBits, bool dirFromNormal ) {
	const int blendBits = stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );

	for ( int i = 0; i < vkExec.numCubePipelines; i++ ) {
		if ( vkExec.cubePipelines[ i ].blendBits == blendBits
				&& vkExec.cubePipelines[ i ].dirFromNormal == dirFromNormal ) {
			return vkExec.cubePipelines[ i ].pipeline;
		}
	}
	if ( vkExec.numCubePipelines >= VK_MAX_CUBE_PIPELINES ) {
		common->Warning( "Vulkan: cube pipeline cache exhausted" );
		return vkExec.cubePipelines[ 0 ].pipeline;
	}

	VkVertexInputBindingDescription bindings[ 2 ];
	memset( bindings, 0, sizeof( bindings ) );
	bindings[ 0 ].binding = 0;
	bindings[ 0 ].stride = sizeof( idDrawVert );
	bindings[ 0 ].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	bindings[ 1 ].binding = 1;
	bindings[ 1 ].stride = sizeof( idVec3 );
	bindings[ 1 ].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attrs[ 2 ];
	memset( attrs, 0, sizeof( attrs ) );
	attrs[ 0 ].location = 0;
	attrs[ 0 ].binding = 0;
	attrs[ 0 ].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[ 0 ].offset = 0;
	attrs[ 1 ].location = 1;
	attrs[ 1 ].format = VK_FORMAT_R32G32B32_SFLOAT;
	if ( dirFromNormal ) {
		attrs[ 1 ].binding = 0;
		attrs[ 1 ].offset = (uint32_t)offsetof( idDrawVert, normal );
	} else {
		attrs[ 1 ].binding = 1;
		attrs[ 1 ].offset = 0;
	}

	VkPipelineVertexInputStateCreateInfo vertexInput;
	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = dirFromNormal ? 1 : 2;
	vertexInput.pVertexBindingDescriptions = bindings;
	vertexInput.vertexAttributeDescriptionCount = 2;
	vertexInput.pVertexAttributeDescriptions = attrs;

	VkPipeline pipeline = VK_Exec_CreatePipeline( vkExec.skyVertModule, vkExec.skyFragModule, &vertexInput, blendBits, vkExec.pipelineLayout, false );
	if ( pipeline == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}
	vkExec.cubePipelines[ vkExec.numCubePipelines ].blendBits = blendBits;
	vkExec.cubePipelines[ vkExec.numCubePipelines ].dirFromNormal = dirFromNormal;
	vkExec.cubePipelines[ vkExec.numCubePipelines ].pipeline = pipeline;
	vkExec.numCubePipelines++;
	return pipeline;
}

// full idDrawVert vertex input shared by the interaction pipelines:
// xyz@0, color ubyte4@12, normal@16, tangent0@32, tangent1@44, st@56
// (64-byte stride, one binding)
static void VK_Exec_InteractionVertexInput( VkVertexInputBindingDescription &binding,
		VkVertexInputAttributeDescription attrs[ 6 ], VkPipelineVertexInputStateCreateInfo &vertexInput ) {
	memset( &binding, 0, sizeof( binding ) );
	binding.stride = sizeof( idDrawVert );
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	memset( attrs, 0, 6 * sizeof( attrs[ 0 ] ) );
	attrs[ 0 ].location = 0;
	attrs[ 0 ].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[ 0 ].offset = (uint32_t)offsetof( idDrawVert, xyz );
	attrs[ 1 ].location = 1;
	attrs[ 1 ].format = VK_FORMAT_R8G8B8A8_UNORM;
	attrs[ 1 ].offset = (uint32_t)offsetof( idDrawVert, color );
	attrs[ 2 ].location = 2;
	attrs[ 2 ].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[ 2 ].offset = (uint32_t)offsetof( idDrawVert, normal );
	attrs[ 3 ].location = 3;
	attrs[ 3 ].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[ 3 ].offset = (uint32_t)offsetof( idDrawVert, tangents );
	attrs[ 4 ].location = 4;
	attrs[ 4 ].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[ 4 ].offset = (uint32_t)( offsetof( idDrawVert, tangents ) + sizeof( idVec3 ) );
	attrs[ 5 ].location = 5;
	attrs[ 5 ].format = VK_FORMAT_R32G32_SFLOAT;
	attrs[ 5 ].offset = (uint32_t)offsetof( idDrawVert, st );

	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &binding;
	vertexInput.vertexAttributeDescriptionCount = 6;
	vertexInput.pVertexAttributeDescriptions = attrs;
}

// interaction pipeline (Phase F1): full idDrawVert vertex input, fixed
// ONE/ONE additive blend (the only state the GL interaction batch ever
// uses), depth func/write and cull through the shared dynamic state.
// Lazily built and dropped with the other pipelines on format changes.
VkPipeline VK_Exec_InteractionPipeline( void ) {
	if ( vkExec.interactionPipeline != VK_NULL_HANDLE ) {
		return vkExec.interactionPipeline;
	}
	if ( vkExec.interactionVertModule == VK_NULL_HANDLE || vkExec.interactionFragModule == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}

	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attrs[ 6 ];
	VkPipelineVertexInputStateCreateInfo vertexInput;
	VK_Exec_InteractionVertexInput( binding, attrs, vertexInput );

	vkExec.interactionPipeline = VK_Exec_CreatePipeline( vkExec.interactionVertModule, vkExec.interactionFragModule,
			&vertexInput, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE, vkExec.interactionPipelineLayout, false );
	return vkExec.interactionPipeline;
}

VkPipelineLayout VK_Exec_InteractionPipelineLayout( void ) {
	return vkExec.interactionPipelineLayout;
}

// shadow-receiving interaction variant (Phase F2a): same vertex input and
// additive blend, plus set 7 (atlas compare sampler + per-space shadow UBO)
VkPipeline VK_Exec_ShadowInteractionPipeline( void ) {
	if ( vkExec.shadowInteractionPipeline != VK_NULL_HANDLE ) {
		return vkExec.shadowInteractionPipeline;
	}
	if ( vkExec.interactionShadowVertModule == VK_NULL_HANDLE || vkExec.interactionShadowFragModule == VK_NULL_HANDLE
			|| vkExec.shadowInteractionPipelineLayout == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}

	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attrs[ 6 ];
	VkPipelineVertexInputStateCreateInfo vertexInput;
	VK_Exec_InteractionVertexInput( binding, attrs, vertexInput );

	vkExec.shadowInteractionPipeline = VK_Exec_CreatePipeline( vkExec.interactionShadowVertModule, vkExec.interactionShadowFragModule,
			&vertexInput, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE, vkExec.shadowInteractionPipelineLayout, false );
	return vkExec.shadowInteractionPipeline;
}

VkPipelineLayout VK_Exec_ShadowInteractionPipelineLayout( void ) {
	return vkExec.shadowInteractionPipelineLayout;
}

// depth-only shadow-map caster (Phase F2a): position + st off the idDrawVert
// stream (perforated casters alpha-test; opaque draws ignore the texcoord),
// zero color attachments, single-image layout (slot 0 = alpha map)
VkPipeline VK_Exec_CasterPipeline( void ) {
	if ( vkExec.casterPipeline != VK_NULL_HANDLE ) {
		return vkExec.casterPipeline;
	}
	if ( vkExec.casterVertModule == VK_NULL_HANDLE || vkExec.casterFragModule == VK_NULL_HANDLE ) {
		return VK_NULL_HANDLE;
	}

	VkVertexInputBindingDescription binding;
	memset( &binding, 0, sizeof( binding ) );
	binding.stride = sizeof( idDrawVert );
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attrs[ 2 ];
	memset( attrs, 0, sizeof( attrs ) );
	attrs[ 0 ].location = 0;
	attrs[ 0 ].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[ 0 ].offset = (uint32_t)offsetof( idDrawVert, xyz );
	attrs[ 1 ].location = 1;
	attrs[ 1 ].format = VK_FORMAT_R32G32_SFLOAT;
	attrs[ 1 ].offset = (uint32_t)offsetof( idDrawVert, st );

	VkPipelineVertexInputStateCreateInfo vertexInput;
	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &binding;
	vertexInput.vertexAttributeDescriptionCount = 2;
	vertexInput.pVertexAttributeDescriptions = attrs;

	vkExec.casterPipeline = VK_Exec_CreatePipeline( vkExec.casterVertModule, vkExec.casterFragModule,
			&vertexInput, 0, vkExec.pipelineLayout, true );
	return vkExec.casterPipeline;
}

VkPipelineLayout VK_Exec_BasePipelineLayout( void ) {
	return vkExec.pipelineLayout;
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
	smci.codeSize = vk_sky_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_sky_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.skyVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: sky vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_sky_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_sky_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.skyFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: sky fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_interaction_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_interaction_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.interactionVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: interaction vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_interaction_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_interaction_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.interactionFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: interaction fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_interaction_shadow_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_interaction_shadow_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.interactionShadowVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow interaction vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_interaction_shadow_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_interaction_shadow_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.interactionShadowFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow interaction fragment shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_shadow_caster_vert_spv_size;
	smci.pCode = (const uint32_t *)vk_shadow_caster_vert_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.casterVertModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow caster vertex shader module creation failed" );
		return false;
	}
	smci.codeSize = vk_shadow_caster_frag_spv_size;
	smci.pCode = (const uint32_t *)vk_shadow_caster_frag_spv;
	if ( vkCreateShaderModule( vkCtx.device, &smci, NULL, &vkExec.casterFragModule ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow caster fragment shader module creation failed" );
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

	// interaction block ring: one dynamic uniform buffer, both stages
	VkDescriptorSetLayoutBinding uboBindingInfo;
	memset( &uboBindingInfo, 0, sizeof( uboBindingInfo ) );
	uboBindingInfo.binding = 0;
	uboBindingInfo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	uboBindingInfo.descriptorCount = 1;
	uboBindingInfo.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	dslci.pBindings = &uboBindingInfo;
	if ( vkCreateDescriptorSetLayout( vkCtx.device, &dslci, NULL, &vkExec.uboSetLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: uniform descriptor set layout creation failed" );
		return false;
	}

	// shadow receiver set: atlas compare sampler + per-space shadow block
	// slice off the same uniform ring (second dynamic offset)
	VkDescriptorSetLayoutBinding shadowBindings[ 2 ];
	memset( shadowBindings, 0, sizeof( shadowBindings ) );
	shadowBindings[ 0 ].binding = 0;
	shadowBindings[ 0 ].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	shadowBindings[ 0 ].descriptorCount = 1;
	shadowBindings[ 0 ].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	shadowBindings[ 1 ].binding = 1;
	shadowBindings[ 1 ].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	shadowBindings[ 1 ].descriptorCount = 1;
	shadowBindings[ 1 ].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	dslci.bindingCount = 2;
	dslci.pBindings = shadowBindings;
	if ( vkCreateDescriptorSetLayout( vkCtx.device, &dslci, NULL, &vkExec.shadowSetLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow descriptor set layout creation failed" );
		return false;
	}
	dslci.bindingCount = 1;

	VkDescriptorPoolSize poolSizes[ 2 ];
	poolSizes[ 0 ].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[ 0 ].descriptorCount = VK_MAX_DESCRIPTOR_SETS + VK_FRAMES_IN_FLIGHT;
	poolSizes[ 1 ].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	poolSizes[ 1 ].descriptorCount = 2 * VK_FRAMES_IN_FLIGHT;
	VkDescriptorPoolCreateInfo dpci;
	memset( &dpci, 0, sizeof( dpci ) );
	dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	dpci.maxSets = VK_MAX_DESCRIPTOR_SETS + 2 * VK_FRAMES_IN_FLIGHT;
	dpci.poolSizeCount = 2;
	dpci.pPoolSizes = poolSizes;
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

	// interaction layout: slots 0..5 reuse the per-image single-sampler
	// layout (cached per-image sets bind directly), slot 6 is the ring UBO;
	// the push range keeps the shared 128B block
	VkDescriptorSetLayout interactionSetLayouts[ 8 ];
	for ( int i = 0; i < 6; i++ ) {
		interactionSetLayouts[ i ] = vkExec.setLayout;
	}
	interactionSetLayouts[ 6 ] = vkExec.uboSetLayout;
	plci.setLayoutCount = 7;
	plci.pSetLayouts = interactionSetLayouts;
	if ( vkCreatePipelineLayout( vkCtx.device, &plci, NULL, &vkExec.interactionPipelineLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: interaction pipeline layout creation failed" );
		return false;
	}

	// shadow-receiving interactions: the same seven slots plus set 7 (atlas
	// compare sampler + shadow block); dynamic offsets bind in set order,
	// so offset 0 = interaction slice, offset 1 = shadow slice
	interactionSetLayouts[ 7 ] = vkExec.shadowSetLayout;
	plci.setLayoutCount = 8;
	if ( vkCreatePipelineLayout( vkCtx.device, &plci, NULL, &vkExec.shadowInteractionPipelineLayout ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: shadow interaction pipeline layout creation failed" );
		return false;
	}

	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		if ( !VK_Ring_Create( vkExec.vertexRings[ i ], VK_VERTEX_RING_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT )
				|| !VK_Ring_Create( vkExec.indexRings[ i ], VK_INDEX_RING_BYTES, VK_BUFFER_USAGE_INDEX_BUFFER_BIT )
				|| !VK_Ring_Create( vkExec.uniformRings[ i ], VK_UNIFORM_RING_BYTES, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT ) ) {
			return false;
		}

		// one descriptor set per slot, written once: dynamic offsets select
		// the 256B slice at bind time
		VkDescriptorSetAllocateInfo dsai;
		memset( &dsai, 0, sizeof( dsai ) );
		dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		dsai.descriptorPool = vkExec.descriptorPool;
		dsai.descriptorSetCount = 1;
		dsai.pSetLayouts = &vkExec.uboSetLayout;
		if ( vkAllocateDescriptorSets( vkCtx.device, &dsai, &vkExec.uniformRingSets[ i ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: uniform ring descriptor set allocation failed" );
			return false;
		}
		VkDescriptorBufferInfo bufferInfo;
		bufferInfo.buffer = vkExec.uniformRings[ i ].buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = VK_UNIFORM_SLICE_BYTES;
		VkWriteDescriptorSet write;
		memset( &write, 0, sizeof( write ) );
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = vkExec.uniformRingSets[ i ];
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		write.pBufferInfo = &bufferInfo;
		vkUpdateDescriptorSets( vkCtx.device, 1, &write, 0, NULL );

		// shadow set per slot: the ring binding is written once here; the
		// atlas image binding is (re)written when the atlas is created
		dsai.pSetLayouts = &vkExec.shadowSetLayout;
		if ( vkAllocateDescriptorSets( vkCtx.device, &dsai, &vkExec.shadowSets[ i ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: shadow descriptor set allocation failed" );
			return false;
		}
		write.dstSet = vkExec.shadowSets[ i ];
		write.dstBinding = 1;
		vkUpdateDescriptorSets( vkCtx.device, 1, &write, 0, NULL );
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
	VK_ShadowMap_Shutdown();
	for ( int i = 0; i < vkExec.numPipelines; i++ ) {
		if ( vkExec.pipelines[ i ].pipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device, vkExec.pipelines[ i ].pipeline, NULL );
		}
	}
	for ( int i = 0; i < vkExec.numCubePipelines; i++ ) {
		if ( vkExec.cubePipelines[ i ].pipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device, vkExec.cubePipelines[ i ].pipeline, NULL );
		}
	}
	if ( vkExec.interactionPipeline != VK_NULL_HANDLE ) {
		vkDestroyPipeline( vkCtx.device, vkExec.interactionPipeline, NULL );
	}
	if ( vkExec.shadowInteractionPipeline != VK_NULL_HANDLE ) {
		vkDestroyPipeline( vkCtx.device, vkExec.shadowInteractionPipeline, NULL );
	}
	if ( vkExec.casterPipeline != VK_NULL_HANDLE ) {
		vkDestroyPipeline( vkCtx.device, vkExec.casterPipeline, NULL );
	}
	if ( vkExec.pipelineLayout != VK_NULL_HANDLE ) {
		vkDestroyPipelineLayout( vkCtx.device, vkExec.pipelineLayout, NULL );
	}
	if ( vkExec.interactionPipelineLayout != VK_NULL_HANDLE ) {
		vkDestroyPipelineLayout( vkCtx.device, vkExec.interactionPipelineLayout, NULL );
	}
	if ( vkExec.shadowInteractionPipelineLayout != VK_NULL_HANDLE ) {
		vkDestroyPipelineLayout( vkCtx.device, vkExec.shadowInteractionPipelineLayout, NULL );
	}
	if ( vkExec.descriptorPool != VK_NULL_HANDLE ) {
		vkDestroyDescriptorPool( vkCtx.device, vkExec.descriptorPool, NULL );
	}
	if ( vkExec.setLayout != VK_NULL_HANDLE ) {
		vkDestroyDescriptorSetLayout( vkCtx.device, vkExec.setLayout, NULL );
	}
	if ( vkExec.uboSetLayout != VK_NULL_HANDLE ) {
		vkDestroyDescriptorSetLayout( vkCtx.device, vkExec.uboSetLayout, NULL );
	}
	if ( vkExec.shadowSetLayout != VK_NULL_HANDLE ) {
		vkDestroyDescriptorSetLayout( vkCtx.device, vkExec.shadowSetLayout, NULL );
	}
	if ( vkExec.vertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.vertModule, NULL );
	}
	if ( vkExec.fragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.fragModule, NULL );
	}
	if ( vkExec.skyVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.skyVertModule, NULL );
	}
	if ( vkExec.skyFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.skyFragModule, NULL );
	}
	if ( vkExec.interactionVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.interactionVertModule, NULL );
	}
	if ( vkExec.interactionFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.interactionFragModule, NULL );
	}
	if ( vkExec.interactionShadowVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.interactionShadowVertModule, NULL );
	}
	if ( vkExec.interactionShadowFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.interactionShadowFragModule, NULL );
	}
	if ( vkExec.casterVertModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.casterVertModule, NULL );
	}
	if ( vkExec.casterFragModule != VK_NULL_HANDLE ) {
		vkDestroyShaderModule( vkCtx.device, vkExec.casterFragModule, NULL );
	}
	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		if ( vkExec.vertexRings[ i ].buffer != VK_NULL_HANDLE ) {
			vmaDestroyBuffer( vkCtx.allocator, vkExec.vertexRings[ i ].buffer, vkExec.vertexRings[ i ].allocation );
		}
		if ( vkExec.indexRings[ i ].buffer != VK_NULL_HANDLE ) {
			vmaDestroyBuffer( vkCtx.allocator, vkExec.indexRings[ i ].buffer, vkExec.indexRings[ i ].allocation );
		}
		if ( vkExec.uniformRings[ i ].buffer != VK_NULL_HANDLE ) {
			vmaDestroyBuffer( vkCtx.allocator, vkExec.uniformRings[ i ].buffer, vkExec.uniformRings[ i ].allocation );
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
	// a failed mid-run recreate tears the swapchain down entirely; keep
	// retrying until a usable swapchain (with depth images) exists
	if ( vkCtx.swapchain == VK_NULL_HANDLE || vkCtx.depthImages[ vkCtx.frameSlot ] == VK_NULL_HANDLE ) {
		if ( !VK_Device_RecreateSwapchain()
				|| vkCtx.swapchain == VK_NULL_HANDLE || vkCtx.depthImages[ vkCtx.frameSlot ] == VK_NULL_HANDLE ) {
			return false;
		}
	}
	// swapchain format changes (rare) invalidate the pipeline set
	if ( vkExec.pipelineTargetFormat != vkCtx.swapchainFormat ) {
		vkDeviceWaitIdle( vkCtx.device );
		for ( int i = 0; i < vkExec.numPipelines; i++ ) {
			vkDestroyPipeline( vkCtx.device, vkExec.pipelines[ i ].pipeline, NULL );
		}
		vkExec.numPipelines = 0;
		for ( int i = 0; i < vkExec.numCubePipelines; i++ ) {
			vkDestroyPipeline( vkCtx.device, vkExec.cubePipelines[ i ].pipeline, NULL );
		}
		vkExec.numCubePipelines = 0;
		if ( vkExec.interactionPipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device, vkExec.interactionPipeline, NULL );
			vkExec.interactionPipeline = VK_NULL_HANDLE;
		}
		if ( vkExec.shadowInteractionPipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device, vkExec.shadowInteractionPipeline, NULL );
			vkExec.shadowInteractionPipeline = VK_NULL_HANDLE;
		}
		if ( vkExec.casterPipeline != VK_NULL_HANDLE ) {
			vkDestroyPipeline( vkCtx.device, vkExec.casterPipeline, NULL );
			vkExec.casterPipeline = VK_NULL_HANDLE;
		}
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

	VkImageMemoryBarrier2 toAttachment[ 2 ];
	memset( toAttachment, 0, sizeof( toAttachment ) );
	toAttachment[ 0 ].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toAttachment[ 0 ].srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
	toAttachment[ 0 ].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	toAttachment[ 0 ].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	toAttachment[ 0 ].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toAttachment[ 0 ].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	toAttachment[ 0 ].image = vkCtx.swapchainImages[ imageIndex ];
	toAttachment[ 0 ].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toAttachment[ 0 ].subresourceRange.levelCount = 1;
	toAttachment[ 0 ].subresourceRange.layerCount = 1;
	toAttachment[ 1 ].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toAttachment[ 1 ].srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	toAttachment[ 1 ].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	toAttachment[ 1 ].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	toAttachment[ 1 ].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toAttachment[ 1 ].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	toAttachment[ 1 ].image = vkCtx.depthImages[ slot ];
	toAttachment[ 1 ].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	toAttachment[ 1 ].subresourceRange.levelCount = 1;
	toAttachment[ 1 ].subresourceRange.layerCount = 1;
	VkDependencyInfo dep;
	memset( &dep, 0, sizeof( dep ) );
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 2;
	dep.pImageMemoryBarriers = toAttachment;
	vkCmdPipelineBarrier2( cmd, &dep );

	vkExec.frameSlot = slot;
	vkExec.swapImageIndex = imageIndex;
	vkExec.cmd = cmd;
	VK_Exec_BeginMainRendering( true );

	vkExec.frameOpen = true;
	vkExec.vertexRings[ slot ].cursor = 0;
	vkExec.indexRings[ slot ].cursor = 0;
	vkExec.uniformRings[ slot ].cursor = 0;
	memset( vkExec.vertMemo, 0, sizeof( vkExec.vertMemo ) );
	memset( vkExec.idxMemo, 0, sizeof( vkExec.idxMemo ) );
	return true;
}

/*
====================
VK_Exec_BeginMainRendering / VK_Exec_EndMainRendering

The swapchain dynamic-rendering scope, factored so the Phase F2a shadow
pass can interrupt it mid-3D-view: end main rendering -> atlas caster scope
-> resume with loadOp LOAD for color AND depth. The clear path is the frame
open (byte-identical to the pre-split BeginFrame recording when no shadow
maps render); the resume path re-establishes the same baseline dynamic
state. Depth storeOp stays DONT_CARE on the r_useShadowMap 0 default and
switches to STORE only when a shadow interruption may need the depth-fill
contents to survive the scope break.
====================
*/
bool VK_Exec_BeginMainRendering( bool clearColorDepth ) {
	if ( vkExec.mainScopeOpen || vkExec.cmd == VK_NULL_HANDLE ) {
		return vkExec.mainScopeOpen;
	}
	VkCommandBuffer cmd = vkExec.cmd;

	VkRenderingAttachmentInfo color;
	memset( &color, 0, sizeof( color ) );
	color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color.imageView = vkCtx.swapchainViews[ vkExec.swapImageIndex ];
	color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color.loadOp = clearColorDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color.clearValue.color.float32[ 0 ] = vkExec.clearColor[ 0 ];
	color.clearValue.color.float32[ 1 ] = vkExec.clearColor[ 1 ];
	color.clearValue.color.float32[ 2 ] = vkExec.clearColor[ 2 ];
	color.clearValue.color.float32[ 3 ] = vkExec.clearColor[ 3 ];

	// depth/stencil attach for the whole frame; contents are transient (the
	// world passes re-clear per 3D view via vkCmdClearAttachments)
	VkRenderingAttachmentInfo depth;
	memset( &depth, 0, sizeof( depth ) );
	depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depth.imageView = vkCtx.depthViews[ vkExec.frameSlot ];
	depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depth.loadOp = clearColorDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	depth.storeOp = ( r_useShadowMap.GetBool() && r_shadows.GetBool() )
			? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth.clearValue.depthStencil.depth = 1.0f;
	depth.clearValue.depthStencil.stencil = 128;

	VkRenderingInfo ri;
	memset( &ri, 0, sizeof( ri ) );
	ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	ri.renderArea.extent = vkCtx.swapchainExtent;
	ri.layerCount = 1;
	ri.colorAttachmentCount = 1;
	ri.pColorAttachments = &color;
	ri.pDepthAttachment = &depth;
	ri.pStencilAttachment = &depth;
	vkCmdBeginRendering( cmd, &ri );

	// baseline dynamic state: 2D semantics (depth/cull off); the world
	// passes override per surface and the next 2D view resets here
	vkCmdSetDepthTestEnable( cmd, VK_FALSE );
	vkCmdSetDepthWriteEnable( cmd, VK_FALSE );
	vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_ALWAYS );
	vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );
	vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );
	vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	vkCmdSetDepthBias( cmd, 0.0f, 0.0f, 0.0f );

	vkExec.mainScopeOpen = true;
	return true;
}

void VK_Exec_EndMainRendering( void ) {
	if ( !vkExec.mainScopeOpen ) {
		return;
	}
	vkCmdEndRendering( vkExec.cmd );
	vkExec.mainScopeOpen = false;
}

bool VK_GuiExecutor_EndFrameAndPresent( void ) {
	if ( !vkExec.frameOpen ) {
		return false;
	}
	const int slot = vkExec.frameSlot;
	const uint32_t imageIndex = vkExec.swapImageIndex;
	VkCommandBuffer cmd = vkExec.cmd;

	VK_Exec_EndMainRendering();

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
Shared surface helpers (2D + world views)
====================
*/

// memoized ring upload + bind: the depth fill and both ambient walks visit
// the same tris; a hit re-binds without re-copying. Also serves the
// interaction pass, where the light-tris chains carry their own index
// subset over the shared ambient vertex cache (distinct idxKey, so memo
// entries never alias across the subsets).
bool VK_Exec_BindTriGeometry( VkCommandBuffer cmd, int slot, const srfTriangles_t *tri ) {
	const void *vertKey = tri->ambientCache;
	const void *idxKey = tri->indexes;

	// independent vert/index memos: light-tris chains carry their own index
	// subset over the SHARED ambient vertex array, so a combined memo would
	// re-upload the full vertex payload once per light per surface
	int vertexOffset;
	{
		const unsigned int memoIndex = (unsigned int)( ( ( (uintptr_t)vertKey ) >> 4 ) & ( VK_TRI_MEMO_SIZE - 1 ) );
		vkVertUpload_t &memo = vkExec.vertMemo[ memoIndex ];
		if ( memo.vertKey == vertKey && vertKey != NULL ) {
			vertexOffset = memo.vertexOffset;
		} else {
			const idDrawVert *verts = (const idDrawVert *)vertexCache.Position( tri->ambientCache );
			if ( verts == NULL ) {
				return false;
			}
			vertexOffset = VK_Ring_Alloc( vkExec.vertexRings[ slot ], verts, tri->numVerts * (int)sizeof( idDrawVert ), 64 );
			if ( vertexOffset < 0 ) {
				return false;
			}
			if ( vertKey != NULL ) {
				memo.vertKey = vertKey;
				memo.vertexOffset = vertexOffset;
			}
		}
	}
	int indexOffset;
	{
		const unsigned int memoIndex = (unsigned int)( ( ( (uintptr_t)idxKey ) >> 4 ) & ( VK_TRI_MEMO_SIZE - 1 ) );
		vkIdxUpload_t &memo = vkExec.idxMemo[ memoIndex ];
		if ( memo.idxKey == idxKey && idxKey != NULL ) {
			indexOffset = memo.indexOffset;
		} else {
			indexOffset = VK_Ring_Alloc( vkExec.indexRings[ slot ], tri->indexes, tri->numIndexes * (int)sizeof( glIndex_t ), 4 );
			if ( indexOffset < 0 ) {
				return false;
			}
			if ( idxKey != NULL ) {
				memo.idxKey = idxKey;
				memo.indexOffset = indexOffset;
			}
		}
	}

	VkDeviceSize vertexBindOffset = (VkDeviceSize)vertexOffset;
	vkCmdBindVertexBuffers( cmd, 0, 1, &vkExec.vertexRings[ slot ].buffer, &vertexBindOffset );
	vkCmdBindIndexBuffer( cmd, vkExec.indexRings[ slot ].buffer, (VkDeviceSize)indexOffset, VK_INDEX_TYPE_UINT32 );
	vkExec.boundVertexOffset = vertexOffset;	// the cube-texgen path re-binds binding 0 alongside its dir stream
	return true;
}

// per-surface scissor: viewport base + drawSurf scissor (GL bottom-left)
void VK_Exec_SetSurfScissor( VkCommandBuffer cmd, const viewDef_t *viewDef, const drawSurf_t *drawSurf, int fbHeight ) {
	const int vpX = viewDef->viewport.x1;
	const int vpYGL = viewDef->viewport.y1;
	const int vpW = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int vpH = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;

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
}

// mvp for a surface's space: GL projection (with the id depth hacks) times
// the space's modelview, then the Vulkan clip-z fixup. cl_gunfov's weapon
// projection refit is not carried into the module (rare tuner cvar).
void VK_BuildSurfMVP( const viewDef_t *viewDef, const drawSurf_t *drawSurf, float outMvp[ 16 ] ) {
	const struct viewEntity_s *space = drawSurf->space;
	float proj[ 16 ];
	memcpy( proj, viewDef->projectionMatrix, sizeof( proj ) );
	if ( space->modelDepthHack != 0.0f ) {
		proj[ 14 ] -= space->modelDepthHack;
	} else if ( space->weaponDepthHack ) {
		proj[ 14 ] *= 0.25f;
	}
	float mvpGL[ 16 ];
	myGlMultMatrix( space->modelViewMatrix, proj, mvpGL );
	VK_FixupClipSpaceZ( outMvp, mvpGL );
}

/*
====================
Interaction-pass accessors (vk_Interactions.cpp)

vkExec stays file-static; the interaction TU reaches the frame state and
the Phase F1 resources through these narrow hooks.
====================
*/
VkCommandBuffer VK_Exec_ActiveCmd( void ) {
	return vkExec.frameOpen ? vkExec.cmd : VK_NULL_HANDLE;
}

int VK_Exec_ActiveFrameSlot( void ) {
	return vkExec.frameSlot;
}

// cached per-image descriptor; NULL when the image has no device backing or
// (require2D) when its view is a cube — the interaction pipeline samples
// every slot through sampler2D and a cube view would trip validation
VkDescriptorSet VK_Exec_ImageDescriptor( unsigned int texnum, bool require2D ) {
	if ( require2D ) {
		const vkImageEntry_t *entry = VK_Image_GetEntry( texnum );
		if ( entry == NULL || entry->isCube ) {
			return VK_NULL_HANDLE;
		}
	}
	return VK_GuiExecutor_GetImageDescriptor( texnum );
}

VkDescriptorSet VK_Exec_InteractionUniformSet( void ) {
	return vkExec.uniformRingSets[ vkExec.frameSlot ];
}

// streams one interaction block into the frame's uniform ring; returns the
// 256-aligned dynamic offset, or -1 on overflow
int VK_Exec_InteractionUniformAlloc( const void *data, int bytes ) {
	if ( bytes > VK_UNIFORM_SLICE_BYTES ) {
		return -1;
	}
	return VK_Ring_Alloc( vkExec.uniformRings[ vkExec.frameSlot ], data, bytes, VK_UNIFORM_SLICE_BYTES );
}

// the frame slot's shadow set (atlas compare sampler + shadow-block ring),
// or NULL until the atlas descriptor has been written
VkDescriptorSet VK_Exec_ShadowDescriptorSet( void ) {
	return vkExec.shadowSetsHaveAtlas ? vkExec.shadowSets[ vkExec.frameSlot ] : VK_NULL_HANDLE;
}

// (re)points every frame slot's shadow set at the atlas view + compare
// sampler (vk_ShadowMap.cpp calls this at atlas creation, before the set is
// ever bound; recreation waits the device idle first). NULL view clears.
bool VK_Exec_UpdateShadowAtlasDescriptors( VkImageView view, VkSampler sampler ) {
	vkExec.shadowSetsHaveAtlas = false;
	if ( view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE ) {
		return true;
	}
	if ( !vkExec.initialized ) {
		return false;
	}
	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		if ( vkExec.shadowSets[ i ] == VK_NULL_HANDLE ) {
			return false;
		}
		VkDescriptorImageInfo imageInfo;
		memset( &imageInfo, 0, sizeof( imageInfo ) );
		imageInfo.sampler = sampler;
		imageInfo.imageView = view;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		VkWriteDescriptorSet write;
		memset( &write, 0, sizeof( write ) );
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = vkExec.shadowSets[ i ];
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfo;
		vkUpdateDescriptorSets( vkCtx.device, 1, &write, 0, NULL );
	}
	vkExec.shadowSetsHaveAtlas = true;
	return true;
}

// the RB_STD_T_RenderShaderPasses ambient-stage walk shared by 2D views and
// the world ambient passes. Geometry and scissor are already bound; mvp is
// clip-z fixed. worldDepthState additionally applies each stage's GLS depth
// bits. Cube texgens (skybox/wobblesky/diffuse) draw through the cube
// pipeline; reflect/screen texgens remain later-phase gaps.
static void VK_Exec_DrawAmbientStages( const viewDef_t *viewDef, const drawSurf_t *drawSurf,
		const srfTriangles_t *tri, const float mvp[ 16 ], bool worldDepthState ) {
	VkCommandBuffer cmd = vkExec.cmd;
	const idMaterial *shader = drawSurf->material;
	const float *regs = drawSurf->shaderRegisters;

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
			continue;	// program stages: unsupported flat-render gap
		}
		// cube texgens draw through the cube pipeline; reflect/screen texgens
		// remain later-phase gaps
		const int texgen = pStage->texture.texgen;
		const bool cubeStage = texgen == TG_SKYBOX_CUBE || texgen == TG_WOBBLESKY_CUBE || texgen == TG_DIFFUSE_CUBE;
		if ( worldDepthState && texgen != TG_EXPLICIT && !cubeStage ) {
			continue;
		}
		if ( ( texgen == TG_SKYBOX_CUBE || texgen == TG_WOBBLESKY_CUBE ) && drawSurf->dynamicTexCoords == NULL ) {
			continue;	// the front-end texgen produced no direction stream
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
		// cube stages sample through samplerCube; the descriptor's view must
		// really be a cube view or validation trips
		if ( cubeStage ) {
			const vkImageEntry_t *cubeEntry = VK_Image_GetEntry( stageImage->GetDeviceHandle() );
			if ( cubeEntry == NULL || !cubeEntry->isCube ) {
				continue;
			}
		}
		VkDescriptorSet descriptor = VK_GuiExecutor_GetImageDescriptor( stageImage->GetDeviceHandle() );
		if ( descriptor == VK_NULL_HANDLE ) {
			continue;
		}

		// skybox/wobblesky direction stream: the front-end texgen writes
		// tightly packed vec3s into the CPU-backed vertex cache; stream them
		// into the frame ring and bind them as binding 1 next to the re-bound
		// idDrawVert stream (diffuse cube reads the idDrawVert normal off
		// binding 0 instead, so no extra buffer is needed)
		if ( texgen == TG_SKYBOX_CUBE || texgen == TG_WOBBLESKY_CUBE ) {
			const void *dirCoords = vertexCache.Position( drawSurf->dynamicTexCoords );
			if ( dirCoords == NULL ) {
				continue;
			}
			const int slot = vkExec.frameSlot;
			const int dirOffset = VK_Ring_Alloc( vkExec.vertexRings[ slot ], dirCoords,
					tri->numVerts * (int)sizeof( idVec3 ), 16 );
			if ( dirOffset < 0 ) {
				continue;
			}
			VkBuffer dirBuffers[ 2 ] = { vkExec.vertexRings[ slot ].buffer, vkExec.vertexRings[ slot ].buffer };
			VkDeviceSize dirOffsets[ 2 ] = { (VkDeviceSize)vkExec.boundVertexOffset, (VkDeviceSize)dirOffset };
			vkCmdBindVertexBuffers( cmd, 0, 2, dirBuffers, dirOffsets );
		}

		vkGuiPushConstants_t push;
		memcpy( push.mvp, mvp, sizeof( push.mvp ) );
		// decal surfaces bake regs-color (x depth fade) into the uploaded
		// vertex colors; modulating by the regs color again double-applies
		// it (the skip culls above still use the regs color)
		const bool bakedDecalStageColor = drawSurf->decalColorCache != NULL
				&& stageNum < drawSurf->decalColorStageCount
				&& drawSurf->decalColorStride > 0
				&& pStage->vertexColor != SVC_IGNORE;
		if ( bakedDecalStageColor ) {
			push.stageColor[ 0 ] = 1.0f;
			push.stageColor[ 1 ] = 1.0f;
			push.stageColor[ 2 ] = 1.0f;
			push.stageColor[ 3 ] = 1.0f;
		} else {
			push.stageColor[ 0 ] = color[ 0 ];
			push.stageColor[ 1 ] = color[ 1 ];
			push.stageColor[ 2 ] = color[ 2 ];
			push.stageColor[ 3 ] = color[ 3 ];
		}

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
				// inverted compare is rare; approximate as none
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

		if ( worldDepthState ) {
			// stage depth semantics from the material parse: opaque and
			// perforated stages test EQUAL against the depth fill,
			// translucents test LEQUAL; GLS_DEPTHMASK set = writes OFF
			const int bits = pStage->drawStateBits;
			VkCompareOp compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			if ( bits & GLS_DEPTHFUNC_EQUAL ) {
				compareOp = VK_COMPARE_OP_EQUAL;
			} else if ( bits & GLS_DEPTHFUNC_ALWAYS ) {
				compareOp = VK_COMPARE_OP_ALWAYS;
			}
			vkCmdSetDepthCompareOp( cmd, compareOp );
			vkCmdSetDepthWriteEnable( cmd, ( bits & GLS_DEPTHMASK ) ? VK_FALSE : VK_TRUE );
		}

		// per-stage polygon offset (RB_PrepareStageTexturing contract)
		const bool stagePolygonOffset = worldDepthState && pStage->privatePolygonOffset != 0.0f;
		if ( stagePolygonOffset ) {
			vkCmdSetDepthBiasEnable( cmd, VK_TRUE );
			vkCmdSetDepthBias( cmd, r_offsetUnits.GetFloat() * pStage->privatePolygonOffset, 0.0f, r_offsetFactor.GetFloat() );
		}

		VkPipeline pipeline;
		if ( cubeStage ) {
			pipeline = VK_GuiExecutor_GetCubePipeline( pStage->drawStateBits, texgen == TG_DIFFUSE_CUBE );
		} else {
			pipeline = VK_GuiExecutor_GetPipeline( pStage->drawStateBits );
		}
		if ( pipeline == VK_NULL_HANDLE ) {
			continue;
		}

		// one-shot bring-up evidence that a cube texgen actually drew
		if ( cubeStage ) {
			static bool loggedFirstCubeStage = false;
			if ( !loggedFirstCubeStage ) {
				loggedFirstCubeStage = true;
				common->Printf( "Vulkan: first cube-texgen stage drew (texgen %d, %s)\n", texgen, shader->GetName() );
			}
		}
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkExec.pipelineLayout, 0, 1, &descriptor, 0, NULL );
		vkCmdPushConstants( cmd, vkExec.pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
		vkCmdDrawIndexed( cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );

		if ( stagePolygonOffset ) {
			if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
				vkCmdSetDepthBias( cmd, r_offsetUnits.GetFloat() * shader->GetPolygonOffset(), 0.0f, r_offsetFactor.GetFloat() );
			} else {
				vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
			}
		}
	}
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

	// 2D semantics regardless of what an earlier 3D view left behind
	vkCmdSetDepthTestEnable( cmd, VK_FALSE );
	vkCmdSetDepthWriteEnable( cmd, VK_FALSE );
	vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_ALWAYS );
	vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );
	vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );
	vkCmdSetDepthBiasEnable( cmd, VK_FALSE );

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

	// the front-end ortho projection is the whole-view MVP
	float mvp[ 16 ];
	VK_FixupClipSpaceZ( mvp, viewDef->projectionMatrix );

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
		if ( !VK_Exec_BindTriGeometry( cmd, slot, tri ) ) {
			continue;
		}
		VK_Exec_SetSurfScissor( cmd, viewDef, drawSurf, fbHeight );
		VK_Exec_DrawAmbientStages( viewDef, drawSurf, tri, mvp, false );
	}
}

/*
====================
VK_GuiExecutor_Draw3DView

Phase E world consumer: RB_STD_DrawView's depth prepass + the two ambient
shader-pass walks (pre-fog: decals and sort < SS_MEDIUM; post-fog:
SS_MEDIUM..<SS_POST_PROCESS). Interactions, fog, and post-process surfaces
belong to later phases.
====================
*/
void VK_GuiExecutor_Draw3DView( const viewDef_t *viewDef ) {
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

	drawSurf_t **drawSurfs = (drawSurf_t **)viewDef->drawSurfs;
	const int numDrawSurfs = viewDef->numDrawSurfs;

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

	// every 3D view starts from clean depth/stencil, exactly like
	// RB_BeginDrawingView's glClear (subviews are separate earlier commands).
	// The rect must stay inside the render area (the viewDef can carry a
	// stale, larger size for one frame across an OUT_OF_DATE recreate)
	{
		int x0 = vpX > 0 ? vpX : 0;
		int y0 = fbHeight - vpYGL - vpH;
		if ( y0 < 0 ) {
			y0 = 0;
		}
		int x1 = vpX + vpW;
		if ( x1 > (int)vkCtx.swapchainExtent.width ) {
			x1 = (int)vkCtx.swapchainExtent.width;
		}
		int y1 = fbHeight - vpYGL;
		if ( y1 > (int)vkCtx.swapchainExtent.height ) {
			y1 = (int)vkCtx.swapchainExtent.height;
		}
		if ( x1 > x0 && y1 > y0 ) {
			VkClearAttachment clearAtt;
			memset( &clearAtt, 0, sizeof( clearAtt ) );
			clearAtt.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			clearAtt.clearValue.depthStencil.depth = 1.0f;
			clearAtt.clearValue.depthStencil.stencil = 128;
			VkClearRect clearRect;
			memset( &clearRect, 0, sizeof( clearRect ) );
			clearRect.rect.offset.x = x0;
			clearRect.rect.offset.y = y0;
			clearRect.rect.extent.width = (uint32_t)( x1 - x0 );
			clearRect.rect.extent.height = (uint32_t)( y1 - y0 );
			clearRect.layerCount = 1;
			vkCmdClearAttachments( cmd, 1, &clearAtt, 1, &clearRect );
		}
	}

	// per-space state tracking (matrix + weapon depth-range hack)
	const struct viewEntity_s *currentSpace = NULL;
	bool weaponDepthRange = false;
	float mvp[ 16 ];
	VK_FixupClipSpaceZ( mvp, viewDef->projectionMatrix );

	// ---- pass 1: depth fill (RB_STD_FillDepthBuffer contract) ----
	vkCmdSetDepthTestEnable( cmd, VK_TRUE );
	vkCmdSetDepthWriteEnable( cmd, VK_TRUE );
	vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_LESS_OR_EQUAL );
	vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	// the GL fill runs under RB_BeginDrawingView's front-sided cull
	vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );
	vkCmdSetCullMode( cmd, viewDef->isMirror ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT );

	for ( int surfNum = 0; surfNum < numDrawSurfs; surfNum++ ) {
		const drawSurf_t *drawSurf = drawSurfs[ surfNum ];
		const idMaterial *shader = drawSurf->material;
		const srfTriangles_t *tri = drawSurf->geo;
		if ( shader == NULL || tri == NULL || !shader->IsDrawn() ) {
			continue;
		}
		if ( tri->numIndexes <= 0 || tri->indexes == NULL ) {
			continue;
		}
		if ( shader->Coverage() == MC_TRANSLUCENT ) {
			continue;	// translucents neither write nor test here
		}
		if ( tri->ambientCache == NULL ) {
			continue;
		}
		const float *regs = drawSurf->shaderRegisters;

		// if all stages are conditioned off, skip
		int stage;
		const int stageCount = shader->GetNumStages();
		for ( stage = 0; stage < stageCount; stage++ ) {
			const shaderStage_t *pStage = shader->GetStage( stage );
			if ( regs == NULL || regs[ pStage->conditionRegister ] != 0 ) {
				break;
			}
		}
		if ( stage == stageCount ) {
			continue;
		}

		if ( !VK_Exec_BindTriGeometry( cmd, slot, tri ) ) {
			continue;
		}
		VK_Exec_SetSurfScissor( cmd, viewDef, drawSurf, fbHeight );

		// space change: rebuild the MVP (depth hacks included) and the
		// weapon depth-range window
		if ( drawSurf->space != currentSpace ) {
			currentSpace = drawSurf->space;
			VK_BuildSurfMVP( viewDef, drawSurf, mvp );
			const bool wantWeaponRange = drawSurf->space->weaponDepthHack;
			if ( wantWeaponRange != weaponDepthRange ) {
				weaponDepthRange = wantWeaponRange;
				viewport.maxDepth = wantWeaponRange ? 0.5f : 1.0f;
				vkCmdSetViewport( cmd, 0, 1, &viewport );
			}
		}

		// polygon offset per material
		if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
			vkCmdSetDepthBiasEnable( cmd, VK_TRUE );
			vkCmdSetDepthBias( cmd, r_offsetUnits.GetFloat() * shader->GetPolygonOffset(), 0.0f, r_offsetFactor.GetFloat() );
		}

		// subviews down-modulate instead of drawing black
		const bool isSubview = shader->GetSort() == SS_SUBVIEW;
		vkGuiPushConstants_t push;
		memcpy( push.mvp, mvp, sizeof( push.mvp ) );
		push.stageColor[ 0 ] = push.stageColor[ 1 ] = push.stageColor[ 2 ] = isSubview ? 1.0f : 0.0f;
		push.stageColor[ 3 ] = 1.0f;
		push.texMatrixS[ 0 ] = 1.0f; push.texMatrixS[ 1 ] = 0.0f; push.texMatrixS[ 2 ] = 0.0f; push.texMatrixS[ 3 ] = 0.0f;
		push.texMatrixT[ 0 ] = 0.0f; push.texMatrixT[ 1 ] = 1.0f; push.texMatrixT[ 2 ] = 0.0f; push.texMatrixT[ 3 ] = 0.0f;
		push.params[ 0 ] = 0.0f;	// SVC_IGNORE
		push.params[ 1 ] = 0.0f;	// alpha test off (per-stage below)
		push.params[ 2 ] = 0.0f;
		push.params[ 3 ] = 0.0f;	// no texmatrix

		const int fillBlendBits = isSubview ? ( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO ) : 0;

		bool drawSolid = shader->Coverage() == MC_OPAQUE;

		if ( shader->Coverage() == MC_PERFORATED ) {
			// alpha-tested stages punch holes; if none draws, fall back solid
			bool didDraw = false;
			for ( stage = 0; stage < stageCount; stage++ ) {
				const shaderStage_t *pStage = shader->GetStage( stage );
				if ( !pStage->hasAlphaTest ) {
					continue;
				}
				if ( regs != NULL && regs[ pStage->conditionRegister ] == 0 ) {
					continue;
				}
				didDraw = true;
				const float stageAlpha = regs != NULL ? regs[ pStage->color.registers[ 3 ] ] : 1.0f;
				if ( stageAlpha <= 0.0f ) {
					continue;
				}
				if ( pStage->texture.image == NULL || pStage->texture.texgen != TG_EXPLICIT ) {
					continue;
				}
				// only greater-style compares map onto the shader's test
				if ( pStage->alphaTestMode != GL_GREATER ) {
					continue;
				}
				VkDescriptorSet stageDescriptor = VK_GuiExecutor_GetImageDescriptor( pStage->texture.image->GetDeviceHandle() );
				if ( stageDescriptor == VK_NULL_HANDLE ) {
					continue;
				}
				// per-stage polygon offset (RB_PrepareStageTexturing contract)
				const bool stagePolygonOffset = pStage->privatePolygonOffset != 0.0f;
				if ( stagePolygonOffset ) {
					vkCmdSetDepthBiasEnable( cmd, VK_TRUE );
					vkCmdSetDepthBias( cmd, r_offsetUnits.GetFloat() * pStage->privatePolygonOffset, 0.0f, r_offsetFactor.GetFloat() );
				}
				push.stageColor[ 3 ] = stageAlpha;
				push.params[ 1 ] = 1.0f;
				push.params[ 2 ] = regs != NULL ? regs[ pStage->alphaTestRegister ] : 0.5f;
				if ( pStage->texture.hasMatrix && regs != NULL ) {
					push.texMatrixS[ 0 ] = regs[ pStage->texture.matrix[ 0 ][ 0 ] ];
					push.texMatrixS[ 1 ] = regs[ pStage->texture.matrix[ 0 ][ 1 ] ];
					push.texMatrixS[ 3 ] = regs[ pStage->texture.matrix[ 0 ][ 2 ] ];
					push.texMatrixT[ 0 ] = regs[ pStage->texture.matrix[ 1 ][ 0 ] ];
					push.texMatrixT[ 1 ] = regs[ pStage->texture.matrix[ 1 ][ 1 ] ];
					push.texMatrixT[ 3 ] = regs[ pStage->texture.matrix[ 1 ][ 2 ] ];
					push.params[ 3 ] = 1.0f;
				}
				VkPipeline pipeline = VK_GuiExecutor_GetPipeline( fillBlendBits );
				if ( pipeline == VK_NULL_HANDLE ) {
					continue;
				}
				vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
				vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkExec.pipelineLayout, 0, 1, &stageDescriptor, 0, NULL );
				vkCmdPushConstants( cmd, vkExec.pipelineLayout,
						VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
				vkCmdDrawIndexed( cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );
				if ( stagePolygonOffset ) {
					if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
						vkCmdSetDepthBias( cmd, r_offsetUnits.GetFloat() * shader->GetPolygonOffset(), 0.0f, r_offsetFactor.GetFloat() );
					} else {
						vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
					}
				}
				// restore solid-fill push defaults for the next stage
				push.stageColor[ 3 ] = 1.0f;
				push.params[ 1 ] = 0.0f;
				push.params[ 2 ] = 0.0f;
				push.params[ 3 ] = 0.0f;
			}
			if ( !didDraw ) {
				drawSolid = true;
			}
		}

		if ( drawSolid ) {
			VkDescriptorSet whiteDescriptor = VK_GuiExecutor_GetImageDescriptor( globalImages->whiteImage->GetDeviceHandle() );
			VkPipeline pipeline = VK_GuiExecutor_GetPipeline( fillBlendBits );
			if ( whiteDescriptor != VK_NULL_HANDLE && pipeline != VK_NULL_HANDLE ) {
				vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
				vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkExec.pipelineLayout, 0, 1, &whiteDescriptor, 0, NULL );
				vkCmdPushConstants( cmd, vkExec.pipelineLayout,
						VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
				vkCmdDrawIndexed( cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );
			}
		}

		if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
			vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
		}
	}

	// ---- interactions: per-light bump/diffuse/specular (Phase F1) ----
	// RB_STD_DrawView order: depth fill, then RB_ARB2_DrawInteractions,
	// then the ambient walks. The pass tracks its own space/depth-range
	// state and exits at maxDepth 1.0 with depth bias off; restart the
	// walk baseline to match.
	VK_Interactions_DrawLights( viewDef );
	currentSpace = NULL;
	weaponDepthRange = false;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport( cmd, 0, 1, &viewport );

	// ---- passes 2+3: ambient shader walks split at the fog boundary ----
	// post-process surfaces (sort >= SS_POST_PROCESS) need _currentRender
	// captures (Phase H); the walks stop at that boundary
	int processed = numDrawSurfs;
	for ( int i = 0; i < numDrawSurfs; i++ ) {
		if ( drawSurfs[ i ]->material != NULL && drawSurfs[ i ]->material->GetSort() >= SS_POST_PROCESS ) {
			processed = i;
			break;
		}
	}

	if ( !r_skipAmbient.GetBool() ) {
		for ( int pass = 0; pass < 2; pass++ ) {
			for ( int surfNum = 0; surfNum < processed; surfNum++ ) {
				const drawSurf_t *drawSurf = drawSurfs[ surfNum ];
				const idMaterial *shader = drawSurf->material;
				if ( shader == NULL || !shader->HasAmbient() || shader->IsPortalSky() ) {
					continue;
				}
				if ( shader->SuppressInSubview() ) {
					continue;
				}

				// pre-fog: decal materials + sort < SS_MEDIUM; post-fog:
				// SS_MEDIUM..<SS_POST_PROCESS (RB_DrawSurfIs*FogMaterialPass)
				const bool isDecal = drawSurf->decalColorCache != NULL
						|| ( shader->GetSort() >= SS_DECAL && shader->GetSort() < SS_FAR );
				bool inPass;
				if ( pass == 0 ) {
					inPass = isDecal ? !r_skipDecals.GetBool() : shader->GetSort() < SS_MEDIUM;
				} else {
					inPass = !isDecal && shader->GetSort() >= SS_MEDIUM && shader->GetSort() < SS_POST_PROCESS;
				}
				if ( !inPass ) {
					continue;
				}

				const srfTriangles_t *tri = drawSurf->geo;
				if ( tri == NULL || tri->numIndexes <= 0 || tri->indexes == NULL || tri->ambientCache == NULL ) {
					continue;
				}
				if ( !VK_Exec_BindTriGeometry( cmd, slot, tri ) ) {
					continue;
				}
				VK_Exec_SetSurfScissor( cmd, viewDef, drawSurf, fbHeight );

				if ( drawSurf->space != currentSpace ) {
					currentSpace = drawSurf->space;
					const bool wantWeaponRange = drawSurf->space->weaponDepthHack;
					if ( wantWeaponRange != weaponDepthRange ) {
						weaponDepthRange = wantWeaponRange;
						viewport.maxDepth = wantWeaponRange ? 0.5f : 1.0f;
						vkCmdSetViewport( cmd, 0, 1, &viewport );
					}
				}
				VK_BuildSurfMVP( viewDef, drawSurf, mvp );

				// material cull with the mirror swap (GL_Cull contract)
				switch ( shader->GetCullType() ) {
					case CT_TWO_SIDED:
						vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );
						break;
					case CT_BACK_SIDED:
						vkCmdSetCullMode( cmd, viewDef->isMirror ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT );
						break;
					default:
						vkCmdSetCullMode( cmd, viewDef->isMirror ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT );
						break;
				}

				if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
					vkCmdSetDepthBiasEnable( cmd, VK_TRUE );
					vkCmdSetDepthBias( cmd, r_offsetUnits.GetFloat() * shader->GetPolygonOffset(), 0.0f, r_offsetFactor.GetFloat() );
				}

				vkCmdSetDepthTestEnable( cmd, VK_TRUE );
				VK_Exec_DrawAmbientStages( viewDef, drawSurf, tri, mvp, true );

				if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
					vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
				}
			}
		}
	}

	// leave 2D-friendly state for a following HUD view
	if ( weaponDepthRange ) {
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport( cmd, 0, 1, &viewport );
	}
	vkCmdSetDepthTestEnable( cmd, VK_FALSE );
	vkCmdSetDepthWriteEnable( cmd, VK_FALSE );
	vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );

	// one-shot bring-up evidence that the world walk emitted real work
	static bool loggedFirstWorldView = false;
	if ( !loggedFirstWorldView ) {
		loggedFirstWorldView = true;
		common->Printf( "Vulkan: first world view drew %d surfaces (rings: %d KB verts, %d KB indexes)\n",
				numDrawSurfs, vkExec.vertexRings[ slot ].cursor / 1024, vkExec.indexRings[ slot ].cursor / 1024 );
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
