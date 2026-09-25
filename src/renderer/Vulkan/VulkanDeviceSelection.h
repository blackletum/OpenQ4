// Copyright (C) 2026 DarkMatter Productions
#ifndef __VULKAN_DEVICE_SELECTION_H__
#define __VULKAN_DEVICE_SELECTION_H__

#include <vulkan/vulkan.h>

// The shadowed interaction layout uses six images, one UBO and one shadow set.
static const int VK_REQUIRED_BOUND_DESCRIPTOR_SETS = 8;

// Instance-level queries only. Selection neither creates a logical device nor
// changes global renderer state; callers publish the result after success.
struct vkDeviceSelectionApi_t {
	PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices;
	PFN_vkGetPhysicalDeviceProperties getProperties;
	PFN_vkGetPhysicalDeviceFeatures2 getFeatures2;
	PFN_vkEnumerateDeviceExtensionProperties enumerateExtensions;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties getQueueFamilies;
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR getSurfaceSupport;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR getSurfaceCapabilities;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR getSurfaceFormats;
	PFN_vkGetPhysicalDeviceFormatProperties getFormatProperties;
};

struct vkDeviceSelection_t {
	VkPhysicalDevice device;
	VkPhysicalDeviceProperties properties;
	uint32_t index;
	uint32_t queueFamily;
	uint32_t timestampValidBits;
};

typedef void (*vkDeviceRejection_t)( uint32_t index, const VkPhysicalDeviceProperties &properties,
		const char *reason, VkResult result, void *user );

VkResult VK_SelectPhysicalDevice( const vkDeviceSelectionApi_t &api, VkInstance instance,
		VkSurfaceKHR surface, int forcedIndex, vkDeviceSelection_t &selected,
		vkDeviceRejection_t report = nullptr, void *user = nullptr );

// Shared by admission and swapchain creation, including the legacy undefined
// format case. Never silently picks an sRGB attachment for display-coded output.
VkResult VK_SelectSurfaceFormat( PFN_vkGetPhysicalDeviceSurfaceFormatsKHR query,
		VkPhysicalDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR &selected );

#endif
