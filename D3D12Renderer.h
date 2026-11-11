#pragma once

#include <array>
#include <cstdint>

#include <Windows.h>
#include <wrl/client.h>
#include <dxgi1_6.h>
#include <d3d12.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

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
    void Clear();

private:
    HRESULT InitializeSwapChain(UINT width, UINT height);
    HRESULT CreateCommandObjects();
    HRESULT CreateSwapChain(HWND hwnd);
    HRESULT CreateRenderTargetViews();
    HRESULT WaitForGpu();

private:
    HWND m_hwnd;
    ComPtr<IDXGIFactory6> m_factory;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<IDXGISwapChain3> m_swapChain;

    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandles[FRAME_COUNT]{};

    std::array<ComPtr<ID3D12Resource>, FRAME_COUNT> m_renderTargets;
    std::array<ComPtr<ID3D12CommandAllocator>, FRAME_COUNT> m_commandAllocators;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Fence> m_fence;

    UINT m_rtvDescriptorSize{ 0 };
    UINT m_frameIndex{ 0 };

    UINT64 m_fenceValue{ 0 };
    HANDLE m_fenceEvent{ nullptr };

    UINT m_width{ 0 };
    UINT m_height{ 0 };
};
