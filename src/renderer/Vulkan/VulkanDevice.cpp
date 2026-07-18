// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	Persistent Vulkan device + swapchain context (Phase C).

	See VulkanDevice.h. All window/surface operations cross the engine's
	renderWindowServices_t; the module never links the windowing library.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../RenderModuleAPI.h"
#include "VulkanDevice.h"

vkDeviceContext_t vkCtx;

static const renderWindowServices_t *vkWindowServices = NULL;

extern idCVar r_vkValidation;
extern idCVar r_vkDevice;
extern idCVar r_swapInterval;

/*
====================
VK_DebugMessengerCallback
====================
*/
static VKAPI_ATTR VkBool32 VKAPI_CALL VK_DebugMessengerCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT types,
		const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
		void *userData ) {
	(void)types;
	(void)userData;
	if ( callbackData == NULL || callbackData->pMessage == NULL ) {
		return VK_FALSE;
	}
	if ( severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) {
		common->Warning( "Vulkan validation: %s", callbackData->pMessage );
	} else if ( severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ) {
		common->Warning( "Vulkan validation: %s", callbackData->pMessage );
	} else {
		common->DPrintf( "Vulkan validation: %s\n", callbackData->pMessage );
	}
	return VK_FALSE;
}

/*
====================
VK_Device_DestroySwapchainObjects
====================
*/
static void VK_Device_DestroySwapchainObjects( void ) {
	for ( uint32_t i = 0; i < vkCtx.swapchainImageCount; i++ ) {
		if ( vkCtx.swapchainViews[ i ] != VK_NULL_HANDLE ) {
			vkDestroyImageView( vkCtx.device, vkCtx.swapchainViews[ i ], NULL );
			vkCtx.swapchainViews[ i ] = VK_NULL_HANDLE;
		}
		if ( vkCtx.renderFinishedSemaphores[ i ] != VK_NULL_HANDLE ) {
			vkDestroySemaphore( vkCtx.device, vkCtx.renderFinishedSemaphores[ i ], NULL );
			vkCtx.renderFinishedSemaphores[ i ] = VK_NULL_HANDLE;
		}
	}
	if ( vkCtx.swapchain != VK_NULL_HANDLE ) {
		vkDestroySwapchainKHR( vkCtx.device, vkCtx.swapchain, NULL );
		vkCtx.swapchain = VK_NULL_HANDLE;
	}
	vkCtx.swapchainImageCount = 0;
}

/*
====================
VK_Device_CreateSwapchain
====================
*/
static bool VK_Device_CreateSwapchain( void ) {
	VkSurfaceCapabilitiesKHR caps;
	if ( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vkCtx.physicalDevice, vkCtx.surface, &caps ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed" );
		return false;
	}

	// surface format: prefer BGRA8/UNORM sRGB-less for parity with the GL
	// default framebuffer, fall back to the first reported format
	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR( vkCtx.physicalDevice, vkCtx.surface, &formatCount, NULL );
	if ( formatCount == 0 ) {
		common->Warning( "Vulkan: surface reports no formats" );
		return false;
	}
	if ( formatCount > 64 ) {
		formatCount = 64;
	}
	VkSurfaceFormatKHR formats[ 64 ];
	vkGetPhysicalDeviceSurfaceFormatsKHR( vkCtx.physicalDevice, vkCtx.surface, &formatCount, formats );
	VkSurfaceFormatKHR chosen = formats[ 0 ];
	for ( uint32_t i = 0; i < formatCount; i++ ) {
		if ( ( formats[ i ].format == VK_FORMAT_B8G8R8A8_UNORM || formats[ i ].format == VK_FORMAT_R8G8B8A8_UNORM )
				&& formats[ i ].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ) {
			chosen = formats[ i ];
			break;
		}
	}

	// present mode from r_swapInterval: 0 = IMMEDIATE (or MAILBOX when
	// IMMEDIATE is absent), else FIFO (always available)
	const int requestedInterval = r_swapInterval.GetInteger();
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	if ( requestedInterval == 0 ) {
		uint32_t modeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR( vkCtx.physicalDevice, vkCtx.surface, &modeCount, NULL );
		if ( modeCount > 16 ) {
			modeCount = 16;
		}
		VkPresentModeKHR modes[ 16 ];
		vkGetPhysicalDeviceSurfacePresentModesKHR( vkCtx.physicalDevice, vkCtx.surface, &modeCount, modes );
		for ( uint32_t i = 0; i < modeCount; i++ ) {
			if ( modes[ i ] == VK_PRESENT_MODE_IMMEDIATE_KHR ) {
				presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
				break;
			}
			if ( modes[ i ] == VK_PRESENT_MODE_MAILBOX_KHR ) {
				presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
			}
		}
	}

	VkExtent2D extent = caps.currentExtent;
	if ( extent.width == 0xFFFFFFFFu ) {
		// surface size is window-driven; poll the engine window
		renderModuleWindowInfo_t info;
		memset( &info, 0, sizeof( info ) );
		if ( vkWindowServices != NULL && vkWindowServices->RefreshNativeWindowHandles != NULL ) {
			vkWindowServices->RefreshNativeWindowHandles( &info );
		}
		extent.width = info.pixelWidth > 0 ? (uint32_t)info.pixelWidth : 640u;
		extent.height = info.pixelHeight > 0 ? (uint32_t)info.pixelHeight : 480u;
	}
	if ( extent.width < caps.minImageExtent.width ) {
		extent.width = caps.minImageExtent.width;
	}
	if ( extent.height < caps.minImageExtent.height ) {
		extent.height = caps.minImageExtent.height;
	}
	if ( caps.maxImageExtent.width > 0 && extent.width > caps.maxImageExtent.width ) {
		extent.width = caps.maxImageExtent.width;
	}
	if ( caps.maxImageExtent.height > 0 && extent.height > caps.maxImageExtent.height ) {
		extent.height = caps.maxImageExtent.height;
	}
	if ( extent.width == 0 || extent.height == 0 ) {
		// minimized window; keep the old swapchain until a real size shows up
		return false;
	}

	uint32_t imageCount = caps.minImageCount + 1;
	if ( caps.maxImageCount > 0 && imageCount > caps.maxImageCount ) {
		imageCount = caps.maxImageCount;
	}
	if ( imageCount > 8 ) {
		imageCount = 8;
	}

	VkSwapchainCreateInfoKHR sci;
	memset( &sci, 0, sizeof( sci ) );
	sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	sci.surface = vkCtx.surface;
	sci.minImageCount = imageCount;
	sci.imageFormat = chosen.format;
	sci.imageColorSpace = chosen.colorSpace;
	sci.imageExtent = extent;
	sci.imageArrayLayers = 1;
	sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	sci.preTransform = caps.currentTransform;
	sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	sci.presentMode = presentMode;
	sci.clipped = VK_TRUE;
	sci.oldSwapchain = vkCtx.swapchain;

	VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
	const VkResult res = vkCreateSwapchainKHR( vkCtx.device, &sci, NULL, &newSwapchain );
	if ( res != VK_SUCCESS ) {
		common->Warning( "Vulkan: vkCreateSwapchainKHR failed (%d)", (int)res );
		return false;
	}

	// the old swapchain (if any) is retired by the create; destroy our
	// per-image objects and the old handle
	VK_Device_DestroySwapchainObjects();
	vkCtx.swapchain = newSwapchain;
	vkCtx.swapchainFormat = chosen.format;
	vkCtx.swapchainExtent = extent;
	vkCtx.presentMode = presentMode;
	vkCtx.swapInterval = requestedInterval;

	uint32_t count = 0;
	vkGetSwapchainImagesKHR( vkCtx.device, vkCtx.swapchain, &count, NULL );
	if ( count > 8 ) {
		count = 8;
	}
	vkGetSwapchainImagesKHR( vkCtx.device, vkCtx.swapchain, &count, vkCtx.swapchainImages );
	vkCtx.swapchainImageCount = count;

	for ( uint32_t i = 0; i < count; i++ ) {
		VkImageViewCreateInfo ivci;
		memset( &ivci, 0, sizeof( ivci ) );
		ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		ivci.image = vkCtx.swapchainImages[ i ];
		ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ivci.format = vkCtx.swapchainFormat;
		ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ivci.subresourceRange.levelCount = 1;
		ivci.subresourceRange.layerCount = 1;
		if ( vkCreateImageView( vkCtx.device, &ivci, NULL, &vkCtx.swapchainViews[ i ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: swapchain image view creation failed" );
			return false;
		}

		VkSemaphoreCreateInfo semci;
		memset( &semci, 0, sizeof( semci ) );
		semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		if ( vkCreateSemaphore( vkCtx.device, &semci, NULL, &vkCtx.renderFinishedSemaphores[ i ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: render-finished semaphore creation failed" );
			return false;
		}
	}

	common->Printf( "Vulkan: created swapchain %ux%u format=%d images=%u presentMode=%d\n",
			extent.width, extent.height, (int)chosen.format, count, (int)presentMode );
	return true;
}

/*
====================
VK_Device_RecreateSwapchain
====================
*/
bool VK_Device_RecreateSwapchain( void ) {
	if ( !vkCtx.initialized ) {
		return false;
	}
	vkDeviceWaitIdle( vkCtx.device );
	return VK_Device_CreateSwapchain();
}

/*
====================
VK_Device_Init
====================
*/
bool VK_Device_Init( const renderWindowServices_s *windowServices ) {
	memset( &vkCtx, 0, sizeof( vkCtx ) );
	vkWindowServices = windowServices;

	if ( windowServices == NULL || windowServices->CreateVulkanSurface == NULL
			|| windowServices->GetVulkanInstanceExtensions == NULL ) {
		common->Warning( "Vulkan: window services carry no Vulkan surface support" );
		return false;
	}

	if ( volkInitialize() != VK_SUCCESS ) {
		common->Warning( "Vulkan: no Vulkan loader available (volkInitialize failed)" );
		return false;
	}

	// instance extensions: the window system's requirements plus debug utils
	// when validation is requested
	const char *extensions[ 24 ];
	int extensionCount = 0;
	if ( !windowServices->GetVulkanInstanceExtensions( extensions, 20, &extensionCount ) || extensionCount <= 0 ) {
		common->Warning( "Vulkan: window system reports no instance extensions" );
		return false;
	}
	if ( extensionCount > 20 ) {
		common->Warning( "Vulkan: instance extension list truncated (%d)", extensionCount );
		extensionCount = 20;
	}

	const bool wantValidation = r_vkValidation.GetBool();
	if ( wantValidation ) {
		extensions[ extensionCount++ ] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	}

	const char *validationLayer = "VK_LAYER_KHRONOS_validation";

	VkApplicationInfo appInfo;
	memset( &appInfo, 0, sizeof( appInfo ) );
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "openQ4";
	appInfo.pEngineName = "openQ4";
	appInfo.apiVersion = VK_API_VERSION_1_3;

	VkInstanceCreateInfo ici;
	memset( &ici, 0, sizeof( ici ) );
	ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	ici.pApplicationInfo = &appInfo;
	ici.enabledExtensionCount = (uint32_t)extensionCount;
	ici.ppEnabledExtensionNames = extensions;
	if ( wantValidation ) {
		ici.enabledLayerCount = 1;
		ici.ppEnabledLayerNames = &validationLayer;
	}

	VkResult res = vkCreateInstance( &ici, NULL, &vkCtx.instance );
	if ( res != VK_SUCCESS && wantValidation ) {
		common->Warning( "Vulkan: instance creation with validation failed (%d); retrying without", (int)res );
		ici.enabledLayerCount = 0;
		ici.ppEnabledLayerNames = NULL;
		res = vkCreateInstance( &ici, NULL, &vkCtx.instance );
	}
	if ( res != VK_SUCCESS ) {
		common->Warning( "Vulkan: vkCreateInstance failed (%d)", (int)res );
		return false;
	}
	volkLoadInstance( vkCtx.instance );

	if ( wantValidation && vkCreateDebugUtilsMessengerEXT != NULL ) {
		VkDebugUtilsMessengerCreateInfoEXT dmci;
		memset( &dmci, 0, sizeof( dmci ) );
		dmci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		dmci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		dmci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		dmci.pfnUserCallback = VK_DebugMessengerCallback;
		vkCreateDebugUtilsMessengerEXT( vkCtx.instance, &dmci, NULL, &vkCtx.debugMessenger );
	}

	// surface through the engine's window services
	unsigned long long surfaceHandle = 0;
	if ( !windowServices->CreateVulkanSurface( (void *)vkCtx.instance, &surfaceHandle ) || surfaceHandle == 0 ) {
		common->Warning( "Vulkan: surface creation through the window services failed" );
		VK_Device_Shutdown();
		return false;
	}
	vkCtx.surface = (VkSurfaceKHR)surfaceHandle;

	// physical device: honor r_vkDevice when set, else first suitable
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices( vkCtx.instance, &deviceCount, NULL );
	if ( deviceCount == 0 ) {
		common->Warning( "Vulkan: no physical devices" );
		VK_Device_Shutdown();
		return false;
	}
	if ( deviceCount > 16 ) {
		deviceCount = 16;
	}
	VkPhysicalDevice devices[ 16 ];
	vkEnumeratePhysicalDevices( vkCtx.instance, &deviceCount, devices );

	const int forcedDevice = r_vkDevice.GetInteger();
	int chosenDevice = -1;
	uint32_t chosenQueueFamily = 0;

	for ( uint32_t d = 0; d < deviceCount; d++ ) {
		if ( forcedDevice >= 0 && (int)d != forcedDevice ) {
			continue;
		}
		uint32_t familyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties( devices[ d ], &familyCount, NULL );
		if ( familyCount > 16 ) {
			familyCount = 16;
		}
		VkQueueFamilyProperties families[ 16 ];
		vkGetPhysicalDeviceQueueFamilyProperties( devices[ d ], &familyCount, families );
		for ( uint32_t f = 0; f < familyCount; f++ ) {
			if ( !( families[ f ].queueFlags & VK_QUEUE_GRAPHICS_BIT ) ) {
				continue;
			}
			VkBool32 presentable = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR( devices[ d ], f, vkCtx.surface, &presentable );
			if ( presentable ) {
				chosenDevice = (int)d;
				chosenQueueFamily = f;
				break;
			}
		}
		if ( chosenDevice >= 0 ) {
			break;
		}
	}
	if ( chosenDevice < 0 ) {
		common->Warning( "Vulkan: no graphics+present capable device%s",
				forcedDevice >= 0 ? " (r_vkDevice selection rejected)" : "" );
		VK_Device_Shutdown();
		return false;
	}
	vkCtx.physicalDevice = devices[ chosenDevice ];
	vkCtx.graphicsQueueFamily = chosenQueueFamily;
	vkGetPhysicalDeviceProperties( vkCtx.physicalDevice, &vkCtx.deviceProperties );
	common->Printf( "Vulkan: device %d '%s' (queue family %u)\n",
			chosenDevice, vkCtx.deviceProperties.deviceName, chosenQueueFamily );

	// logical device: swapchain + the VK 1.3 dynamic-rendering/sync2 floor
	const float queuePriority = 1.0f;
	VkDeviceQueueCreateInfo qci;
	memset( &qci, 0, sizeof( qci ) );
	qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	qci.queueFamilyIndex = vkCtx.graphicsQueueFamily;
	qci.queueCount = 1;
	qci.pQueuePriorities = &queuePriority;

	const char *deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	VkPhysicalDeviceVulkan13Features features13;
	memset( &features13, 0, sizeof( features13 ) );
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features13.dynamicRendering = VK_TRUE;
	features13.synchronization2 = VK_TRUE;

	VkDeviceCreateInfo dci;
	memset( &dci, 0, sizeof( dci ) );
	dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	dci.pNext = &features13;
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &qci;
	dci.enabledExtensionCount = 1;
	dci.ppEnabledExtensionNames = deviceExtensions;

	res = vkCreateDevice( vkCtx.physicalDevice, &dci, NULL, &vkCtx.device );
	if ( res != VK_SUCCESS ) {
		common->Warning( "Vulkan: vkCreateDevice failed (%d)", (int)res );
		VK_Device_Shutdown();
		return false;
	}
	volkLoadDevice( vkCtx.device );
	vkGetDeviceQueue( vkCtx.device, vkCtx.graphicsQueueFamily, 0, &vkCtx.graphicsQueue );

	// command pool + per-slot sync
	VkCommandPoolCreateInfo cpci;
	memset( &cpci, 0, sizeof( cpci ) );
	cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cpci.queueFamilyIndex = vkCtx.graphicsQueueFamily;
	if ( vkCreateCommandPool( vkCtx.device, &cpci, NULL, &vkCtx.commandPool ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: command pool creation failed" );
		VK_Device_Shutdown();
		return false;
	}

	VkCommandBufferAllocateInfo cbai;
	memset( &cbai, 0, sizeof( cbai ) );
	cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cbai.commandPool = vkCtx.commandPool;
	cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbai.commandBufferCount = VK_FRAMES_IN_FLIGHT;
	if ( vkAllocateCommandBuffers( vkCtx.device, &cbai, vkCtx.commandBuffers ) != VK_SUCCESS ) {
		common->Warning( "Vulkan: command buffer allocation failed" );
		VK_Device_Shutdown();
		return false;
	}

	for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
		VkSemaphoreCreateInfo semci;
		memset( &semci, 0, sizeof( semci ) );
		semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VkFenceCreateInfo fci;
		memset( &fci, 0, sizeof( fci ) );
		fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		if ( vkCreateSemaphore( vkCtx.device, &semci, NULL, &vkCtx.acquireSemaphores[ i ] ) != VK_SUCCESS
				|| vkCreateFence( vkCtx.device, &fci, NULL, &vkCtx.frameFences[ i ] ) != VK_SUCCESS ) {
			common->Warning( "Vulkan: frame sync object creation failed" );
			VK_Device_Shutdown();
			return false;
		}
	}

	if ( !VK_Device_CreateSwapchain() ) {
		VK_Device_Shutdown();
		return false;
	}

	vkCtx.initialized = true;
	return true;
}

/*
====================
VK_Device_Shutdown
====================
*/
void VK_Device_Shutdown( void ) {
	if ( vkCtx.device != VK_NULL_HANDLE ) {
		vkDeviceWaitIdle( vkCtx.device );
	}
	if ( vkCtx.device != VK_NULL_HANDLE ) {
		VK_Device_DestroySwapchainObjects();
		for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
			if ( vkCtx.acquireSemaphores[ i ] != VK_NULL_HANDLE ) {
				vkDestroySemaphore( vkCtx.device, vkCtx.acquireSemaphores[ i ], NULL );
			}
			if ( vkCtx.frameFences[ i ] != VK_NULL_HANDLE ) {
				vkDestroyFence( vkCtx.device, vkCtx.frameFences[ i ], NULL );
			}
		}
		if ( vkCtx.commandPool != VK_NULL_HANDLE ) {
			vkDestroyCommandPool( vkCtx.device, vkCtx.commandPool, NULL );
		}
		vkDestroyDevice( vkCtx.device, NULL );
	}
	if ( vkCtx.instance != VK_NULL_HANDLE ) {
		if ( vkCtx.surface != VK_NULL_HANDLE ) {
			vkDestroySurfaceKHR( vkCtx.instance, vkCtx.surface, NULL );
		}
		if ( vkCtx.debugMessenger != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT != NULL ) {
			vkDestroyDebugUtilsMessengerEXT( vkCtx.instance, vkCtx.debugMessenger, NULL );
		}
		vkDestroyInstance( vkCtx.instance, NULL );
	}
	memset( &vkCtx, 0, sizeof( vkCtx ) );
}

/*
====================
VK_Device_PresentClearFrame
====================
*/
void VK_Device_PresentClearFrame( const float clearColor[ 4 ] ) {
	if ( !vkCtx.initialized ) {
		return;
	}

	// swap-interval changes require a swapchain rebuild
	if ( r_swapInterval.GetInteger() != vkCtx.swapInterval ) {
		if ( !VK_Device_RecreateSwapchain() ) {
			return;
		}
	}

	const int slot = vkCtx.frameSlot;
	vkCtx.frameSlot = ( vkCtx.frameSlot + 1 ) % VK_FRAMES_IN_FLIGHT;

	vkWaitForFences( vkCtx.device, 1, &vkCtx.frameFences[ slot ], VK_TRUE, UINT64_MAX );

	uint32_t imageIndex = 0;
	VkResult res = vkAcquireNextImageKHR( vkCtx.device, vkCtx.swapchain, UINT64_MAX,
			vkCtx.acquireSemaphores[ slot ], VK_NULL_HANDLE, &imageIndex );
	if ( res == VK_ERROR_OUT_OF_DATE_KHR ) {
		if ( !VK_Device_RecreateSwapchain() ) {
			return;
		}
		res = vkAcquireNextImageKHR( vkCtx.device, vkCtx.swapchain, UINT64_MAX,
				vkCtx.acquireSemaphores[ slot ], VK_NULL_HANDLE, &imageIndex );
	}
	if ( res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR ) {
		return;
	}

	vkResetFences( vkCtx.device, 1, &vkCtx.frameFences[ slot ] );

	VkCommandBuffer cmd = vkCtx.commandBuffers[ slot ];
	vkResetCommandBuffer( cmd, 0 );

	VkCommandBufferBeginInfo cbbi;
	memset( &cbbi, 0, sizeof( cbbi ) );
	cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer( cmd, &cbbi );

	// UNDEFINED -> COLOR_ATTACHMENT
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

	// dynamic-rendering clear pass
	VkRenderingAttachmentInfo color;
	memset( &color, 0, sizeof( color ) );
	color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color.imageView = vkCtx.swapchainViews[ imageIndex ];
	color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color.clearValue.color.float32[ 0 ] = clearColor[ 0 ];
	color.clearValue.color.float32[ 1 ] = clearColor[ 1 ];
	color.clearValue.color.float32[ 2 ] = clearColor[ 2 ];
	color.clearValue.color.float32[ 3 ] = clearColor[ 3 ];

	VkRenderingInfo ri;
	memset( &ri, 0, sizeof( ri ) );
	ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	ri.renderArea.extent = vkCtx.swapchainExtent;
	ri.layerCount = 1;
	ri.colorAttachmentCount = 1;
	ri.pColorAttachments = &color;

	vkCmdBeginRendering( cmd, &ri );
	vkCmdEndRendering( cmd );

	// COLOR_ATTACHMENT -> PRESENT
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

	res = vkQueuePresentKHR( vkCtx.graphicsQueue, &pi );
	if ( res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR ) {
		VK_Device_RecreateSwapchain();
	}
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
