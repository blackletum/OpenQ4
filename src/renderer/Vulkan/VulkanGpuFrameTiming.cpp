// Copyright (C) 2026 DarkMatter Productions
//

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../GpuFrameTimingCore.h"
#include "../RendererMetrics.h"
#include "VulkanGpuFrameTiming.h"

typedef struct vkGpuFrameTimingSlot_s {
	bool			open;
	bool			pending;
	int				frameNumber;
	unsigned int	generation;
} vkGpuFrameTimingSlot_t;

static VkQueryPool vkGpuTimingQueryPool = VK_NULL_HANDLE;
static VkDevice vkGpuTimingOwnerDevice = VK_NULL_HANDLE;
static vkGpuFrameTimingSlot_t vkGpuTimingSlots[ VK_FRAMES_IN_FLIGHT ];
static bool vkGpuTimingEnableStateKnown = false;
static bool vkGpuTimingEnabledLastFrame = false;

bool VK_GpuFrameTiming_Available( void ) {
	return vkCtx.initialized
		&& vkCtx.device != VK_NULL_HANDLE
		&& vkCtx.graphicsTimestampValidBits > 0
		&& vkCtx.deviceProperties.limits.timestampPeriod > 0.0f
		&& vkCreateQueryPool != NULL
		&& vkDestroyQueryPool != NULL
		&& vkGetQueryPoolResults != NULL
		&& vkCmdResetQueryPool != NULL
		&& vkCmdWriteTimestamp != NULL;
}

static bool VK_GpuFrameTiming_EnsureQueryPool( void ) {
	if ( vkGpuTimingQueryPool != VK_NULL_HANDLE && vkGpuTimingOwnerDevice == vkCtx.device ) {
		return true;
	}
	if ( vkGpuTimingOwnerDevice != VK_NULL_HANDLE && vkGpuTimingOwnerDevice != vkCtx.device ) {
		// The old device is already gone. Its non-dispatchable query-pool handle
		// must not be presented to the replacement device.
		vkGpuTimingQueryPool = VK_NULL_HANDLE;
		memset( vkGpuTimingSlots, 0, sizeof( vkGpuTimingSlots ) );
		R_RendererMetrics_ResetGpuFrameTiming( "Vulkan device generation changed" );
	}

	VkQueryPoolCreateInfo createInfo;
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	createInfo.queryCount = VK_FRAMES_IN_FLIGHT * 2;
	if ( vkCreateQueryPool( vkCtx.device, &createInfo, NULL, &vkGpuTimingQueryPool ) != VK_SUCCESS ) {
		vkGpuTimingQueryPool = VK_NULL_HANDLE;
		vkGpuTimingOwnerDevice = VK_NULL_HANDLE;
		return false;
	}
	vkGpuTimingOwnerDevice = vkCtx.device;
	return true;
}

static void VK_GpuFrameTiming_ResolveRetiredSlot( int frameSlot, int currentFrameNumber ) {
	vkGpuFrameTimingSlot_t &slot = vkGpuTimingSlots[ frameSlot ];
	if ( !slot.pending ) {
		return;
	}

	std::uint64_t timestamps[ 2 ] = { 0, 0 };
	const VkResult result = vkGetQueryPoolResults( vkCtx.device, vkGpuTimingQueryPool,
		static_cast<std::uint32_t>( frameSlot * 2 ), 2, sizeof( timestamps ), timestamps,
		sizeof( timestamps[ 0 ] ), VK_QUERY_RESULT_64_BIT );
	// Deliberately no VK_QUERY_RESULT_WAIT_BIT: this function is reached only
	// after the normal frame-slot fence has retired, but readiness is still
	// treated as fallible and never escalated into an extra wait.
	if ( result == VK_NOT_READY ) {
		R_RendererMetrics_RecordGpuFrameTimingUnavailable();
		R_RendererMetrics_RecordGpuFrameTimingDropped();
		slot.pending = false;
		return;
	}
	if ( result != VK_SUCCESS ) {
		R_RendererMetrics_RecordGpuFrameTimingDropped();
		slot.pending = false;
		return;
	}

	const std::uint64_t ticks = GpuFrameTimingCore_TimestampDelta(
		timestamps[ 0 ], timestamps[ 1 ], vkCtx.graphicsTimestampValidBits );
	const std::uint64_t elapsedMicroseconds = GpuFrameTimingCore_TicksToMicroseconds(
		ticks, static_cast<double>( vkCtx.deviceProperties.limits.timestampPeriod ) );
	R_RendererMetrics_RecordGpuFrameTimingResolved( RENDER_GPU_TIMING_BACKEND_VULKAN,
		slot.frameNumber, slot.generation, elapsedMicroseconds, currentFrameNumber );
	slot.pending = false;
}

void VK_GpuFrameTiming_BeginFrame( VkCommandBuffer commandBuffer, int frameSlot,
		int frameNumber ) {
	const bool supported = VK_GpuFrameTiming_Available();
	R_RendererMetrics_SetGpuFrameTimingBackend( RENDER_GPU_TIMING_BACKEND_VULKAN, supported );
	const bool enabled = supported
		&& ( r_rendererGpuTimers.GetBool()
			|| r_rendererDynamicResolution.GetBool() );
	if ( vkGpuTimingEnableStateKnown && enabled != vkGpuTimingEnabledLastFrame ) {
		R_RendererMetrics_ResetGpuFrameTiming( enabled
			? "Vulkan whole-frame timing enabled" : "Vulkan whole-frame timing disabled" );
	}
	vkGpuTimingEnableStateKnown = true;
	vkGpuTimingEnabledLastFrame = enabled;

	if ( !enabled || commandBuffer == VK_NULL_HANDLE || frameSlot < 0
			|| frameSlot >= VK_FRAMES_IN_FLIGHT ) {
		return;
	}
	if ( !VK_GpuFrameTiming_EnsureQueryPool() ) {
		R_RendererMetrics_RecordGpuFrameTimingDropped();
		return;
	}

	VK_GpuFrameTiming_ResolveRetiredSlot( frameSlot, frameNumber );
	vkGpuFrameTimingSlot_t &slot = vkGpuTimingSlots[ frameSlot ];
	const std::uint32_t firstQuery = static_cast<std::uint32_t>( frameSlot * 2 );
	vkCmdResetQueryPool( commandBuffer, vkGpuTimingQueryPool, firstQuery, 2 );
	vkCmdWriteTimestamp( commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		vkGpuTimingQueryPool, firstQuery );
	slot.open = true;
	slot.pending = false;
	slot.frameNumber = frameNumber;
	slot.generation = R_RendererMetrics_GpuFrameTimingGeneration();
}

void VK_GpuFrameTiming_EndFrame( VkCommandBuffer commandBuffer, int frameSlot ) {
	if ( vkGpuTimingQueryPool == VK_NULL_HANDLE || commandBuffer == VK_NULL_HANDLE
			|| frameSlot < 0 || frameSlot >= VK_FRAMES_IN_FLIGHT ) {
		return;
	}
	vkGpuFrameTimingSlot_t &slot = vkGpuTimingSlots[ frameSlot ];
	if ( !slot.open ) {
		return;
	}
	vkCmdWriteTimestamp( commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		vkGpuTimingQueryPool, static_cast<std::uint32_t>( frameSlot * 2 + 1 ) );
	slot.open = false;
	slot.pending = true;
}

void VK_GpuFrameTiming_SubmitFailed( int frameSlot ) {
	if ( frameSlot < 0 || frameSlot >= VK_FRAMES_IN_FLIGHT ) {
		return;
	}
	vkGpuFrameTimingSlot_t &slot = vkGpuTimingSlots[ frameSlot ];
	if ( slot.open || slot.pending ) {
		R_RendererMetrics_RecordGpuFrameTimingDropped();
	}
	slot.open = false;
	slot.pending = false;
}

void VK_GpuFrameTiming_Shutdown( void ) {
	if ( vkGpuTimingQueryPool != VK_NULL_HANDLE && vkGpuTimingOwnerDevice != VK_NULL_HANDLE ) {
		// Shutdown/vid_restart already accepts device retirement. Keeping the wait
		// here prevents destroying a pool referenced by an in-flight command
		// buffer; normal frame sampling never calls a device/queue wait.
		vkDeviceWaitIdle( vkGpuTimingOwnerDevice );
		vkDestroyQueryPool( vkGpuTimingOwnerDevice, vkGpuTimingQueryPool, NULL );
	}
	vkGpuTimingQueryPool = VK_NULL_HANDLE;
	vkGpuTimingOwnerDevice = VK_NULL_HANDLE;
	memset( vkGpuTimingSlots, 0, sizeof( vkGpuTimingSlots ) );
	vkGpuTimingEnableStateKnown = false;
	vkGpuTimingEnabledLastFrame = false;
}

bool VK_GpuFrameTiming_RunSelfTest( void ) {
	if ( !VK_GpuFrameTiming_Available() ) {
		common->Printf( "RendererGpuTimer self-test skipped: Vulkan timestamps are unavailable\n" );
		return true;
	}
	VkQueryPoolCreateInfo createInfo;
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	createInfo.queryCount = 2;
	VkQueryPool testPool = VK_NULL_HANDLE;
	if ( vkCreateQueryPool( vkCtx.device, &createInfo, NULL, &testPool ) != VK_SUCCESS ) {
		common->Printf( "RendererGpuTimer self-test failed: Vulkan query-pool creation failed\n" );
		return false;
	}
	vkDestroyQueryPool( vkCtx.device, testPool, NULL );
	common->Printf( "RendererGpuTimer self-test passed (Vulkan timestampPeriod=%.6fns validBits=%u waits=0)\n",
		vkCtx.deviceProperties.limits.timestampPeriod, vkCtx.graphicsTimestampValidBits );
	return true;
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
