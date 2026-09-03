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

/*
===============================================================================

	Shadow-pass GPU timing

	One ring of timestamp pairs, shared by every view in flight. A span is
	claimed when a phase begins and released once its result is read, so a
	subview's spans cannot overwrite the main view's before they resolve.

	Nothing here ever stalls unless r_shadowMapGpuSyncTimings asks it to: an
	unresolved span is simply carried to the next view.

===============================================================================
*/

// Enough for several views of a frame to be in flight at once without a span
// being reclaimed before its result is read. Ring pressure degrades to a
// dropped sample, never to a wait.
static const int VK_SHADOW_TIMING_RING_SPANS = 64;

// A span this old is not merely slow. Either its submission failed or its
// command buffer was discarded, and waiting on it would never return, so
// it is dropped instead. Generous enough that a genuinely long frame is
// never mistaken for one.
static const int VK_SHADOW_TIMING_MAX_SPAN_AGE_FRAMES = 120;

typedef struct vkShadowTimingSpan_s {
	bool					claimed;	// holds two queries in the pool
	bool					open;		// begin written, end not yet
	bool					pending;	// both written, result not read
	vkShadowTimingPhase_t	phase;
	int						frameNumber;
} vkShadowTimingSpan_t;

static VkQueryPool vkShadowTimingQueryPool = VK_NULL_HANDLE;
static VkDevice vkShadowTimingOwnerDevice = VK_NULL_HANDLE;
static vkShadowTimingSpan_t vkShadowTimingSpans[ VK_SHADOW_TIMING_RING_SPANS ];
static int vkShadowTimingNextSpan = 0;
static int vkShadowTimingFrameSlot = 0;
static vkShadowGpuTimingReport_t vkShadowTimingReport;

const char *VK_ShadowGpuTiming_PhaseName( vkShadowTimingPhase_t phase ) {
	switch ( phase ) {
	case VK_SHADOW_TIMING_MAP_RENDER:
		return "map";
	case VK_SHADOW_TIMING_CACHE_REUSE:
		return "reuse";
	default:
		return "unknown";
	}
}

static bool VK_ShadowGpuTiming_Available( void ) {
	// Same device requirements as the whole-frame facility; the pool is
	// separate only because the spans are.
	return VK_GpuFrameTiming_Available();
}

bool VK_ShadowGpuTiming_Enabled( void ) {
	// OpenGL writes its shadow timer queries whenever r_shadowMapGpuTimerQueries
	// is set, independently of whether a report prints this frame, so the
	// numbers are already warm when one does. Mirror that: a handful of
	// timestamps per view is not worth gating on the report interval.
	return VK_ShadowGpuTiming_Available()
		&& ( r_shadowMapGpuTimerQueries.GetBool()
			|| r_shadowMapGpuSyncTimings.GetBool() );
}

static bool VK_ShadowGpuTiming_EnsureQueryPool( void ) {
	if ( vkShadowTimingQueryPool != VK_NULL_HANDLE
			&& vkShadowTimingOwnerDevice == vkCtx.device ) {
		return true;
	}
	if ( vkShadowTimingOwnerDevice != VK_NULL_HANDLE
			&& vkShadowTimingOwnerDevice != vkCtx.device ) {
		// The old device is gone; its non-dispatchable pool handle must never
		// reach the replacement device.
		vkShadowTimingQueryPool = VK_NULL_HANDLE;
		memset( vkShadowTimingSpans, 0, sizeof( vkShadowTimingSpans ) );
		vkShadowTimingNextSpan = 0;
	}

	VkQueryPoolCreateInfo createInfo;
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	createInfo.queryCount = VK_SHADOW_TIMING_RING_SPANS * 2;
	if ( vkCreateQueryPool( vkCtx.device, &createInfo, NULL,
			&vkShadowTimingQueryPool ) != VK_SUCCESS ) {
		vkShadowTimingQueryPool = VK_NULL_HANDLE;
		vkShadowTimingOwnerDevice = VK_NULL_HANDLE;
		return false;
	}
	vkShadowTimingOwnerDevice = vkCtx.device;
	return true;
}

static void VK_ShadowGpuTiming_ReleaseSpan( vkShadowTimingSpan_t &span ) {
	span.claimed = false;
	span.open = false;
	span.pending = false;
}

// Reads one finished span into the report. A synchronized resolve waits for
// the result; the default poll leaves an unready span for the next view.
static void VK_ShadowGpuTiming_ResolveSpan( vkShadowTimingSpan_t &span,
		const int spanIndex, const bool synchronized ) {
	std::uint64_t timestamps[ 2 ] = { 0, 0 };
	VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;
	if ( synchronized ) {
		flags |= VK_QUERY_RESULT_WAIT_BIT;
	}
	const VkResult result = vkGetQueryPoolResults( vkCtx.device,
		vkShadowTimingQueryPool, static_cast<std::uint32_t>( spanIndex * 2 ), 2,
		sizeof( timestamps ), timestamps, sizeof( timestamps[ 0 ] ), flags );
	if ( result == VK_NOT_READY ) {
		// Still executing. Carry it: the span stays claimed so its queries are
		// not reused underneath the GPU.
		vkShadowTimingReport.pending++;
		return;
	}
	if ( result != VK_SUCCESS ) {
		vkShadowTimingReport.dropped++;
		VK_ShadowGpuTiming_ReleaseSpan( span );
		return;
	}

	const std::uint64_t ticks = GpuFrameTimingCore_TimestampDelta(
		timestamps[ 0 ], timestamps[ 1 ], vkCtx.graphicsTimestampValidBits );
	const std::uint64_t microseconds = GpuFrameTimingCore_TicksToMicroseconds(
		ticks, static_cast<double>(
			vkCtx.deviceProperties.limits.timestampPeriod ) );
	const double milliseconds = static_cast<double>( microseconds ) * 0.001;
	const int phase = idMath::ClampInt( 0, VK_SHADOW_TIMING_PHASE_COUNT - 1,
		static_cast<int>( span.phase ) );
	vkShadowTimingReport.totalMilliseconds += milliseconds;
	vkShadowTimingReport.samples++;
	vkShadowTimingReport.phaseMilliseconds[ phase ] += milliseconds;
	vkShadowTimingReport.phaseSamples[ phase ]++;
	VK_ShadowGpuTiming_ReleaseSpan( span );
}

void VK_ShadowGpuTiming_BeginView( int frameSlot ) {
	const bool available = VK_ShadowGpuTiming_Available();
	const bool enabled = VK_ShadowGpuTiming_Enabled();
	const bool synchronized = enabled && r_shadowMapGpuSyncTimings.GetBool();

	// The report describes what became readable during THIS view, which is the
	// same lagged attribution the OpenGL shadow stats use.
	memset( &vkShadowTimingReport, 0, sizeof( vkShadowTimingReport ) );
	vkShadowTimingReport.available = available;
	vkShadowTimingReport.enabled = enabled;
	vkShadowTimingReport.synchronized = synchronized;

	vkShadowTimingFrameSlot = frameSlot;
	if ( vkShadowTimingQueryPool == VK_NULL_HANDLE ) {
		return;
	}
	if ( !enabled ) {
		// Timing was switched off. Nothing will read these results, but a span
		// whose command buffer is still in flight owns queries the GPU may yet
		// write, and a later claim would reset them underneath it. Release
		// only what has certainly retired; the rest ages out on later views.
		for ( int i = 0 ; i < VK_SHADOW_TIMING_RING_SPANS ; i++ ) {
			vkShadowTimingSpan_t &span = vkShadowTimingSpans[ i ];
			if ( span.claimed
					&& tr.frameCount - span.frameNumber
						> VK_FRAMES_IN_FLIGHT ) {
				VK_ShadowGpuTiming_ReleaseSpan( span );
			}
		}
		return;
	}

	for ( int i = 0 ; i < VK_SHADOW_TIMING_RING_SPANS ; i++ ) {
		vkShadowTimingSpan_t &span = vkShadowTimingSpans[ i ];
		if ( !span.claimed ) {
			continue;
		}
		if ( !span.pending ) {
			// Opened and never closed: an early exit between the two halves of
			// a phase. It holds two queries and will never resolve, so reclaim
			// it once the command buffer that wrote its begin has retired.
			if ( !span.open
					|| tr.frameCount - span.frameNumber
						> VK_FRAMES_IN_FLIGHT ) {
				if ( span.open ) {
					vkShadowTimingReport.dropped++;
				}
				VK_ShadowGpuTiming_ReleaseSpan( span );
			}
			continue;
		}
		// A span recorded this frame has not been submitted yet, so a
		// synchronized read of it would wait forever. Only ever resolve spans
		// from an earlier frame.
		if ( span.frameNumber >= tr.frameCount ) {
			vkShadowTimingReport.pending++;
			continue;
		}
		const int age = tr.frameCount - span.frameNumber;
		if ( age > VK_SHADOW_TIMING_MAX_SPAN_AGE_FRAMES ) {
			// Its submission never happened. Reclaim the queries rather than
			// waiting on a result that will not arrive.
			vkShadowTimingReport.dropped++;
			VK_ShadowGpuTiming_ReleaseSpan( span );
			continue;
		}
		// Only wait once this frame slot has come back around, because by
		// then the frame loop has already waited its fence: the work is
		// complete, so the wait returns immediately, and a submission that
		// never happened has already hung that fence wait rather than this
		// one. Younger spans are polled and simply carried.
		VK_ShadowGpuTiming_ResolveSpan( span, i,
			synchronized && age > VK_FRAMES_IN_FLIGHT );
	}
}

static int VK_ShadowGpuTiming_ClaimSpan( const vkShadowTimingPhase_t phase ) {
	for ( int attempt = 0 ; attempt < VK_SHADOW_TIMING_RING_SPANS ; attempt++ ) {
		const int index = vkShadowTimingNextSpan;
		vkShadowTimingNextSpan =
			( vkShadowTimingNextSpan + 1 ) % VK_SHADOW_TIMING_RING_SPANS;
		vkShadowTimingSpan_t &span = vkShadowTimingSpans[ index ];
		if ( span.claimed ) {
			continue;
		}
		span.claimed = true;
		span.open = true;
		span.pending = false;
		span.phase = phase;
		span.frameNumber = tr.frameCount;
		return index;
	}
	return -1;
}

static int VK_ShadowGpuTiming_FindOpenSpan( const vkShadowTimingPhase_t phase ) {
	// The most recently claimed open span of this phase is the one being
	// closed; scan backwards from the ring cursor so nesting cannot mismatch.
	for ( int offset = 1 ; offset <= VK_SHADOW_TIMING_RING_SPANS ; offset++ ) {
		const int index = ( vkShadowTimingNextSpan
			- offset + VK_SHADOW_TIMING_RING_SPANS * 2 )
			% VK_SHADOW_TIMING_RING_SPANS;
		const vkShadowTimingSpan_t &span = vkShadowTimingSpans[ index ];
		if ( span.claimed && span.open && span.phase == phase ) {
			return index;
		}
	}
	return -1;
}

void VK_ShadowGpuTiming_BeginPhase( VkCommandBuffer commandBuffer,
		const vkShadowTimingPhase_t phase ) {
	if ( commandBuffer == VK_NULL_HANDLE || !VK_ShadowGpuTiming_Enabled()
			|| !VK_ShadowGpuTiming_EnsureQueryPool() ) {
		return;
	}
	const int index = VK_ShadowGpuTiming_ClaimSpan( phase );
	if ( index < 0 ) {
		// Every span is still in flight. Skip this region rather than stalling
		// to free one.
		vkShadowTimingReport.dropped++;
		return;
	}
	const std::uint32_t firstQuery = static_cast<std::uint32_t>( index * 2 );
	// Illegal inside a dynamic-rendering scope, which is why every caller
	// opens its span before vkCmdBeginRendering.
	vkCmdResetQueryPool( commandBuffer, vkShadowTimingQueryPool, firstQuery, 2 );
	vkCmdWriteTimestamp( commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		vkShadowTimingQueryPool, firstQuery );
}

void VK_ShadowGpuTiming_EndPhase( VkCommandBuffer commandBuffer,
		const vkShadowTimingPhase_t phase ) {
	if ( commandBuffer == VK_NULL_HANDLE
			|| vkShadowTimingQueryPool == VK_NULL_HANDLE ) {
		return;
	}
	const int index = VK_ShadowGpuTiming_FindOpenSpan( phase );
	if ( index < 0 ) {
		return;
	}
	vkShadowTimingSpan_t &span = vkShadowTimingSpans[ index ];
	vkCmdWriteTimestamp( commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		vkShadowTimingQueryPool, static_cast<std::uint32_t>( index * 2 + 1 ) );
	span.open = false;
	span.pending = true;
}

// Spans recorded into a command buffer that will not be submitted would
// otherwise be waited on forever under a synchronized resolve.
static void VK_ShadowGpuTiming_ReleaseFrameSpans( const int frameNumber ) {
	for ( int i = 0 ; i < VK_SHADOW_TIMING_RING_SPANS ; i++ ) {
		vkShadowTimingSpan_t &span = vkShadowTimingSpans[ i ];
		if ( span.claimed && span.frameNumber == frameNumber ) {
			if ( span.open || span.pending ) {
				vkShadowTimingReport.dropped++;
			}
			VK_ShadowGpuTiming_ReleaseSpan( span );
		}
	}
}

void VK_ShadowGpuTiming_AbandonView( void ) {
	VK_ShadowGpuTiming_ReleaseFrameSpans( tr.frameCount );
}


const vkShadowGpuTimingReport_t &VK_ShadowGpuTiming_Report( void ) {
	return vkShadowTimingReport;
}

void VK_ShadowGpuTiming_Shutdown( void ) {
	if ( vkShadowTimingQueryPool != VK_NULL_HANDLE
			&& vkShadowTimingOwnerDevice != VK_NULL_HANDLE ) {
		// Shutdown/vid_restart already accepts device retirement; the wait keeps
		// the pool alive until no command buffer references it.
		vkDeviceWaitIdle( vkShadowTimingOwnerDevice );
		vkDestroyQueryPool( vkShadowTimingOwnerDevice, vkShadowTimingQueryPool,
			NULL );
	}
	vkShadowTimingQueryPool = VK_NULL_HANDLE;
	vkShadowTimingOwnerDevice = VK_NULL_HANDLE;
	memset( vkShadowTimingSpans, 0, sizeof( vkShadowTimingSpans ) );
	memset( &vkShadowTimingReport, 0, sizeof( vkShadowTimingReport ) );
	vkShadowTimingNextSpan = 0;
	vkShadowTimingFrameSlot = 0;
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
