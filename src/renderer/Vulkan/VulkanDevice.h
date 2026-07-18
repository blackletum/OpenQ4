// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __VULKANDEVICE_H__
#define __VULKANDEVICE_H__

/*
===============================================================================

	Persistent Vulkan device + swapchain context (Phase C,
	docs/dev/plans/2026-07-17-vulkan-phase-c.md).

	Owns the instance, surface (created through the engine's window
	services), physical/logical device, queues, VMA allocator, swapchain,
	and the frames-in-flight synchronization. Phase C drives it to an
	animated clear; later phases attach the real draw path.

===============================================================================
*/

#include "volk.h"

struct renderWindowServices_s;

// per-slot frame synchronization (frames in flight)
static const int VK_FRAMES_IN_FLIGHT = 2;

typedef struct vkDeviceContext_s {
	bool				initialized;

	VkInstance			instance;
	VkDebugUtilsMessengerEXT debugMessenger;
	VkSurfaceKHR		surface;
	VkPhysicalDevice	physicalDevice;
	VkPhysicalDeviceProperties deviceProperties;
	VkDevice			device;
	uint32_t			graphicsQueueFamily;	// also the present family (required)
	VkQueue				graphicsQueue;

	VkSwapchainKHR		swapchain;
	VkFormat			swapchainFormat;
	VkExtent2D			swapchainExtent;
	VkPresentModeKHR	presentMode;
	uint32_t			swapchainImageCount;
	VkImage				swapchainImages[ 8 ];
	VkImageView			swapchainViews[ 8 ];

	VkCommandPool		commandPool;
	VkCommandBuffer		commandBuffers[ VK_FRAMES_IN_FLIGHT ];
	VkSemaphore			acquireSemaphores[ VK_FRAMES_IN_FLIGHT ];
	// one render-finished semaphore per swapchain image: present may still
	// read the semaphore of an image the acquire slot has already recycled
	VkSemaphore			renderFinishedSemaphores[ 8 ];
	VkFence				frameFences[ VK_FRAMES_IN_FLIGHT ];
	int					frameSlot;

	// requested swap interval the swapchain was created with; a change
	// triggers recreation at the next present
	int					swapInterval;
} vkDeviceContext_t;

// the module-wide device context; valid while initialized is true
extern vkDeviceContext_t vkCtx;

// full bring-up through the window services: instance (+validation when
// r_vkValidation), surface, device selection honoring r_vkDevice, queues,
// swapchain, per-frame sync. Returns false with everything torn down on any
// failure so the loader's fail-closed ladder stays reachable.
bool	VK_Device_Init( const renderWindowServices_s *windowServices );
void	VK_Device_Shutdown( void );

// recreates the swapchain (resize / OUT_OF_DATE / swap-interval change);
// reads the current window pixel size through the services
bool	VK_Device_RecreateSwapchain( void );

// acquires, records a dynamic-rendering clear with the given color, and
// presents; handles OUT_OF_DATE/SUBOPTIMAL by recreating and retrying once
void	VK_Device_PresentClearFrame( const float clearColor[ 4 ] );

#endif /* !__VULKANDEVICE_H__ */
