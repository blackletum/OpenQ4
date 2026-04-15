#include "NvrhiBootstrap.h"
#include "../../sys/GraphicsWindow.h"

#if defined( OPENQ4_NVRHI_HAS_D3D12 )

#include <nvrhi/d3d12.h>
#include <nvrhi/utils.h>

#include <SDL3/SDL_video.h>

#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "NvrhiError.h"

using Microsoft::WRL::ComPtr;

namespace {

class d3d12BootstrapMessageCallback_t final : public nvrhi::IMessageCallback {
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

static std::string D3D12_HResultString( HRESULT hr ) {
	char buffer[ 32 ];
	std::snprintf( buffer, sizeof( buffer ), "0x%08lx", static_cast<unsigned long>( hr ) );
	return std::string( buffer );
}

static bool D3D12_Fail( const std::string &message, const char *&error ) {
	error = OpenQ4_NvrhiMakeError( message );
	return false;
}

static bool D3D12_Fail( const char *message, const char *&error ) {
	error = OpenQ4_NvrhiMakeError( message );
	return false;
}

static nvrhi::Color D3D12_ProbeClearColor( double timeSeconds ) {
	const float red = 0.25f + 0.25f * static_cast<float>( std::sin( timeSeconds * 0.75 ) );
	const float green = 0.20f + 0.35f * static_cast<float>( std::sin( timeSeconds * 1.05 + 1.2 ) );
	const float blue = 0.30f + 0.30f * static_cast<float>( std::sin( timeSeconds * 1.35 + 2.4 ) );
	return nvrhi::Color( red, green, blue, 1.0f );
}

class idNvrhiBootstrapBackendD3D12 final : public idNvrhiBootstrapBackend {
public:
	virtual const char *GetName() const override {
		return "DX12/NVRHI";
	}

	virtual bool Initialize( SDL_Window *window, const openq4NvrhiBootstrapOptions_t &options, const char *&error ) override {
		window_ = window;
		options_ = options;

#if defined( _DEBUG )
		ComPtr<ID3D12Debug> debugLayer;
		if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugLayer ) ) ) ) {
			debugLayer->EnableDebugLayer();
		}
#endif

		UINT factoryFlags = 0;
#if defined( _DEBUG )
		factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
		HRESULT hr = CreateDXGIFactory2( factoryFlags, IID_PPV_ARGS( &factory_ ) );
		if ( FAILED( hr ) ) {
			return D3D12_Fail( std::string( "CreateDXGIFactory2 failed: " ) + D3D12_HResultString( hr ), error );
		}

		if ( !CreateDevice( error ) ) {
			return false;
		}
		if ( !CreateCommandQueue( error ) ) {
			return false;
		}
		if ( !CreateNvrhiDevice( error ) ) {
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

		if ( windowWidth != static_cast<int>( width_ ) || windowHeight != static_cast<int>( height_ ) ) {
			if ( !ResizeSwapChain( static_cast<UINT>( windowWidth ), static_cast<UINT>( windowHeight ), error ) ) {
				return false;
			}
		}

		const UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();
		if ( backBufferIndex >= framebuffers_.size() ) {
			return D3D12_Fail( "DX12 swap chain returned an invalid back-buffer index.", error );
		}

		commandList_->open();
		nvrhi::utils::ClearColorAttachment(
			commandList_.Get(),
			framebuffers_[ backBufferIndex ].Get(),
			0,
			D3D12_ProbeClearColor( timeSeconds ) );
		commandList_->close();
		nvrhiDevice_->executeCommandList( commandList_.Get() );

		if ( !nvrhiDevice_->waitForIdle() ) {
			return D3D12_Fail( "NVRHI waitForIdle failed after DX12 command submission.", error );
		}

		const HRESULT hr = swapChain_->Present( options_.vsync ? 1 : 0, 0 );
		if ( FAILED( hr ) ) {
			return D3D12_Fail( std::string( "IDXGISwapChain::Present failed: " ) + D3D12_HResultString( hr ), error );
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
		swapChainBuffers_.clear();
		commandList_ = nullptr;
		nvrhiDevice_ = nullptr;
		swapChain_.Reset();
		queue_.Reset();
		device_.Reset();
		factory_.Reset();
		window_ = NULL;
		width_ = 0;
		height_ = 0;
	}

private:
	static constexpr UINT SWAP_CHAIN_IMAGE_COUNT = 2;

	bool CreateDevice( const char *&error ) {
		for ( UINT adapterIndex = 0; ; ++adapterIndex ) {
			ComPtr<IDXGIAdapter1> adapter;
			if ( factory_->EnumAdapters1( adapterIndex, &adapter ) == DXGI_ERROR_NOT_FOUND ) {
				break;
			}

			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1( &desc );
			if ( desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE ) {
				continue;
			}

			const HRESULT hr = D3D12CreateDevice( adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &device_ ) );
			if ( SUCCEEDED( hr ) ) {
				adapter_ = adapter;
				return true;
			}
		}

		return D3D12_Fail( "No suitable D3D12 adapter was found.", error );
	}

	bool CreateCommandQueue( const char *&error ) {
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

		const HRESULT hr = device_->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &queue_ ) );
		if ( FAILED( hr ) ) {
			return D3D12_Fail( std::string( "ID3D12Device::CreateCommandQueue failed: " ) + D3D12_HResultString( hr ), error );
		}

		return true;
	}

	bool CreateNvrhiDevice( const char *&error ) {
		nvrhi::d3d12::DeviceDesc deviceDesc;
		deviceDesc.errorCB = &messageCallback_;
		deviceDesc.pDevice = device_.Get();
		deviceDesc.pGraphicsCommandQueue = queue_.Get();

		nvrhiDevice_ = nvrhi::d3d12::createDevice( deviceDesc );
		if ( !nvrhiDevice_ ) {
			return D3D12_Fail( "nvrhi::d3d12::createDevice returned null.", error );
		}

		commandList_ = nvrhiDevice_->createCommandList();
		if ( !commandList_ ) {
			return D3D12_Fail( "NVRHI failed to create a DX12 command list.", error );
		}

		return true;
	}

	bool CreateSwapChain( const char *&error ) {
		HWND hwnd = NULL;
		void *nativeWindow = nullptr;
		if ( !OpenQ4_GetWindowWin32Handle( window_, nativeWindow, error ) ) {
			return false;
		}
		hwnd = static_cast<HWND>( nativeWindow );

		int windowWidth = 0;
		int windowHeight = 0;
		if ( !OpenQ4_GetGraphicsWindowSizeInPixels( window_, windowWidth, windowHeight, error ) ) {
			return false;
		}

		width_ = windowWidth > 0 ? static_cast<UINT>( windowWidth ) : static_cast<UINT>( options_.width );
		height_ = windowHeight > 0 ? static_cast<UINT>( windowHeight ) : static_cast<UINT>( options_.height );

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = width_;
		swapChainDesc.Height = height_;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.Stereo = FALSE;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = SWAP_CHAIN_IMAGE_COUNT;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		swapChainDesc.Flags = 0;

		ComPtr<IDXGISwapChain1> baseSwapChain;
		HRESULT hr = factory_->CreateSwapChainForHwnd(
			queue_.Get(),
			hwnd,
			&swapChainDesc,
			NULL,
			NULL,
			&baseSwapChain );
		if ( FAILED( hr ) ) {
			return D3D12_Fail( std::string( "CreateSwapChainForHwnd failed: " ) + D3D12_HResultString( hr ), error );
		}

		hr = factory_->MakeWindowAssociation( hwnd, DXGI_MWA_NO_ALT_ENTER );
		if ( FAILED( hr ) ) {
			return D3D12_Fail( std::string( "MakeWindowAssociation failed: " ) + D3D12_HResultString( hr ), error );
		}

		hr = baseSwapChain.As( &swapChain_ );
		if ( FAILED( hr ) ) {
			return D3D12_Fail( std::string( "Failed to query IDXGISwapChain4: " ) + D3D12_HResultString( hr ), error );
		}

		return true;
	}

	bool ResizeSwapChain( UINT newWidth, UINT newHeight, const char *&error ) {
		if ( newWidth == 0 || newHeight == 0 ) {
			return true;
		}

		if ( nvrhiDevice_ ) {
			nvrhiDevice_->waitForIdle();
		}
		framebuffers_.clear();
		swapChainTextures_.clear();
		swapChainBuffers_.clear();

		const HRESULT hr = swapChain_->ResizeBuffers(
			SWAP_CHAIN_IMAGE_COUNT,
			newWidth,
			newHeight,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			0 );
		if ( FAILED( hr ) ) {
			return D3D12_Fail( std::string( "IDXGISwapChain::ResizeBuffers failed: " ) + D3D12_HResultString( hr ), error );
		}

		width_ = newWidth;
		height_ = newHeight;
		return RebuildSwapChainImages( error );
	}

	bool RebuildSwapChainImages( const char *&error ) {
		swapChainBuffers_.resize( SWAP_CHAIN_IMAGE_COUNT );
		swapChainTextures_.resize( SWAP_CHAIN_IMAGE_COUNT );
		framebuffers_.resize( SWAP_CHAIN_IMAGE_COUNT );

		for ( UINT i = 0; i < SWAP_CHAIN_IMAGE_COUNT; ++i ) {
			HRESULT hr = swapChain_->GetBuffer( i, IID_PPV_ARGS( &swapChainBuffers_[ i ] ) );
			if ( FAILED( hr ) ) {
				return D3D12_Fail( std::string( "IDXGISwapChain::GetBuffer failed: " ) + D3D12_HResultString( hr ), error );
			}

			const std::string debugName = std::string( "DX12 Swap Chain Image " ) + std::to_string( i );
			nvrhi::TextureDesc textureDesc;
			textureDesc
				.setDimension( nvrhi::TextureDimension::Texture2D )
				.setWidth( width_ )
				.setHeight( height_ )
				.setFormat( nvrhi::Format::RGBA8_UNORM )
				.setIsRenderTarget( true )
				.enableAutomaticStateTracking( nvrhi::ResourceStates::Present )
				.setDebugName( debugName );

			swapChainTextures_[ i ] = nvrhiDevice_->createHandleForNativeTexture(
				nvrhi::ObjectTypes::D3D12_Resource,
				swapChainBuffers_[ i ].Get(),
				textureDesc );
			if ( !swapChainTextures_[ i ] ) {
				return D3D12_Fail( "NVRHI failed to wrap a DX12 swap-chain texture.", error );
			}

			nvrhi::FramebufferDesc framebufferDesc;
			framebufferDesc.addColorAttachment( swapChainTextures_[ i ] );
			framebuffers_[ i ] = nvrhiDevice_->createFramebuffer( framebufferDesc );
			if ( !framebuffers_[ i ] ) {
				return D3D12_Fail( "NVRHI failed to create a DX12 framebuffer for the swap chain.", error );
			}
		}

		return true;
	}

	SDL_Window *window_ = NULL;
	openq4NvrhiBootstrapOptions_t options_;
	d3d12BootstrapMessageCallback_t messageCallback_;
	ComPtr<IDXGIFactory6> factory_;
	ComPtr<IDXGIAdapter1> adapter_;
	ComPtr<ID3D12Device> device_;
	ComPtr<ID3D12CommandQueue> queue_;
	ComPtr<IDXGISwapChain4> swapChain_;
	UINT width_ = 0;
	UINT height_ = 0;
	nvrhi::DeviceHandle nvrhiDevice_;
	nvrhi::CommandListHandle commandList_;
	std::vector<ComPtr<ID3D12Resource>> swapChainBuffers_;
	std::vector<nvrhi::TextureHandle> swapChainTextures_;
	std::vector<nvrhi::FramebufferHandle> framebuffers_;
};

} // namespace

idNvrhiBootstrapBackend *OpenQ4_CreateNvrhiBootstrapD3D12Backend( void ) {
	return new idNvrhiBootstrapBackendD3D12();
}

#else

idNvrhiBootstrapBackend *OpenQ4_CreateNvrhiBootstrapD3D12Backend( void ) {
	return NULL;
}

#endif
