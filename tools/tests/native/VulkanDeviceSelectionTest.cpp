// Copyright (C) 2026 DarkMatter Productions
// Fake Vulkan queries drive the production selector; no loader or GPU required.
#include "../../../src/renderer/Vulkan/VulkanDeviceSelection.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int passed = 0;
const char *caseName = "setup";
void Require( bool condition, const char *message ) {
	if ( !condition ) {
		std::fprintf( stderr, "VulkanDeviceSelectionTest [%s]: %s\n", caseName, message );
		std::exit( 1 );
	}
}

struct Enumeration {
	VkResult countResult = VK_SUCCESS;
	VkResult dataResult = VK_SUCCESS;
	uint32_t firstCountLimit = UINT32_MAX;
	int incomplete = 0;
	int countCalls = 0;
	int dataCalls = 0;
};

template<class T>
VkResult Enumerate( const std::vector<T> &source, Enumeration &state, uint32_t *count, T *data ) {
	if ( data == nullptr ) {
		++state.countCalls;
		if ( state.countResult != VK_SUCCESS ) { return state.countResult; }
		*count = static_cast<uint32_t>( source.size() );
		if ( state.countCalls == 1 ) { *count = std::min( *count, state.firstCountLimit ); }
		return VK_SUCCESS;
	}
	++state.dataCalls;
	if ( state.dataResult != VK_SUCCESS ) { return state.dataResult; }
	const uint32_t written = std::min( *count, static_cast<uint32_t>( source.size() ) );
	std::copy_n( source.begin(), written, data );
	*count = written;
	if ( state.incomplete > 0 ) { --state.incomplete; return VK_INCOMPLETE; }
	return written == source.size() ? VK_SUCCESS : VK_INCOMPLETE;
}

struct Queue {
	VkQueueFamilyProperties properties = {};
	VkBool32 present = VK_TRUE;
	VkResult result = VK_SUCCESS;
	Queue() { properties.queueFlags = VK_QUEUE_GRAPHICS_BIT; properties.queueCount = 1; properties.timestampValidBits = 48; }
};

struct Device {
	VkPhysicalDeviceProperties properties = {};
	VkBool32 dynamicRendering = VK_TRUE;
	VkBool32 synchronization2 = VK_TRUE;
	VkPhysicalDeviceFeatures baseFeatures = {};
	VkPhysicalDeviceVulkan12Features features12 = {};
	VkDeviceSize localBytes = 8ull * 1024 * 1024 * 1024;
	std::vector<VkExtensionProperties> extensions;
	std::vector<Queue> queues = { Queue() };
	VkResult capsResult = VK_SUCCESS;
	VkImageUsageFlags surfaceUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	std::vector<VkSurfaceFormatKHR> formats = { { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } };
	VkFormatFeatureFlags d24 = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	VkFormatFeatureFlags d32 = 0;
	Enumeration extensionQuery, formatQuery;
	int propertiesCalls = 0, featureCalls = 0, presentCalls = 0;
	Device() {
		properties.apiVersion = VK_API_VERSION_1_3;
		properties.limits.maxBoundDescriptorSets = 8;
		properties.limits.timestampPeriod = 2.0f;
		std::strcpy( properties.deviceName, "fake GPU" );
		VkExtensionProperties swapchain = {};
		std::strcpy( swapchain.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME );
		extensions.push_back( swapchain );
	}
};

std::vector<Device> devices;
Enumeration deviceQuery;
std::vector<uint32_t> rejected;
std::vector<VkResult> rejectionResults;
std::vector<vkDeviceProbeInfo_t> probeReports;
std::vector<VkResult> probeQueryResults;

VkPhysicalDevice Handle( uint32_t index ) { return reinterpret_cast<VkPhysicalDevice>( static_cast<uintptr_t>( index + 1 ) ); }
Device &Get( VkPhysicalDevice handle ) {
	const uintptr_t index = reinterpret_cast<uintptr_t>( handle ) - 1;
	Require( index < devices.size(), "query of invalid/uninitialized physical device" );
	return devices[index];
}

VKAPI_ATTR VkResult VKAPI_CALL PhysicalDevices( VkInstance, uint32_t *count, VkPhysicalDevice *data ) {
	std::vector<VkPhysicalDevice> handles;
	for ( uint32_t i = 0; i < devices.size(); ++i ) { handles.push_back( Handle( i ) ); }
	return Enumerate( handles, deviceQuery, count, data );
}
VKAPI_ATTR void VKAPI_CALL Properties( VkPhysicalDevice handle, VkPhysicalDeviceProperties *properties ) {
	auto &device = Get( handle );
	++device.propertiesCalls;
	*properties = device.properties;
}
VKAPI_ATTR void VKAPI_CALL Features( VkPhysicalDevice handle, VkPhysicalDeviceFeatures2 *features ) {
	auto &device = Get( handle );
	++device.featureCalls;
	Require( features->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, "invalid features query" );
	bool queried13 = false;
	int nodes = 0;
	for ( auto *node = static_cast<VkBaseOutStructure *>( features->pNext ); node != nullptr; node = node->pNext ) {
		Require( ++nodes <= 2, "invalid feature query chain" );
		if ( node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES ) {
			Require( device.properties.apiVersion >= VK_API_VERSION_1_3, "1.3 features queried on an older device" );
			auto *features13 = reinterpret_cast<VkPhysicalDeviceVulkan13Features *>( node );
			features13->dynamicRendering = device.dynamicRendering;
			features13->synchronization2 = device.synchronization2;
			queried13 = true;
		} else if ( node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES ) {
			Require( device.properties.apiVersion >= VK_API_VERSION_1_2, "1.2 features queried on an older device" );
			auto *features12 = reinterpret_cast<VkPhysicalDeviceVulkan12Features *>( node );
			const auto type = features12->sType;
			void *next = features12->pNext;
			*features12 = device.features12;
			features12->sType = type;
			features12->pNext = next;
		} else { Require( false, "unexpected feature query structure" ); }
	}
	Require( queried13 == ( device.properties.apiVersion >= VK_API_VERSION_1_3 ), "missing 1.3 feature query" );
	features->features = device.baseFeatures;
}
VKAPI_ATTR VkResult VKAPI_CALL Extensions( VkPhysicalDevice handle, const char *, uint32_t *count, VkExtensionProperties *data ) {
	auto &device = Get( handle );
	return Enumerate( device.extensions, device.extensionQuery, count, data );
}
VKAPI_ATTR void VKAPI_CALL Queues( VkPhysicalDevice handle, uint32_t *count, VkQueueFamilyProperties *data ) {
	auto &queues = Get( handle ).queues;
	if ( data == nullptr ) { *count = static_cast<uint32_t>( queues.size() ); return; }
	*count = std::min( *count, static_cast<uint32_t>( queues.size() ) );
	for ( uint32_t i = 0; i < *count; ++i ) { data[i] = queues[i].properties; }
}
VKAPI_ATTR VkResult VKAPI_CALL SurfaceSupport( VkPhysicalDevice handle, uint32_t family, VkSurfaceKHR, VkBool32 *present ) {
	auto &device = Get( handle );
	++device.presentCalls;
	Require( family < device.queues.size(), "invalid queue family queried" );
	*present = device.queues[family].present;
	return device.queues[family].result;
}
VKAPI_ATTR VkResult VKAPI_CALL Capabilities( VkPhysicalDevice handle, VkSurfaceKHR, VkSurfaceCapabilitiesKHR *caps ) {
	auto &device = Get( handle );
	caps->supportedUsageFlags = device.surfaceUsage;
	return device.capsResult;
}
VKAPI_ATTR VkResult VKAPI_CALL Formats( VkPhysicalDevice handle, VkSurfaceKHR, uint32_t *count, VkSurfaceFormatKHR *data ) {
	auto &device = Get( handle );
	return Enumerate( device.formats, device.formatQuery, count, data );
}
VKAPI_ATTR void VKAPI_CALL FormatProperties( VkPhysicalDevice handle, VkFormat format, VkFormatProperties *properties ) {
	auto &device = Get( handle );
	*properties = {};
	if ( format == VK_FORMAT_D24_UNORM_S8_UINT ) { properties->optimalTilingFeatures = device.d24; }
	if ( format == VK_FORMAT_D32_SFLOAT_S8_UINT ) { properties->optimalTilingFeatures = device.d32; }
}
VKAPI_ATTR void VKAPI_CALL MemoryProperties( VkPhysicalDevice handle, VkPhysicalDeviceMemoryProperties *properties ) {
	*properties = {};
	properties->memoryHeapCount = 1;
	properties->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
	properties->memoryHeaps[0].size = Get( handle ).localBytes;
}
void Report( uint32_t index, const VkPhysicalDeviceProperties &properties, const char *reason, VkResult result, void * ) {
	Require( reason && *reason, "missing rejection diagnostic" );
	Require( properties.deviceName[0] != '\0', "missing rejected device identity" );
	Require( result != VK_SUCCESS, "success reported as rejection" );
	rejected.push_back( index );
	rejectionResults.push_back( result );
}

const vkDeviceSelectionApi_t api = { PhysicalDevices, Properties, Features, Extensions,
	Queues, SurfaceSupport, Capabilities, Formats, FormatProperties, MemoryProperties };

void Reset( const char *name, size_t count = 2 ) {
	caseName = name;
	devices.assign( count, Device() );
	deviceQuery = {};
	rejected.clear();
	rejectionResults.clear();
	probeReports.clear();
	probeQueryResults.clear();
}
vkDeviceSelection_t Select( int expected, int forced = -1, VkResult expectedFailure = VK_ERROR_FEATURE_NOT_PRESENT ) {
	vkDeviceSelection_t selected = {};
	selected.device = Handle( 10 );
	selected.index = 10;
	selected.queueFamily = 10;
	selected.properties.apiVersion = VK_API_VERSION_1_3;
	const VkResult result = VK_SelectPhysicalDevice( api, VK_NULL_HANDLE, VK_NULL_HANDLE, forced, selected, Report );
	if ( expected < 0 ) {
		Require( result == expectedFailure, "unexpected selection error" );
		Require( selected.device == VK_NULL_HANDLE && selected.index == 0 && selected.queueFamily == 0
			&& selected.properties.apiVersion == 0, "failed selection left stale device data" );
	} else {
		Require( result == VK_SUCCESS, "selection failed" );
		Require( selected.device == Handle( expected ) && selected.index == static_cast<uint32_t>( expected ), "selected wrong device" );
		Require( selected.timestampValidBits == 48 && selected.properties.limits.timestampPeriod == 2.0f, "lost timing properties" );
	}
	++passed;
	return selected;
}
void SkipFirst( VkResult error ) {
	Select( 1 );
	Require( rejected == std::vector<uint32_t>{ 0 } && rejectionResults == std::vector<VkResult>{ error }, "incorrect skipped-device report" );
}
void Surface( VkFormat expected, VkResult expectedResult = VK_SUCCESS ) {
	VkSurfaceFormatKHR selected = { VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT };
	const VkResult result = VK_SelectSurfaceFormat( Formats, Handle( 0 ), VK_NULL_HANDLE, selected );
	Require( result == expectedResult && selected.format == expected, "incorrect surface selection/result" );
	Require( selected.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, "incorrect or stale color space" );
	++passed;
}

void ProbeReport( const vkDeviceProbeInfo_t &info, VkResult result, void * ) {
	Require( info.physicalDevice == Handle( info.index ), "probe reported wrong device identity" );
	probeReports.push_back( info );
	probeQueryResults.push_back( result );
}

vkDeviceProbeInfo_t Probe( int expected, bool verbose = false, int forced = -1,
		VkResult expectedFailure = VK_ERROR_FEATURE_NOT_PRESENT ) {
	vkDeviceProbeInfo_t selected = {};
	selected.physicalDevice = Handle( 10 );
	selected.index = 10;
	selected.meetsRequirements = true;
	const VkResult result = VK_SelectProbeDevice( api, VK_NULL_HANDLE, forced, verbose, selected, ProbeReport );
	if ( expected < 0 ) {
		Require( result == expectedFailure, "unexpected probe selection error" );
		Require( selected.physicalDevice == VK_NULL_HANDLE && selected.index == 0
				&& !selected.meetsRequirements, "failed probe retained stale device" );
	} else {
		Require( result == VK_SUCCESS && selected.physicalDevice == Handle( expected )
				&& selected.index == static_cast<uint32_t>( expected ) && selected.meetsRequirements, "probe selected wrong device" );
	}
	for ( const auto &device : devices ) {
		Require( device.presentCalls == 0 && device.formatQuery.countCalls == 0, "no-surface probe queried presentation" );
	}
	++passed;
	return selected;
}

} // namespace

int main() {
	Reset( "first suitable; no optional features required" ); Select( 0 );
	Require( devices[1].propertiesCalls == 0, "automatic selection queried later devices unnecessarily" );
	Reset( "older API before suitable GPU" ); devices[0].properties.apiVersion = VK_API_VERSION_1_2;
	SkipFirst( VK_ERROR_INCOMPATIBLE_DRIVER ); Require( devices[0].featureCalls == 0, "queried unsupported feature structure" );
	Reset( "descriptor ceiling" ); devices[0].properties.limits.maxBoundDescriptorSets = 7; SkipFirst( VK_ERROR_FEATURE_NOT_PRESENT );
	Reset( "dynamic rendering" ); devices[0].dynamicRendering = VK_FALSE; SkipFirst( VK_ERROR_FEATURE_NOT_PRESENT );
	Reset( "synchronization2" ); devices[0].synchronization2 = VK_FALSE; SkipFirst( VK_ERROR_FEATURE_NOT_PRESENT );
	Reset( "missing swapchain" ); devices[0].extensions.clear(); SkipFirst( VK_ERROR_EXTENSION_NOT_PRESENT );
	Reset( "no queues" ); devices[0].queues.clear(); SkipFirst( VK_ERROR_FEATURE_NOT_PRESENT );
	Reset( "zero queue count" ); devices[0].queues[0].properties.queueCount = 0; SkipFirst( VK_ERROR_FEATURE_NOT_PRESENT );
	Require( devices[0].presentCalls == 0, "queried an unusable queue" );
	Reset( "compute-only queue" ); devices[0].queues[0].properties.queueFlags = VK_QUEUE_COMPUTE_BIT; SkipFirst( VK_ERROR_FEATURE_NOT_PRESENT );
	Reset( "presentation unavailable" ); devices[0].queues[0].present = VK_FALSE; SkipFirst( VK_ERROR_FEATURE_NOT_PRESENT );
	Reset( "failed present query must not trust output" ); devices[0].queues[0].result = VK_ERROR_SURFACE_LOST_KHR; SkipFirst( VK_ERROR_SURFACE_LOST_KHR );
	Reset( "later usable queue" ); devices[0].queues.push_back( Queue() ); devices[0].queues[0].result = VK_ERROR_SURFACE_LOST_KHR;
	Require( Select( 0 ).queueFamily == 1, "did not try later queue family" );
	Reset( "queue beyond old fixed array" ); devices[0].queues.assign( 33, Queue() );
	for ( size_t i = 0; i < 32; ++i ) { devices[0].queues[i].present = VK_FALSE; }
	Require( Select( 0 ).queueFamily == 32, "queue list truncated" );
	Reset( "surface capability query failure" ); devices[0].capsResult = VK_ERROR_SURFACE_LOST_KHR; SkipFirst( VK_ERROR_SURFACE_LOST_KHR );
	Reset( "no color attachment surface" ); devices[0].surfaceUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT; SkipFirst( VK_ERROR_FORMAT_NOT_SUPPORTED );
	Reset( "sRGB-only surface" ); devices[0].formats[0].format = VK_FORMAT_B8G8R8A8_SRGB; SkipFirst( VK_ERROR_FORMAT_NOT_SUPPORTED );
	Reset( "wrong surface color space" ); devices[0].formats[0].colorSpace = VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT; SkipFirst( VK_ERROR_FORMAT_NOT_SUPPORTED );
	Reset( "no depth/stencil format" ); devices[0].d24 = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT; SkipFirst( VK_ERROR_FORMAT_NOT_SUPPORTED );
	Reset( "D32 depth/stencil fallback" ); devices[0].d24 = 0; devices[0].d32 = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT; Select( 0 );
	Reset( "forced suitable" ); Select( 1, 1 ); Require( devices[0].propertiesCalls == 0, "forced choice queried another device" );
	Reset( "forced unsuitable must not substitute" ); devices[0].dynamicRendering = VK_FALSE; Select( -1, 0 );
	Require( devices[1].propertiesCalls == 0 && rejected == std::vector<uint32_t>{ 0 }, "substituted forced GPU" );
	Reset( "forced nonexistent" ); Select( -1, 2, VK_ERROR_INITIALIZATION_FAILED );
	Require( devices[0].propertiesCalls == 0 && rejected.empty(), "queried candidate for invalid index" );
	Reset( "no suitable device" ); for ( auto &d : devices ) { d.synchronization2 = VK_FALSE; } Select( -1 );
	Require( rejected == std::vector<uint32_t>{ 0, 1 }, "did not report all unsuitable devices" );
	Reset( "no physical devices", 0 ); Select( -1 );
	Reset( "device beyond old fixed array", 18 ); for ( size_t i = 0; i < 17; ++i ) { devices[i].dynamicRendering = VK_FALSE; } Select( 17 );
	Reset( "device count grows" ); deviceQuery.firstCountLimit = 1; devices[0].dynamicRendering = VK_FALSE; Select( 1 );
	Require( deviceQuery.countCalls == 2 && deviceQuery.dataCalls == 2, "did not retry growing list" );
	Reset( "physical count error" ); deviceQuery.countResult = VK_ERROR_OUT_OF_HOST_MEMORY; Select( -1, -1, VK_ERROR_OUT_OF_HOST_MEMORY );
	Reset( "physical data error" ); deviceQuery.dataResult = VK_ERROR_INITIALIZATION_FAILED; Select( -1, -1, VK_ERROR_INITIALIZATION_FAILED );
	Require( devices[0].propertiesCalls == 0, "consumed failed enumeration" );
	Reset( "persistent incomplete list" ); deviceQuery.incomplete = 20; Select( -1, -1, VK_INCOMPLETE );
	Require( deviceQuery.dataCalls == 4 && devices[0].propertiesCalls == 0, "unbounded retry or partial list admitted" );
	Reset( "extension count error" ); devices[0].extensionQuery.countResult = VK_ERROR_OUT_OF_HOST_MEMORY; SkipFirst( VK_ERROR_OUT_OF_HOST_MEMORY );
	Reset( "extension data error" ); devices[0].extensionQuery.dataResult = VK_ERROR_INITIALIZATION_FAILED; SkipFirst( VK_ERROR_INITIALIZATION_FAILED );
	Reset( "extension list incomplete" ); devices[0].extensionQuery.incomplete = 20; SkipFirst( VK_INCOMPLETE );
	Reset( "extension enumeration recovers" ); devices[0].extensionQuery.incomplete = 1; Select( 0 );
	Require( devices[0].extensionQuery.countCalls == 2, "extension list was not re-enumerated" );
	Reset( "format count error" ); devices[0].formatQuery.countResult = VK_ERROR_SURFACE_LOST_KHR; SkipFirst( VK_ERROR_SURFACE_LOST_KHR );
	Reset( "format data error" ); devices[0].formatQuery.dataResult = VK_ERROR_SURFACE_LOST_KHR; SkipFirst( VK_ERROR_SURFACE_LOST_KHR );
	Reset( "format list incomplete" ); devices[0].formatQuery.incomplete = 20; SkipFirst( VK_INCOMPLETE );
	Reset( "empty surface formats" ); devices[0].formats.clear(); SkipFirst( VK_ERROR_FORMAT_NOT_SUPPORTED );
	Reset( "format beyond old fixed array" ); devices[0].formats.assign( 65, { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } );
	devices[0].formats.push_back( { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } );
	Select( 0 ); Surface( VK_FORMAT_R8G8B8A8_UNORM );
	Reset( "undefined legacy format" ); devices[0].formats[0].format = VK_FORMAT_UNDEFINED; Surface( VK_FORMAT_B8G8R8A8_UNORM );
	Reset( "concrete format preferred over undefined" ); devices[0].formats[0].format = VK_FORMAT_UNDEFINED;
	devices[0].formats.push_back( { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } ); Surface( VK_FORMAT_R8G8B8A8_UNORM );
	Reset( "failed standalone surface selection clears output" ); devices[0].formats.clear(); Surface( VK_FORMAT_UNDEFINED, VK_ERROR_FORMAT_NOT_SUPPORTED );
	Reset( "surface count grows" ); devices[0].formats[0].format = VK_FORMAT_R8G8B8A8_SRGB;
	devices[0].formats.push_back( { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } );
	devices[0].formatQuery.firstCountLimit = 1; Surface( VK_FORMAT_R8G8B8A8_UNORM );
	Require( devices[0].formatQuery.countCalls == 2, "surface formats did not retry changing count" );
	const int rendererCases = passed;
	Reset( "quiet probe stops at first suitable" ); Probe( 0 );
	Require( devices[1].propertiesCalls == 0 && probeReports.size() == 1, "quiet probe queried later GPU" );
	Reset( "verbose probe scores suitable candidates" );
	devices[0].properties.deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
	devices[1].properties.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
	Probe( 1, true ); Require( probeReports.size() == 2, "verbose inventory incomplete" );
	Reset( "unsuitable high-memory GPU cannot win score" );
	devices[0].properties.limits.maxBoundDescriptorSets = 7; devices[0].localBytes = UINT64_MAX;
	Probe( 1, true );
	Reset( "probe version 1.2 before suitable" ); devices[0].properties.apiVersion = VK_API_VERSION_1_2;
	devices[0].features12.timelineSemaphore = VK_TRUE; Probe( 1 );
	Require( devices[0].featureCalls == 1 && probeReports[0].hasTimelineSemaphore
			&& !probeReports[0].hasDynamicRendering, "older core feature query/report is wrong" );
	Reset( "probe version 1.1 before suitable" ); devices[0].properties.apiVersion = VK_API_VERSION_1_1; Probe( 1 );
	Require( devices[0].featureCalls == 0, "queried 1.2 structures on 1.1 device" );
	Reset( "probe dynamic rendering required" ); devices[0].dynamicRendering = VK_FALSE; Probe( 1 );
	Reset( "probe synchronization2 required" ); devices[0].synchronization2 = VK_FALSE; Probe( 1 );
	Reset( "probe swapchain required" ); devices[0].extensions.clear(); Probe( 1 );
	Reset( "probe zero-count graphics queue" ); devices[0].queues[0].properties.queueCount = 0; Probe( 1 );
	Reset( "probe compute-only queue" ); devices[0].queues[0].properties.queueFlags = VK_QUEUE_COMPUTE_BIT; Probe( 1 );
	Reset( "probe no queues" ); devices[0].queues.clear(); Probe( 1 );
	Reset( "probe graphics queue beyond old bound" ); devices[0].queues.assign( 40, Queue() );
	for ( size_t i = 0; i < 39; ++i ) { devices[0].queues[i].properties.queueCount = 0; }
	Require( Probe( 0 ).graphicsQueueFamily == 39, "probe queue list truncated" );
	Reset( "probe dedicated transfer queue" ); devices[0].queues.push_back( Queue() );
	devices[0].queues[1].properties.queueFlags = VK_QUEUE_TRANSFER_BIT;
	const auto transfer = Probe( 0 );
	Require( transfer.hasDedicatedTransferQueue && transfer.transferQueueFamily == 1, "dedicated transfer lost" );
	Reset( "probe empty transfer queue is ignored" ); devices[0].queues.push_back( Queue() );
	devices[0].queues[1].properties.queueFlags = VK_QUEUE_TRANSFER_BIT; devices[0].queues[1].properties.queueCount = 0;
	const auto shared = Probe( 0 ); Require( !shared.hasDedicatedTransferQueue && shared.transferQueueFamily == 0, "empty transfer selected" );
	Reset( "probe forced choice, quiet" ); Probe( 1, false, 1 );
	Require( devices[0].propertiesCalls == 0, "forced quiet probe queried other GPU" );
	Reset( "probe forced choice, verbose" ); Probe( 1, true, 1 );
	Require( devices[0].propertiesCalls == 0, "forced verbose probe queried other GPU" );
	Reset( "probe forced unsuitable" ); devices[0].dynamicRendering = VK_FALSE; Probe( -1, true, 0 );
	Require( devices[1].propertiesCalls == 0, "forced probe silently substituted GPU" );
	Reset( "probe forced out of range" ); Probe( -1, true, 2, VK_ERROR_INITIALIZATION_FAILED );
	Require( probeReports.empty() && devices[0].propertiesCalls == 0, "out-of-range probe substituted GPU" );
	Reset( "probe beyond old physical-device cap", 18 );
	for ( size_t i = 0; i < 17; ++i ) { devices[i].dynamicRendering = VK_FALSE; } Probe( 17 );
	Reset( "probe physical count failure" ); deviceQuery.countResult = VK_ERROR_OUT_OF_HOST_MEMORY;
	Probe( -1, false, -1, VK_ERROR_OUT_OF_HOST_MEMORY );
	Reset( "probe physical data failure" ); deviceQuery.dataResult = VK_ERROR_INITIALIZATION_FAILED;
	Probe( -1, false, -1, VK_ERROR_INITIALIZATION_FAILED ); Require( probeReports.empty(), "probe consumed failed device enumeration" );
	Reset( "probe physical list grows" ); deviceQuery.firstCountLimit = 1; devices[0].dynamicRendering = VK_FALSE; Probe( 1 );
	Require( deviceQuery.countCalls == 2, "probe did not retry changing list" );
	Reset( "probe physical enumeration incomplete" ); deviceQuery.incomplete = 20; Probe( -1, false, -1, VK_INCOMPLETE );
	Require( deviceQuery.dataCalls == 4 && probeReports.empty(), "probe accepted partial list or retried indefinitely" );
	Reset( "probe extension count failure" ); devices[0].extensionQuery.countResult = VK_ERROR_OUT_OF_HOST_MEMORY; Probe( 1 );
	Require( probeQueryResults[0] == VK_ERROR_OUT_OF_HOST_MEMORY, "lost extension error" );
	Reset( "probe extension data failure" ); devices[0].extensionQuery.dataResult = VK_ERROR_INITIALIZATION_FAILED; Probe( 1 );
	Require( probeQueryResults[0] == VK_ERROR_INITIALIZATION_FAILED, "lost extension data error" );
	Reset( "probe extension list remains incomplete" ); devices[0].extensionQuery.incomplete = 20; Probe( 1 );
	Require( probeQueryResults[0] == VK_INCOMPLETE && !probeReports[0].hasSwapchain, "admitted partial extension list" );
	Reset( "probe extensions beyond old cap and retry" );
	devices[0].extensions.insert( devices[0].extensions.begin(), 700, VkExtensionProperties{} );
	VkExtensionProperties portability = {}; std::strcpy( portability.extensionName, "VK_KHR_portability_subset" );
	devices[0].extensions.push_back( portability ); devices[0].extensionQuery.firstCountLimit = 1;
	const auto portable = Probe( 0 );
	Require( portable.hasPortabilitySubset && portable.hasSwapchain && devices[0].extensionQuery.countCalls == 2, "truncated extension support" );
	Reset( "probe reports optional features" );
	devices[0].features12.timelineSemaphore = VK_TRUE;
	devices[0].features12.descriptorIndexing = VK_TRUE;
	devices[0].features12.runtimeDescriptorArray = VK_TRUE;
	devices[0].features12.descriptorBindingPartiallyBound = VK_TRUE;
	devices[0].features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
	devices[0].features12.bufferDeviceAddress = VK_TRUE;
	devices[0].baseFeatures.samplerAnisotropy = VK_TRUE;
	devices[0].baseFeatures.textureCompressionBC = VK_TRUE;
	devices[0].baseFeatures.depthBounds = VK_TRUE;
	const auto optional = Probe( 0 );
	Require( optional.hasTimelineSemaphore && optional.hasDescriptorIndexing && optional.hasBufferDeviceAddress
			&& optional.hasSamplerAnisotropy && optional.hasTextureCompressionBC && optional.hasDepthBounds, "optional diagnostics lost" );
	Reset( "partial descriptor indexing is optional" ); devices[0].features12.descriptorIndexing = VK_TRUE;
	Require( !Probe( 0 ).hasDescriptorIndexing, "partial descriptor indexing reported as complete" );
	Reset( "no suitable probe candidate" ); for ( auto &device : devices ) { device.dynamicRendering = VK_FALSE; } Probe( -1, true );
	Reset( "empty probe device list", 0 ); Probe( -1 );
	std::printf( "Vulkan device selection: %d renderer cases and %d probe cases passed\n", rendererCases, passed - rendererCases );
	return 0;
}
