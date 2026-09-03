// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __VULKAN_GPU_FRAME_TIMING_H__
#define __VULKAN_GPU_FRAME_TIMING_H__

#include "VulkanDevice.h"

bool VK_GpuFrameTiming_Available( void );
void VK_GpuFrameTiming_BeginFrame( VkCommandBuffer commandBuffer, int frameSlot,
	int frameNumber );
void VK_GpuFrameTiming_EndFrame( VkCommandBuffer commandBuffer, int frameSlot );
void VK_GpuFrameTiming_SubmitFailed( int frameSlot );
void VK_GpuFrameTiming_Shutdown( void );
bool VK_GpuFrameTiming_RunSelfTest( void );

#endif /* !__VULKAN_GPU_FRAME_TIMING_H__ */
