#include "D3D12Renderer.h"

#include "PlayerGlobalContext.h"

HRESULT CD3D12Renderer::Initialize(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return E_INVALIDARG;

    CPlayerGlobalContext& context = CPlayerGlobalContext::GetInstance();

    m_hwnd = hwnd;
    m_factory = context.GetFactory();
    m_device = context.GetDevice();
    m_commandQueue = context.GetCommandQueue();

    HRESULT hr = CreateCommandObjects();
    if (FAILED(hr))
        return hr;

    return S_OK;
}

HRESULT CD3D12Renderer::Resize(UINT width, UINT height)
{
    if (width == 0 || height == 0)
        return S_FALSE;

    if (!m_swapChain)
    {
        return InitializeSwapChain(width, height);
    }

    if (width == m_width && height == m_height)
        return S_OK;

    // Wait until the GPU finishes using the old buffers
    HRESULT hr = WaitForGpu();
    if (FAILED(hr))
        return hr;

    // Release old back buffers
    for (auto& renderTarget : m_renderTargets)
        renderTarget.Reset();

    // Resize swap chain buffers
    hr = m_swapChain->ResizeBuffers(FRAME_COUNT, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    if (FAILED(hr))
        return hr;

    m_width = width;
    m_height = height;
    
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Recreate render target views
    return CreateRenderTargetViews();
}

void CD3D12Renderer::Shutdown()
{
    if (m_commandQueue && m_fence && m_fenceEvent)
        WaitForGpu();

    m_commandList.Reset();

    for (auto& allocator : m_commandAllocators)
        allocator.Reset();

    for (auto& target : m_renderTargets)
        target.Reset();

    m_rtvHeap.Reset();
    m_swapChain.Reset();

    m_fence.Reset();

    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

HRESULT CD3D12Renderer::CreateCommandObjects()
{
    HRESULT hr;

    for (UINT i = 0; i < FRAME_COUNT; ++i)
    {
        hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i]));
        if (FAILED(hr))
            return hr;
    }

    hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_commandList));
    if (FAILED(hr))
        return hr;

    // Command lists are initially open.
    hr = m_commandList->Close();
    if (FAILED(hr))
        return hr;

    hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr))
        return hr;

    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent)
        return HRESULT_FROM_WIN32(GetLastError());

    return S_OK;
}

HRESULT CD3D12Renderer::InitializeSwapChain(UINT width, UINT height)
{
    if (width == 0 || height == 0)
        return E_INVALIDARG;

    m_width = width;
    m_height = height;

    HRESULT hr = CreateSwapChain(m_hwnd);
    if (FAILED(hr))
        return hr;

    hr = CreateRenderTargetViews();
    if (FAILED(hr))
        return hr;

    return S_OK;
}

HRESULT CD3D12Renderer::CreateSwapChain(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = m_width;
    desc.Height = m_height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = FRAME_COUNT;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = 0;

    ComPtr<IDXGISwapChain1> swapChain;
    HRESULT hr = m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &desc, nullptr, nullptr, &swapChain);
    if (FAILED(hr))
        return hr;

    hr = swapChain.As(&m_swapChain);
    if (FAILED(hr))
        return hr;

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Disable Alt+Enter fullscreen switching
    return m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
}

HRESULT CD3D12Renderer::CreateRenderTargetViews()
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.NumDescriptors = FRAME_COUNT;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    heapDesc.NodeMask = 0;

    HRESULT hr = m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_rtvHeap));
    if (FAILED(hr))
        return hr;

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < FRAME_COUNT; ++i)
    {
        hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        if (FAILED(hr))
            return hr;

        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, handle);

        m_rtvHandles[i] = handle;

        handle.ptr += m_rtvDescriptorSize;
    }

    return S_OK;
}

HRESULT CD3D12Renderer::WaitForGpu()
{
    if (!m_commandQueue || !m_fence)
        return E_FAIL;

    const UINT64 fenceValue = ++m_fenceValue;

    HRESULT hr = m_commandQueue->Signal(m_fence.Get(), fenceValue);
    if (FAILED(hr))
        return hr;

    if (m_fence->GetCompletedValue() < fenceValue)
    {
        hr = m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
        if (FAILED(hr))
            return hr;

        const DWORD waitResult = WaitForSingleObject(m_fenceEvent, INFINITE);

        if (waitResult != WAIT_OBJECT_0)
            return HRESULT_FROM_WIN32(waitResult == WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE);
    }

    return S_OK;
}

void CD3D12Renderer::Clear()
{
    if (!m_swapChain)
        return;

    const UINT backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    ComPtr<ID3D12Resource>& backBuffer = m_renderTargets[backBufferIndex];

    HRESULT hr = m_commandAllocators[backBufferIndex]->Reset();
    if (FAILED(hr))
        return;

    // Reset command list
    hr = m_commandList->Reset(m_commandAllocators[backBufferIndex].Get(), nullptr);
    if (FAILED(hr))
        return;

    // Transition: PRESENT -> RENDER_TARGET
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = backBuffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    m_commandList->ResourceBarrier(1, &barrier);

    // Clear render target to black
    constexpr float clearColor[] = {
        1.0f,  // Red
        0.0f,  // Green
        0.0f,  // Blue
        1.0f   // Alpha
    };

    m_commandList->ClearRenderTargetView(m_rtvHandles[backBufferIndex], clearColor, 0, nullptr);

    // Transition: RENDER_TARGET -> PRESENT
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

    m_commandList->ResourceBarrier(1, &barrier);

    // Close command list
    hr = m_commandList->Close();
    if (FAILED(hr))
        return;

    // Execute command list
    ID3D12CommandList* commandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, commandLists);

    // Present
    hr = m_swapChain->Present(1, 0);
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Present failed\n");
        return;
    }
}
