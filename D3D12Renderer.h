#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#define NOMINMAX
#include <wrl/client.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include "IRenderer.h"
#include "ResourceUtils.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

struct FrameContext
{
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    UINT64 fenceValue{ 0 };
};

namespace
{
    constexpr UINT FRAME_COUNT = 2;
}

class CD3D12Renderer : public IRenderer
{
public:
    HRESULT Initialize(HWND hwnd) override;
    HRESULT Resize(UINT width, UINT height) override;
    HRESULT UploadVideoFrame(AVFrame* frame) override;
    HRESULT Render() override;
    void Shutdown() override;

private:
    HRESULT CreateCommandObjects();
    HRESULT CreateFence();
    HRESULT CreateSwapChain(HWND hwnd, UINT width, UINT height);
    HRESULT CreateRenderTargetViews();
    HRESULT CreateRootSignature();
    HRESULT CreatePipelineState();
    HRESULT CompileShader(const std::wstring& filename, const char* entryPoint, const char* target, ComPtr<ID3DBlob>& shaderBlob);
    HRESULT CreateVideoSrvHeap();
    HRESULT CreateVideoTexture(UINT width, UINT height);
    D3D12_VIEWPORT CalculateViewport();
    HRESULT CopyVideoPlanesToGpu();
    HRESULT WaitForFence(UINT64 fenceValue);
    HRESULT WaitForGpu();
    UINT64 SignalGpu();

private:
    ComPtr<IDXGIFactory6> m_factory;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<IDXGISwapChain3> m_swapChain;

    // SwapChain Back Buffer RTVs
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    UINT m_rtvDescriptorSize{ 0 };
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, FRAME_COUNT> m_rtvHandles;
    std::array<ComPtr<ID3D12Resource>, FRAME_COUNT> m_renderTargets;
    std::array<FrameContext, FRAME_COUNT> m_frames;
    ComPtr<ID3D12GraphicsCommandList> m_renderCommandList;
    ComPtr<ID3D12Fence> m_fence;

    UINT64 m_nextFenceValue{ 0 };
    UINT64 m_uploadFenceValue{ 0 };
    HandlePtr m_fenceEvent{ nullptr };

    // Texture
    ComPtr<ID3D12Resource> m_videoTexture;
    ComPtr<ID3D12Resource> m_videoUploadBuffer;
    uint8_t* m_mappedVideoData{ nullptr };
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_videoFootprint{};
    UINT64 m_videoUploadBufferSize{ 0 };
    D3D12_CPU_DESCRIPTOR_HANDLE m_videoTextureSrvHandle{};

    ComPtr<ID3D12CommandAllocator> m_uploadCommandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_uploadCommandList;

    bool m_videoTexturesInShaderState{ false };

    // Video Texture SRVs
    ComPtr<ID3D12DescriptorHeap> m_videoSrvHeap;

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3DBlob> m_vertexShader;
    ComPtr<ID3DBlob> m_pixelShader;
};
