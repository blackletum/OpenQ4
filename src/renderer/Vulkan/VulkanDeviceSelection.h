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
	// Used only by the no-surface capability probe.
	PFN_vkGetPhysicalDeviceMemoryProperties getMemoryProperties = nullptr;
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

// Capability inventory for the startup gate and verbose diagnostic probe.
// Optional features are reported, never added to the renderer's admission floor.
struct vkDeviceProbeInfo_t {
	VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceProperties props;
	uint32_t index;
	uint32_t graphicsQueueFamily;
	uint32_t transferQueueFamily;
	VkDeviceSize deviceLocalBytes;
	bool hasGraphicsQueue;
	bool hasDedicatedTransferQueue;
	bool hasSwapchain;
	bool hasPortabilitySubset;
	bool hasDynamicRendering;
	bool hasSynchronization2;
	bool hasTimelineSemaphore;
	bool hasDescriptorIndexing;
	bool hasBufferDeviceAddress;
	bool hasSamplerAnisotropy;
	bool hasTextureCompressionBC;
	bool hasDepthBounds;
	bool meetsRequirements;
	uint64_t score;
};

typedef void (*vkDeviceProbeReport_t)( const vkDeviceProbeInfo_t &info, VkResult queryResult, void *user );

// Quiet activation stops at the first suitable device. Verbose diagnostics
// inspect all candidates and score only suitable ones. Forced indices are
// queried alone in either mode; failure clears selected and never substitutes.
VkResult VK_SelectProbeDevice( const vkDeviceSelectionApi_t &api, VkInstance instance,
		int forcedIndex, bool verbose, vkDeviceProbeInfo_t &selected,
		vkDeviceProbeReport_t report = nullptr, void *user = nullptr );

#endif
