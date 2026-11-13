#pragma once

#include <array>
#include <cstdint>
#include <mutex>

#include <Windows.h>
#include <wrl/client.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include "Utility.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

struct FrameContext
{
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    UINT64 fenceValue{ 0 };
};

struct PlaneUploadInfo
{
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows{ 0 };
    UINT64 rowSize{ 0 };
    UINT64 totalBytes{ 0 };
};

namespace
{
    constexpr UINT FRAME_COUNT = 2;
}

class CD3D12Renderer
{
public:
    HRESULT Initialize(HWND hwnd);
    HRESULT Resize(UINT width, UINT height);
    void Shutdown();
    HRESULT UploadVideoFrame(AVFrame* frame);
    HRESULT Render();
    void Clear();
    HANDLE GetFrameLatencyWaitableObject() const { return m_frameLatencyWaitableObject; }

private:
    HRESULT InitializeSwapChain(UINT width, UINT height);
    HRESULT CreateCommandObjects();
    HRESULT CreateFence();
    HRESULT CreateSwapChain(HWND hwnd);
    HRESULT CreateRenderTargetViews();
    HRESULT CreateRootSignature();
    HRESULT CreatePipelineState();
    HRESULT CompileShader(const std::wstring& filename, const char* entryPoint, const char* target, ComPtr<ID3DBlob>& shaderBlob);
    HRESULT CreateVideoSrvHeap();
    void CreateVideoSrvs();
    HRESULT CreateVideoTextures(UINT width, UINT height);
    HRESULT CreatePlaneTexture(UINT width, UINT height, DXGI_FORMAT format, ComPtr<ID3D12Resource>& texture);
    HRESULT CreateUploadBuffer(UINT64 size, ComPtr<ID3D12Resource>& buffer);
    PlaneUploadInfo GetPlaneUploadInfo(UINT width, UINT height);
    HRESULT CopyVideoPlanesToGpu();
    HRESULT WaitForFence(UINT64 fenceValue);
    HRESULT WaitForGpu();
    UINT64 SignalGpu();

private:
    HWND m_hwnd;
    ComPtr<IDXGIFactory6> m_factory;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<IDXGISwapChain3> m_swapChain;

    // SwapChain Back Buffer RTVs
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, FRAME_COUNT> m_rtvHandles;
    std::array<ComPtr<ID3D12Resource>, FRAME_COUNT> m_renderTargets;
    std::array<FrameContext, FRAME_COUNT> m_frames;
    ComPtr<ID3D12GraphicsCommandList> m_renderCommandList;
    ComPtr<ID3D12Fence> m_fence;

    UINT64 m_nextFenceValue{ 0 };
    UINT64 m_uploadFenceValue{ 0 };
    HANDLE m_fenceEvent{ nullptr };
    HANDLE m_frameLatencyWaitableObject{ nullptr };

    UINT m_width{ 0 };
    UINT m_height{ 0 };
    UINT m_rtvDescriptorSize{ 0 };

    ComPtr<ID3D12Resource> m_yTexture;
    ComPtr<ID3D12Resource> m_uTexture;
    ComPtr<ID3D12Resource> m_vTexture;

    ComPtr<ID3D12Resource> m_yUploadBuffer;
    ComPtr<ID3D12Resource> m_uUploadBuffer;
    ComPtr<ID3D12Resource> m_vUploadBuffer;

    PlaneUploadInfo m_yUploadInfo;
    PlaneUploadInfo m_uUploadInfo;
    PlaneUploadInfo m_vUploadInfo;

    UINT m_yRowPitch{ 0 };
    UINT m_uRowPitch{ 0 };
    UINT m_vRowPitch{ 0 };

    ComPtr<ID3D12CommandAllocator> m_uploadCommandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_uploadCommandList;

    bool m_videoTexturesInShaderState{ false };

    // Video Texture SRVs
    ComPtr<ID3D12DescriptorHeap> m_videoSrvHeap;
    UINT m_srvDescriptorSize{ 0 };

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3DBlob> m_vertexShader;
    ComPtr<ID3DBlob> m_pixelShader;
};
