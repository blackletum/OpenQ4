#include "NvrhiBootstrap.h"
#include "../../sys/GraphicsWindow.h"

#if defined( OPENQ4_NVRHI_HAS_VULKAN )

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <nvrhi/utils.h>
#include <nvrhi/vulkan.h>

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "NvrhiError.h"

namespace {

class vulkanBootstrapMessageCallback_t final : public nvrhi::IMessageCallback {
public:
	virtual void message( nvrhi::MessageSeverity severity, const char *messageText ) override {
		const char *prefix = "INFO";
		switch ( severity ) {
			case nvrhi::MessageSeverity::Info:
				prefix = "INFO";
				break;
			case nvrhi::MessageSeverity::Warning:
				prefix = "WARN";
				break;
			case nvrhi::MessageSeverity::Error:
				prefix = "ERR";
				break;
			case nvrhi::MessageSeverity::Fatal:
				prefix = "FATAL";
				break;
			default:
				break;
		}

		std::fprintf( stderr, "[NVRHI/%s] %s\n", prefix, messageText ? messageText : "" );
	}
};

static std::string Vulkan_ResultString( VkResult result ) {
	return std::string( nvrhi::vulkan::resultToString( result ) );
}

static bool Vulkan_Fail( const std::string &message, const char *&error ) {
	error = OpenQ4_NvrhiMakeError( message );
	return false;
}

static bool Vulkan_Fail( const char *message, const char *&error ) {
	error = OpenQ4_NvrhiMakeError( message );
	return false;
}

template<typename procType_t>
static bool Vulkan_LoadGlobalProc(
	PFN_vkGetInstanceProcAddr getInstanceProcAddr,
	const char *name,
	procType_t &proc,
	const char *&error ) {
	proc = reinterpret_cast<procType_t>( getInstanceProcAddr( VK_NULL_HANDLE, name ) );
	if ( proc == nullptr ) {
		return Vulkan_Fail( std::string( "Failed to load Vulkan global procedure: " ) + name, error );
	}

	return true;
}

template<typename procType_t>
static bool Vulkan_LoadInstanceProc(
	PFN_vkGetInstanceProcAddr getInstanceProcAddr,
	VkInstance instance,
	const char *name,
	procType_t &proc,
	const char *&error ) {
	proc = reinterpret_cast<procType_t>( getInstanceProcAddr( instance, name ) );
	if ( proc == nullptr ) {
		return Vulkan_Fail( std::string( "Failed to load Vulkan instance procedure: " ) + name, error );
	}

	return true;
}

template<typename procType_t>
static bool Vulkan_LoadDeviceProc(
	PFN_vkGetDeviceProcAddr getDeviceProcAddr,
	VkDevice device,
	const char *name,
	procType_t &proc,
	const char *&error ) {
	proc = reinterpret_cast<procType_t>( getDeviceProcAddr( device, name ) );
	if ( proc == nullptr ) {
		return Vulkan_Fail( std::string( "Failed to load Vulkan device procedure: " ) + name, error );
	}

	return true;
}

static nvrhi::Color Vulkan_ProbeClearColor( double timeSeconds ) {
	const float red = 0.12f + 0.18f * static_cast<float>( std::sin( timeSeconds * 0.85 ) + 1.0 );
	const float green = 0.10f + 0.20f * static_cast<float>( std::sin( timeSeconds * 1.10 + 1.1 ) + 1.0 );
	const float blue = 0.15f + 0.20f * static_cast<float>( std::sin( timeSeconds * 1.40 + 2.3 ) + 1.0 );
	return nvrhi::Color( red, green, blue, 1.0f );
}

static nvrhi::Format Vulkan_ToNvrhiFormat( VkFormat format ) {
	switch ( format ) {
		case VK_FORMAT_B8G8R8A8_UNORM:
			return nvrhi::Format::BGRA8_UNORM;
		case VK_FORMAT_R8G8B8A8_UNORM:
			return nvrhi::Format::RGBA8_UNORM;
		default:
			return nvrhi::Format::UNKNOWN;
	}
}

static VkSurfaceFormatKHR Vulkan_ChooseSurfaceFormat( const std::vector<VkSurfaceFormatKHR> &formats ) {
	for ( const VkSurfaceFormatKHR &format : formats ) {
		if ( format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ) {
			return format;
		}
	}

	for ( const VkSurfaceFormatKHR &format : formats ) {
		if ( format.format == VK_FORMAT_R8G8B8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ) {
			return format;
		}
	}

	return formats[ 0 ];
}

class idNvrhiBootstrapBackendVulkan final : public idNvrhiBootstrapBackend {
public:
	virtual const char *GetName() const override {
		return "Vulkan/NVRHI";
	}

	virtual bool Initialize( SDL_Window *window, const openq4NvrhiBootstrapOptions_t &options, const char *&error ) override {
		window_ = window;
		options_ = options;

		if ( !LoadGlobalProcs( error ) ) {
			return false;
		}
		if ( !GetInstanceExtensions( error ) ) {
			return false;
		}
		if ( !CreateInstance( error ) ) {
			return false;
		}
		if ( !CreateSurface( error ) ) {
			return false;
		}
		if ( !PickPhysicalDevice( error ) ) {
			return false;
		}
		if ( !CreateDevice( error ) ) {
			return false;
		}
		if ( !CreateNvrhiDevice( error ) ) {
			return false;
		}
		if ( !CreateAcquireFence( error ) ) {
			return false;
		}
		if ( !CreateSwapChain( error ) ) {
			return false;
		}
		if ( !RebuildSwapChainImages( error ) ) {
			return false;
		}

		return true;
	}

	virtual bool RenderFrame( double timeSeconds, const char *&error ) override {
		int windowWidth = 0;
		int windowHeight = 0;
		if ( !OpenQ4_GetGraphicsWindowSizeInPixels( window_, windowWidth, windowHeight, error ) ) {
			return false;
		}

		if ( windowWidth <= 0 || windowHeight <= 0 ) {
			return true;
		}

		if ( windowWidth != static_cast<int>( swapChainExtent_.width ) || windowHeight != static_cast<int>( swapChainExtent_.height ) ) {
			if ( !ResizeSwapChain( error ) ) {
				return false;
			}
		}

		uint32_t imageIndex = 0;
		VkResult result = vkAcquireNextImageKHR_( device_, swapChain_, std::numeric_limits<uint64_t>::max(), VK_NULL_HANDLE, acquireFence_, &imageIndex );
		if ( result == VK_ERROR_OUT_OF_DATE_KHR ) {
			return ResizeSwapChain( error );
		}
		if ( result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR ) {
			return Vulkan_Fail( std::string( "vkAcquireNextImageKHR failed: " ) + Vulkan_ResultString( result ), error );
		}

		result = vkWaitForFences_( device_, 1, &acquireFence_, VK_TRUE, std::numeric_limits<uint64_t>::max() );
		if ( result != VK_SUCCESS ) {
			return Vulkan_Fail( std::string( "vkWaitForFences failed: " ) + Vulkan_ResultString( result ), error );
		}

		result = vkResetFences_( device_, 1, &acquireFence_ );
		if ( result != VK_SUCCESS ) {
			return Vulkan_Fail( std::string( "vkResetFences failed: " ) + Vulkan_ResultString( result ), error );
		}

		commandList_->open();
		nvrhi::utils::ClearColorAttachment(
			commandList_.Get(),
			framebuffers_[ imageIndex ].Get(),
			0,
			Vulkan_ProbeClearColor( timeSeconds ) );
		commandList_->close();
		nvrhiDevice_->executeCommandList( commandList_.Get() );

		if ( !nvrhiDevice_->waitForIdle() ) {
			return Vulkan_Fail( "NVRHI waitForIdle failed after Vulkan command submission.", error );
		}

		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapChain_;
		presentInfo.pImageIndices = &imageIndex;

		result = vkQueuePresentKHR_( graphicsQueue_, &presentInfo );
		if ( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ) {
			return ResizeSwapChain( error );
		}
		if ( result != VK_SUCCESS ) {
			return Vulkan_Fail( std::string( "vkQueuePresentKHR failed: " ) + Vulkan_ResultString( result ), error );
		}

		nvrhiDevice_->runGarbageCollection();
		return true;
	}

	virtual void Shutdown() override {
		if ( nvrhiDevice_ ) {
			nvrhiDevice_->waitForIdle();
		}

		framebuffers_.clear();
		swapChainTextures_.clear();
		swapChainImages_.clear();
		commandList_ = nullptr;
		nvrhiDevice_ = nullptr;

		if ( swapChain_ != VK_NULL_HANDLE && vkDestroySwapchainKHR_ != nullptr ) {
			vkDestroySwapchainKHR_( device_, swapChain_, nullptr );
			swapChain_ = VK_NULL_HANDLE;
		}

		if ( acquireFence_ != VK_NULL_HANDLE && vkDestroyFence_ != nullptr ) {
			vkDestroyFence_( device_, acquireFence_, nullptr );
			acquireFence_ = VK_NULL_HANDLE;
		}

		if ( device_ != VK_NULL_HANDLE && vkDestroyDevice_ != nullptr ) {
			vkDestroyDevice_( device_, nullptr );
			device_ = VK_NULL_HANDLE;
		}

		if ( surface_ != VK_NULL_HANDLE ) {
			SDL_Vulkan_DestroySurface( instance_, surface_, nullptr );
			surface_ = VK_NULL_HANDLE;
		}

		if ( instance_ != VK_NULL_HANDLE && vkDestroyInstance_ != nullptr ) {
			vkDestroyInstance_( instance_, nullptr );
			instance_ = VK_NULL_HANDLE;
		}

		if ( ownsVulkanLibrary_ ) {
			SDL_Vulkan_UnloadLibrary();
			ownsVulkanLibrary_ = false;
		}

		vkGetInstanceProcAddr_ = nullptr;
		vkGetDeviceProcAddr_ = nullptr;
		vkCreateInstance_ = nullptr;
		vkDestroyInstance_ = nullptr;
		vkEnumeratePhysicalDevices_ = nullptr;
		vkEnumerateDeviceExtensionProperties_ = nullptr;
		vkGetPhysicalDeviceQueueFamilyProperties_ = nullptr;
		vkGetPhysicalDeviceSurfaceSupportKHR_ = nullptr;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR_ = nullptr;
		vkGetPhysicalDeviceSurfaceFormatsKHR_ = nullptr;
		vkGetPhysicalDeviceSurfacePresentModesKHR_ = nullptr;
		vkCreateDevice_ = nullptr;
		vkDestroyDevice_ = nullptr;
		vkGetDeviceQueue_ = nullptr;
		vkCreateFence_ = nullptr;
		vkDestroyFence_ = nullptr;
		vkWaitForFences_ = nullptr;
		vkResetFences_ = nullptr;
		vkCreateSwapchainKHR_ = nullptr;
		vkDestroySwapchainKHR_ = nullptr;
		vkGetSwapchainImagesKHR_ = nullptr;
		vkAcquireNextImageKHR_ = nullptr;
		vkQueuePresentKHR_ = nullptr;

		physicalDevice_ = VK_NULL_HANDLE;
		graphicsQueue_ = VK_NULL_HANDLE;
		graphicsQueueFamilyIndex_ = UINT32_MAX;
		swapChainExtent_ = {};
		swapChainFormat_ = VK_FORMAT_UNDEFINED;
		window_ = NULL;
	}

private:
	bool LoadGlobalProcs( const char *&error ) {
		vkGetInstanceProcAddr_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>( SDL_Vulkan_GetVkGetInstanceProcAddr() );
		if ( vkGetInstanceProcAddr_ == nullptr ) {
			if ( !SDL_Vulkan_LoadLibrary( NULL ) ) {
				return Vulkan_Fail( std::string( "SDL_Vulkan_LoadLibrary failed: " ) + SDL_GetError(), error );
			}

			ownsVulkanLibrary_ = true;
			vkGetInstanceProcAddr_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>( SDL_Vulkan_GetVkGetInstanceProcAddr() );
		}

		if ( vkGetInstanceProcAddr_ == nullptr ) {
			return Vulkan_Fail( "SDL_Vulkan_GetVkGetInstanceProcAddr returned null.", error );
		}

		VULKAN_HPP_DEFAULT_DISPATCHER.init( vkGetInstanceProcAddr_ );
		return Vulkan_LoadGlobalProc( vkGetInstanceProcAddr_, "vkCreateInstance", vkCreateInstance_, error );
	}

	bool LoadInstanceProcs( const char *&error ) {
		VULKAN_HPP_DEFAULT_DISPATCHER.init( vk::Instance( instance_ ) );

		if ( !Vulkan_LoadInstanceProc( vkGetInstanceProcAddr_, instance_, "vkDestroyInstance", vkDestroyInstance_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadInstanceProc( vkGetInstanceProcAddr_, instance_, "vkEnumeratePhysicalDevices", vkEnumeratePhysicalDevices_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadInstanceProc( vkGetInstanceProcAddr_, instance_, "vkEnumerateDeviceExtensionProperties", vkEnumerateDeviceExtensionProperties_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadInstanceProc( vkGetInstanceProcAddr_, instance_, "vkGetPhysicalDeviceQueueFamilyProperties", vkGetPhysicalDeviceQueueFamilyProperties_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadInstanceProc( vkGetInstanceProcAddr_, instance_, "vkGetPhysicalDeviceSurfaceSupportKHR", vkGetPhysicalDeviceSurfaceSupportKHR_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadInstanceProc( vkGetInstanceProcAddr_, instance_, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", vkGetPhysicalDeviceSurfaceCapabilitiesKHR_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadInstanceProc( vkGetInstanceProcAddr_, instance_, "vkGetPhysicalDeviceSurfaceFormatsKHR", vkGetPhysicalDeviceSurfaceFormatsKHR_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadInstanceProc( vkGetInstanceProcAddr_, instance_, "vkGetPhysicalDeviceSurfacePresentModesKHR", vkGetPhysicalDeviceSurfacePresentModesKHR_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadInstanceProc( vkGetInstanceProcAddr_, instance_, "vkCreateDevice", vkCreateDevice_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadInstanceProc( vkGetInstanceProcAddr_, instance_, "vkGetDeviceProcAddr", vkGetDeviceProcAddr_, error ) ) {
			return false;
		}

		return true;
	}

	bool LoadDeviceProcs( const char *&error ) {
		VULKAN_HPP_DEFAULT_DISPATCHER.init( vk::Device( device_ ) );

		if ( !Vulkan_LoadDeviceProc( vkGetDeviceProcAddr_, device_, "vkDestroyDevice", vkDestroyDevice_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadDeviceProc( vkGetDeviceProcAddr_, device_, "vkGetDeviceQueue", vkGetDeviceQueue_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadDeviceProc( vkGetDeviceProcAddr_, device_, "vkCreateFence", vkCreateFence_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadDeviceProc( vkGetDeviceProcAddr_, device_, "vkDestroyFence", vkDestroyFence_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadDeviceProc( vkGetDeviceProcAddr_, device_, "vkWaitForFences", vkWaitForFences_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadDeviceProc( vkGetDeviceProcAddr_, device_, "vkResetFences", vkResetFences_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadDeviceProc( vkGetDeviceProcAddr_, device_, "vkCreateSwapchainKHR", vkCreateSwapchainKHR_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadDeviceProc( vkGetDeviceProcAddr_, device_, "vkDestroySwapchainKHR", vkDestroySwapchainKHR_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadDeviceProc( vkGetDeviceProcAddr_, device_, "vkGetSwapchainImagesKHR", vkGetSwapchainImagesKHR_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadDeviceProc( vkGetDeviceProcAddr_, device_, "vkAcquireNextImageKHR", vkAcquireNextImageKHR_, error ) ) {
			return false;
		}
		if ( !Vulkan_LoadDeviceProc( vkGetDeviceProcAddr_, device_, "vkQueuePresentKHR", vkQueuePresentKHR_, error ) ) {
			return false;
		}

		return true;
	}

	bool GetInstanceExtensions( const char *&error ) {
		Uint32 extensionCount = 0;
		const char * const *extensions = SDL_Vulkan_GetInstanceExtensions( &extensionCount );
		if ( extensions == NULL || extensionCount == 0 ) {
			return Vulkan_Fail( std::string( "SDL_Vulkan_GetInstanceExtensions failed: " ) + SDL_GetError(), error );
		}

		instanceExtensions_.assign( extensions, extensions + extensionCount );
		return true;
	}

	bool CreateInstance( const char *&error ) {
		VkApplicationInfo appInfo = {};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "OpenQ4 NVRHI Bootstrap";
		appInfo.applicationVersion = VK_MAKE_VERSION( 0, 1, 0 );
		appInfo.pEngineName = "OpenQ4";
		appInfo.engineVersion = VK_MAKE_VERSION( 0, 1, 0 );
		appInfo.apiVersion = VK_API_VERSION_1_2;

		VkInstanceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount = static_cast<uint32_t>( instanceExtensions_.size() );
		createInfo.ppEnabledExtensionNames = instanceExtensions_.data();

		const VkResult result = vkCreateInstance_( &createInfo, nullptr, &instance_ );
		if ( result != VK_SUCCESS ) {
			return Vulkan_Fail( std::string( "vkCreateInstance failed: " ) + Vulkan_ResultString( result ), error );
		}

		return LoadInstanceProcs( error );
	}

	bool CreateSurface( const char *&error ) {
		if ( !SDL_Vulkan_CreateSurface( window_, instance_, nullptr, &surface_ ) ) {
			return Vulkan_Fail( std::string( "SDL_Vulkan_CreateSurface failed: " ) + SDL_GetError(), error );
		}

		return true;
	}

	bool PickPhysicalDevice( const char *&error ) {
		uint32_t physicalDeviceCount = 0;
		VkResult result = vkEnumeratePhysicalDevices_( instance_, &physicalDeviceCount, nullptr );
		if ( result != VK_SUCCESS || physicalDeviceCount == 0 ) {
			return Vulkan_Fail( "vkEnumeratePhysicalDevices failed or found no adapters.", error );
		}

		std::vector<VkPhysicalDevice> devices( physicalDeviceCount );
		result = vkEnumeratePhysicalDevices_( instance_, &physicalDeviceCount, devices.data() );
		if ( result != VK_SUCCESS ) {
			return Vulkan_Fail( "vkEnumeratePhysicalDevices failed while retrieving adapters.", error );
		}

		for ( VkPhysicalDevice candidate : devices ) {
			uint32_t extensionCount = 0;
			vkEnumerateDeviceExtensionProperties_( candidate, nullptr, &extensionCount, nullptr );
			std::vector<VkExtensionProperties> extensions( extensionCount );
			if ( extensionCount > 0 ) {
				vkEnumerateDeviceExtensionProperties_( candidate, nullptr, &extensionCount, extensions.data() );
			}

			bool hasSwapChainExtension = false;
			for ( const VkExtensionProperties &extension : extensions ) {
				if ( !SDL_strcmp( extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME ) ) {
					hasSwapChainExtension = true;
					break;
				}
			}
			if ( !hasSwapChainExtension ) {
				continue;
			}

			uint32_t queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties_( candidate, &queueFamilyCount, nullptr );
			std::vector<VkQueueFamilyProperties> queueFamilies( queueFamilyCount );
			vkGetPhysicalDeviceQueueFamilyProperties_( candidate, &queueFamilyCount, queueFamilies.data() );

			for ( uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueFamilyCount; ++queueFamilyIndex ) {
				VkBool32 presentSupported = VK_FALSE;
				vkGetPhysicalDeviceSurfaceSupportKHR_( candidate, queueFamilyIndex, surface_, &presentSupported );

				if ( ( queueFamilies[ queueFamilyIndex ].queueFlags & VK_QUEUE_GRAPHICS_BIT ) && presentSupported ) {
					physicalDevice_ = candidate;
					graphicsQueueFamilyIndex_ = queueFamilyIndex;
					deviceExtensions_.push_back( VK_KHR_SWAPCHAIN_EXTENSION_NAME );
					return true;
				}
			}
		}

		return Vulkan_Fail( "No Vulkan physical device with graphics + present support was found.", error );
	}

	bool CreateDevice( const char *&error ) {
		const float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueInfo = {};
		queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfo.queueFamilyIndex = graphicsQueueFamilyIndex_;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &queuePriority;

		VkPhysicalDeviceFeatures deviceFeatures = {};

		VkDeviceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos = &queueInfo;
		createInfo.pEnabledFeatures = &deviceFeatures;
		createInfo.enabledExtensionCount = static_cast<uint32_t>( deviceExtensions_.size() );
		createInfo.ppEnabledExtensionNames = deviceExtensions_.data();

		const VkResult result = vkCreateDevice_( physicalDevice_, &createInfo, nullptr, &device_ );
		if ( result != VK_SUCCESS ) {
			return Vulkan_Fail( std::string( "vkCreateDevice failed: " ) + Vulkan_ResultString( result ), error );
		}

		if ( !LoadDeviceProcs( error ) ) {
			return false;
		}

		vkGetDeviceQueue_( device_, graphicsQueueFamilyIndex_, 0, &graphicsQueue_ );
		return true;
	}

	bool CreateNvrhiDevice( const char *&error ) {
		nvrhi::vulkan::DeviceDesc deviceDesc;
		deviceDesc.errorCB = &messageCallback_;
		deviceDesc.instance = instance_;
		deviceDesc.physicalDevice = physicalDevice_;
		deviceDesc.device = device_;
		deviceDesc.graphicsQueue = graphicsQueue_;
		deviceDesc.graphicsQueueIndex = static_cast<int>( graphicsQueueFamilyIndex_ );
		deviceDesc.instanceExtensions = instanceExtensions_.data();
		deviceDesc.numInstanceExtensions = instanceExtensions_.size();
		deviceDesc.deviceExtensions = deviceExtensions_.data();
		deviceDesc.numDeviceExtensions = deviceExtensions_.size();

		nvrhiDevice_ = nvrhi::vulkan::createDevice( deviceDesc );
		if ( !nvrhiDevice_ ) {
			return Vulkan_Fail( "nvrhi::vulkan::createDevice returned null.", error );
		}

		commandList_ = nvrhiDevice_->createCommandList();
		if ( !commandList_ ) {
			return Vulkan_Fail( "NVRHI failed to create a Vulkan command list.", error );
		}

		return true;
	}

	bool CreateAcquireFence( const char *&error ) {
		VkFenceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

		const VkResult result = vkCreateFence_( device_, &createInfo, nullptr, &acquireFence_ );
		if ( result != VK_SUCCESS ) {
			return Vulkan_Fail( std::string( "vkCreateFence failed: " ) + Vulkan_ResultString( result ), error );
		}

		return true;
	}

	bool ResizeSwapChain( const char *&error ) {
		int windowWidth = 0;
		int windowHeight = 0;
		if ( !OpenQ4_GetGraphicsWindowSizeInPixels( window_, windowWidth, windowHeight, error ) ) {
			return false;
		}

		if ( windowWidth <= 0 || windowHeight <= 0 ) {
			return true;
		}

		if ( nvrhiDevice_ ) {
			nvrhiDevice_->waitForIdle();
		}

		framebuffers_.clear();
		swapChainTextures_.clear();
		swapChainImages_.clear();

		if ( swapChain_ != VK_NULL_HANDLE ) {
			vkDestroySwapchainKHR_( device_, swapChain_, nullptr );
			swapChain_ = VK_NULL_HANDLE;
		}

		if ( acquireFence_ != VK_NULL_HANDLE ) {
			vkResetFences_( device_, 1, &acquireFence_ );
		}

		if ( !CreateSwapChain( error ) ) {
			return false;
		}
		return RebuildSwapChainImages( error );
	}

	bool CreateSwapChain( const char *&error ) {
		VkSurfaceCapabilitiesKHR surfaceCaps = {};
		VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR_( physicalDevice_, surface_, &surfaceCaps );
		if ( result != VK_SUCCESS ) {
			return Vulkan_Fail( std::string( "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed: " ) + Vulkan_ResultString( result ), error );
		}

		uint32_t surfaceFormatCount = 0;
		result = vkGetPhysicalDeviceSurfaceFormatsKHR_( physicalDevice_, surface_, &surfaceFormatCount, nullptr );
		if ( result != VK_SUCCESS || surfaceFormatCount == 0 ) {
			return Vulkan_Fail( "vkGetPhysicalDeviceSurfaceFormatsKHR failed or returned no formats.", error );
		}
		std::vector<VkSurfaceFormatKHR> surfaceFormats( surfaceFormatCount );
		result = vkGetPhysicalDeviceSurfaceFormatsKHR_( physicalDevice_, surface_, &surfaceFormatCount, surfaceFormats.data() );
		if ( result != VK_SUCCESS ) {
			return Vulkan_Fail( "vkGetPhysicalDeviceSurfaceFormatsKHR failed while retrieving formats.", error );
		}

		uint32_t presentModeCount = 0;
		result = vkGetPhysicalDeviceSurfacePresentModesKHR_( physicalDevice_, surface_, &presentModeCount, nullptr );
		if ( result != VK_SUCCESS || presentModeCount == 0 ) {
			return Vulkan_Fail( "vkGetPhysicalDeviceSurfacePresentModesKHR failed or returned no present modes.", error );
		}
		std::vector<VkPresentModeKHR> presentModes( presentModeCount );
		result = vkGetPhysicalDeviceSurfacePresentModesKHR_( physicalDevice_, surface_, &presentModeCount, presentModes.data() );
		if ( result != VK_SUCCESS ) {
			return Vulkan_Fail( "vkGetPhysicalDeviceSurfacePresentModesKHR failed while retrieving present modes.", error );
		}

		const VkSurfaceFormatKHR chosenFormat = Vulkan_ChooseSurfaceFormat( surfaceFormats );
		swapChainFormat_ = chosenFormat.format;

		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
		if ( !options_.vsync ) {
			for ( VkPresentModeKHR candidate : presentModes ) {
				if ( candidate == VK_PRESENT_MODE_MAILBOX_KHR ) {
					presentMode = candidate;
					break;
				}
				if ( candidate == VK_PRESENT_MODE_IMMEDIATE_KHR ) {
					presentMode = candidate;
				}
			}
		}

		if ( surfaceCaps.currentExtent.width != UINT32_MAX ) {
			swapChainExtent_ = surfaceCaps.currentExtent;
		} else {
			int windowWidth = 0;
			int windowHeight = 0;
			const char *sizeError = NULL;
			if ( !OpenQ4_GetGraphicsWindowSizeInPixels( window_, windowWidth, windowHeight, sizeError ) ) {
				windowWidth = 0;
				windowHeight = 0;
			}

			swapChainExtent_.width = std::clamp(
				static_cast<uint32_t>( windowWidth ),
				surfaceCaps.minImageExtent.width,
				surfaceCaps.maxImageExtent.width );
			swapChainExtent_.height = std::clamp(
				static_cast<uint32_t>( windowHeight ),
				surfaceCaps.minImageExtent.height,
				surfaceCaps.maxImageExtent.height );
		}

		uint32_t imageCount = surfaceCaps.minImageCount + 1;
		if ( imageCount < 2 ) {
			imageCount = 2;
		}
		if ( surfaceCaps.maxImageCount > 0 && imageCount > surfaceCaps.maxImageCount ) {
			imageCount = surfaceCaps.maxImageCount;
		}

		VkSwapchainCreateInfoKHR createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface_;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = chosenFormat.format;
		createInfo.imageColorSpace = chosenFormat.colorSpace;
		createInfo.imageExtent = swapChainExtent_;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.preTransform = surfaceCaps.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		result = vkCreateSwapchainKHR_( device_, &createInfo, nullptr, &swapChain_ );
		if ( result != VK_SUCCESS ) {
			return Vulkan_Fail( std::string( "vkCreateSwapchainKHR failed: " ) + Vulkan_ResultString( result ), error );
		}

		return true;
	}

	bool RebuildSwapChainImages( const char *&error ) {
		uint32_t imageCount = 0;
		VkResult result = vkGetSwapchainImagesKHR_( device_, swapChain_, &imageCount, nullptr );
		if ( result != VK_SUCCESS || imageCount == 0 ) {
			return Vulkan_Fail( "vkGetSwapchainImagesKHR failed or returned no images.", error );
		}

		swapChainImages_.resize( imageCount );
		result = vkGetSwapchainImagesKHR_( device_, swapChain_, &imageCount, swapChainImages_.data() );
		if ( result != VK_SUCCESS ) {
			return Vulkan_Fail( "vkGetSwapchainImagesKHR failed while retrieving images.", error );
		}

		const nvrhi::Format nvrhiFormat = Vulkan_ToNvrhiFormat( swapChainFormat_ );
		if ( nvrhiFormat == nvrhi::Format::UNKNOWN ) {
			return Vulkan_Fail( "Swap-chain format is not mapped in the Vulkan bootstrap.", error );
		}

		swapChainTextures_.resize( imageCount );
		framebuffers_.resize( imageCount );
		for ( uint32_t i = 0; i < imageCount; ++i ) {
			const std::string debugName = std::string( "Vulkan Swap Chain Image " ) + std::to_string( i );
			nvrhi::TextureDesc textureDesc;
			textureDesc
				.setDimension( nvrhi::TextureDimension::Texture2D )
				.setWidth( swapChainExtent_.width )
				.setHeight( swapChainExtent_.height )
				.setFormat( nvrhiFormat )
				.setIsRenderTarget( true )
				.enableAutomaticStateTracking( nvrhi::ResourceStates::Present )
				.setDebugName( debugName );

			swapChainTextures_[ i ] = nvrhiDevice_->createHandleForNativeTexture(
				nvrhi::ObjectTypes::VK_Image,
				reinterpret_cast<void *>( swapChainImages_[ i ] ),
				textureDesc );
			if ( !swapChainTextures_[ i ] ) {
				return Vulkan_Fail( "NVRHI failed to wrap a Vulkan swap-chain image.", error );
			}

			nvrhi::FramebufferDesc framebufferDesc;
			framebufferDesc.addColorAttachment( swapChainTextures_[ i ] );
			framebuffers_[ i ] = nvrhiDevice_->createFramebuffer( framebufferDesc );
			if ( !framebuffers_[ i ] ) {
				return Vulkan_Fail( "NVRHI failed to create a Vulkan framebuffer for the swap chain.", error );
			}
		}

		return true;
	}

	SDL_Window *window_ = NULL;
	openq4NvrhiBootstrapOptions_t options_;
	vulkanBootstrapMessageCallback_t messageCallback_;
	bool ownsVulkanLibrary_ = false;
	std::vector<const char *> instanceExtensions_;
	std::vector<const char *> deviceExtensions_;
	VkInstance instance_ = VK_NULL_HANDLE;
	VkSurfaceKHR surface_ = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
	VkDevice device_ = VK_NULL_HANDLE;
	VkQueue graphicsQueue_ = VK_NULL_HANDLE;
	uint32_t graphicsQueueFamilyIndex_ = UINT32_MAX;
	VkSwapchainKHR swapChain_ = VK_NULL_HANDLE;
	VkFormat swapChainFormat_ = VK_FORMAT_UNDEFINED;
	VkExtent2D swapChainExtent_ = {};
	VkFence acquireFence_ = VK_NULL_HANDLE;
	nvrhi::DeviceHandle nvrhiDevice_;
	nvrhi::CommandListHandle commandList_;
	std::vector<VkImage> swapChainImages_;
	std::vector<nvrhi::TextureHandle> swapChainTextures_;
	std::vector<nvrhi::FramebufferHandle> framebuffers_;
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_ = nullptr;
	PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr_ = nullptr;
	PFN_vkCreateInstance vkCreateInstance_ = nullptr;
	PFN_vkDestroyInstance vkDestroyInstance_ = nullptr;
	PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices_ = nullptr;
	PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties_ = nullptr;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties_ = nullptr;
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR_ = nullptr;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR_ = nullptr;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR_ = nullptr;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR_ = nullptr;
	PFN_vkCreateDevice vkCreateDevice_ = nullptr;
	PFN_vkDestroyDevice vkDestroyDevice_ = nullptr;
	PFN_vkGetDeviceQueue vkGetDeviceQueue_ = nullptr;
	PFN_vkCreateFence vkCreateFence_ = nullptr;
	PFN_vkDestroyFence vkDestroyFence_ = nullptr;
	PFN_vkWaitForFences vkWaitForFences_ = nullptr;
	PFN_vkResetFences vkResetFences_ = nullptr;
	PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR_ = nullptr;
	PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR_ = nullptr;
	PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR_ = nullptr;
	PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR_ = nullptr;
	PFN_vkQueuePresentKHR vkQueuePresentKHR_ = nullptr;
};

} // namespace

idNvrhiBootstrapBackend *OpenQ4_CreateNvrhiBootstrapVulkanBackend( void ) {
	return new idNvrhiBootstrapBackendVulkan();
}

#else

idNvrhiBootstrapBackend *OpenQ4_CreateNvrhiBootstrapVulkanBackend( void ) {
	return NULL;
}

#endif
