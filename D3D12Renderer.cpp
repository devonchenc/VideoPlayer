#include "D3D12Renderer.h"

#include <D3DCompiler.h>
#include "PlayerGlobalContext.h"

#pragma comment(lib, "D3DCompiler.lib")

namespace
{
    void CopyPlane(BYTE* dst, UINT dstRowPitch, const BYTE* src, int srcLinesize, UINT rowSize, UINT height)
    {
        for (UINT y = 0; y < height; ++y)
        {
            memcpy(dst + static_cast<size_t>(y) * dstRowPitch, src + static_cast<ptrdiff_t>(y) * srcLinesize, rowSize);
        }
    }

    void CopyPlaneToTexture(ID3D12GraphicsCommandList* commandList, ID3D12Resource* uploadBuffer, ID3D12Resource* texture, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint)
    {
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = uploadBuffer;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprint;

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = texture;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
}

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

    hr = CreateFence();
    if (FAILED(hr))
        return hr;

    hr = CreateRootSignature();
    if (FAILED(hr))
        return hr;

    hr = CreatePipelineState();
    if (FAILED(hr))
        return hr;

    return S_OK;
}

HRESULT CD3D12Renderer::Resize(UINT width, UINT height)
{
    if (width == 0 || height == 0)
        return S_FALSE;

    if (!m_swapChain)
        return InitializeSwapChain(width, height);

    if (width == m_width && height == m_height)
        return S_OK;

    // Wait until the GPU finishes using the old buffers
    HRESULT hr = WaitForGpu();
    if (FAILED(hr))
        return hr;

    // Release old back buffers
    for (ComPtr<ID3D12Resource>& renderTarget : m_renderTargets)
        renderTarget.Reset();

    // Resize swap chain buffers
    hr = m_swapChain->ResizeBuffers(FRAME_COUNT, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    if (FAILED(hr))
        return hr;

    m_width = width;
    m_height = height;

    // Recreate render target views.
    hr = CreateRenderTargetViews();
    if (FAILED(hr))
        return hr;

    // Recreate render target views
    return S_OK;
}

void CD3D12Renderer::Shutdown()
{
    if (m_commandQueue && m_fence && m_fenceEvent)
        WaitForGpu();

    m_renderCommandList.Reset();
    m_uploadCommandList.Reset();

    for (FrameContext& frame : m_frames)
        frame.commandAllocator.Reset();

    for (ComPtr<ID3D12Resource>& target : m_renderTargets)
        target.Reset();

    if (m_frameLatencyWaitableObject)
    {
        CloseHandle(m_frameLatencyWaitableObject);
        m_frameLatencyWaitableObject = nullptr;
    }

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
        hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_frames[i].commandAllocator));
        if (FAILED(hr))
            return hr;
    }

    hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_frames[0].commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_renderCommandList));
    if (FAILED(hr))
        return hr;

    // Command lists are initially open
    hr = m_renderCommandList->Close();
    if (FAILED(hr))
        return hr;

    hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_uploadCommandAllocator));
    if (FAILED(hr))
        return hr;

    hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_uploadCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_uploadCommandList));
    if (FAILED(hr))
        return hr;

    hr = m_uploadCommandList->Close();
    if (FAILED(hr))
        return hr;

    return S_OK;
}

HRESULT CD3D12Renderer::CreateFence()
{
    HRESULT hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
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

    hr = CreateVideoSrvHeap();
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
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    ComPtr<IDXGISwapChain1> swapChain;
    HRESULT hr = m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &desc, nullptr, nullptr, &swapChain);
    if (FAILED(hr))
        return hr;

    hr = swapChain.As(&m_swapChain);
    if (FAILED(hr))
        return hr;

    // Configure maximum frame latency
    hr = m_swapChain->SetMaximumFrameLatency(1);
    if (FAILED(hr))
    {
        return hr;
    }

    // Retrieve the frame latency waitable object
    m_frameLatencyWaitableObject = m_swapChain->GetFrameLatencyWaitableObject();
    if (!m_frameLatencyWaitableObject)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

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

    m_rtvHeap.Reset();
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

HRESULT CD3D12Renderer::WaitForFence(UINT64 fenceValue)
{
    if (m_fence->GetCompletedValue() >= fenceValue)
        return S_OK;

    HRESULT hr = m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
    if (FAILED(hr))
        return hr;

    DWORD result = WaitForSingleObject(m_fenceEvent, INFINITE);
    if (result != WAIT_OBJECT_0)
        return HRESULT_FROM_WIN32(result == WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE);

    return S_OK;
}

HRESULT CD3D12Renderer::WaitForGpu()
{
    if (!m_commandQueue || !m_fence)
        return E_FAIL;

    const UINT64 fenceValue = ++m_nextFenceValue;

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

HRESULT CD3D12Renderer::UploadVideoFrame(AVFrame* frame)
{
    if (!frame)
        return E_INVALIDARG;

    if (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P)
        return E_NOTIMPL;

    const UINT videoWidth = frame->width;
    const UINT videoHeight = frame->height;

    if (videoWidth == 0 || videoHeight == 0)
        return E_INVALIDARG;

    const UINT chromaWidth = (videoWidth + 1) / 2;
    const UINT chromaHeight = (videoHeight + 1) / 2;

    // 1. Recreate resources if video format or size changes
    if (m_videoWidth != videoWidth || m_videoHeight != videoHeight)
    {
        // Wait for previous GPU usage before releasing old textures
        const UINT backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
        FrameContext& frame = m_frames[backBufferIndex];

        HRESULT hr;
        if (frame.fenceValue != 0)
        {
            hr = WaitForFence(frame.fenceValue);
            if (FAILED(hr))
                return hr;
        }

        hr = CreateVideoTextures(videoWidth, videoHeight);
        if (FAILED(hr))
            return hr;

        CreateVideoSrvs();

        m_videoWidth = videoWidth;
        m_videoHeight = videoHeight;
    }

    HRESULT hr;
    // 2. Wait until previous GPU usage has completed
    if (m_uploadFenceValue != 0)
    {
        hr = WaitForFence(m_uploadFenceValue);
        if (FAILED(hr))
            return hr;
    }

    // 3. Map upload buffers.
    BYTE* yMapped = nullptr;
    BYTE* uMapped = nullptr;
    BYTE* vMapped = nullptr;

    D3D12_RANGE readRange{ 0, 0 };

    hr = m_yUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&yMapped));
    if (FAILED(hr))
        return hr;

    hr = m_uUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&uMapped));
    if (FAILED(hr))
    {
        m_yUploadBuffer->Unmap(0, nullptr);
        return hr;
    }

    hr = m_vUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&vMapped));
    if (FAILED(hr))
    {
        m_yUploadBuffer->Unmap(0, nullptr);
        m_uUploadBuffer->Unmap(0, nullptr);
        return hr;
    }

    // 4. Copy AVFrame data to upload buffers
    CopyPlane(yMapped, m_yRowPitch, frame->data[0], frame->linesize[0], videoWidth, videoHeight);
    CopyPlane(uMapped, m_uRowPitch, frame->data[1], frame->linesize[1], chromaWidth, chromaHeight);
    CopyPlane(vMapped, m_vRowPitch, frame->data[2], frame->linesize[2], chromaWidth, chromaHeight);

    // 5. Unmap upload buffers
    m_yUploadBuffer->Unmap(0, nullptr);
    m_uUploadBuffer->Unmap(0, nullptr);
    m_vUploadBuffer->Unmap(0, nullptr);

    // 6. Copy upload buffers to GPU textures
    return CopyVideoPlanesToGpu();
}

HRESULT CD3D12Renderer::Render()
{
    if (!m_swapChain || !m_pipelineState)
        return E_UNEXPECTED;

    if (!m_yTexture || !m_uTexture || !m_vTexture)
        return S_FALSE;

    // 1. Get current back buffer.
    const UINT backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    FrameContext& frame = m_frames[backBufferIndex];

    HRESULT hr;
    // 2. Wait for previous rendering to complete
    if (frame.fenceValue != 0)
    {
        hr = WaitForFence(frame.fenceValue);
        if (FAILED(hr))
            return hr;
    }

    // 3. Reset command allocator and command list.
    hr = frame.commandAllocator->Reset();
    if (FAILED(hr))
        return hr;

    hr = m_renderCommandList->Reset(frame.commandAllocator.Get(), m_pipelineState.Get());
    if (FAILED(hr))
        return hr;

    ComPtr<ID3D12Resource>& backBuffer = m_renderTargets[backBufferIndex];

    // 4. Transition PRESENT -> RENDER_TARGET
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backBuffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    m_renderCommandList->ResourceBarrier(1, &barrier);

    // 5. Set render target
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    rtvHandle.ptr += static_cast<SIZE_T>(backBufferIndex) * m_rtvDescriptorSize;

    m_renderCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // 6. Set viewport and scissor rect
    D3D12_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect{};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(m_width);
    scissorRect.bottom = static_cast<LONG>(m_height);

    m_renderCommandList->RSSetViewports(1, &viewport);

    m_renderCommandList->RSSetScissorRects(1, &scissorRect);

    // 7. Clear render target
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };

    m_renderCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // 8. Bind pipeline and root signature.
    m_renderCommandList->SetGraphicsRootSignature(m_rootSignature.Get());

    m_renderCommandList->SetPipelineState(m_pipelineState.Get());

    // 9. Bind video SRV descriptor heap.
    ID3D12DescriptorHeap* descriptorHeaps[] = { m_videoSrvHeap.Get() };

    m_renderCommandList->SetDescriptorHeaps(1, descriptorHeaps);

    // 10. Bind SRV descriptor table.
    m_renderCommandList->SetGraphicsRootDescriptorTable(0, m_videoSrvHeap->GetGPUDescriptorHandleForHeapStart());

    // 11. Draw fullscreen triangle
    m_renderCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_renderCommandList->DrawInstanced(3, 1, 0, 0);

    // 12. Transition RENDER_TARGET -> PRESENT
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

    m_renderCommandList->ResourceBarrier(1, &barrier);

    // 13. Close command list.
    hr = m_renderCommandList->Close();

    if (FAILED(hr))
        return hr;

    // 14. Execute command list
    ID3D12CommandList* commandLists[] = { m_renderCommandList.Get() };

    m_commandQueue->ExecuteCommandLists(1, commandLists);

    // 15. Present
    hr = m_swapChain->Present(1, 0);

    // 16. Signal GPU fence
    frame.fenceValue = SignalGpu();

    return hr;
}

void CD3D12Renderer::Clear()
{
    if (!m_swapChain)
        return;

    const UINT backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    FrameContext& frame = m_frames[backBufferIndex];

    // Wait until previous GPU work is completed
    HRESULT hr = WaitForFence(frame.fenceValue);
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Wait for frame failed\n");
        return;
    }

    // Reset command allocator
    hr = frame.commandAllocator->Reset();
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Command allocator reset failed\n");
        return;
    }

    // Reset command list
    hr = m_renderCommandList->Reset(frame.commandAllocator.Get(), nullptr);
    if (FAILED(hr))
        return;

    ComPtr<ID3D12Resource>& backBuffer = m_renderTargets[backBufferIndex];

    // Transition: PRESENT -> RENDER_TARGET
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = backBuffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    m_renderCommandList->ResourceBarrier(1, &barrier);

    // Clear render target to black
    constexpr float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_renderCommandList->ClearRenderTargetView(m_rtvHandles[backBufferIndex], clearColor, 0, nullptr);

    // Transition: RENDER_TARGET -> PRESENT
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

    m_renderCommandList->ResourceBarrier(1, &barrier);

    // Close command list
    hr = m_renderCommandList->Close();
    if (FAILED(hr))
    {
        OutputDebugStringW(L"CommandList Close failed\n");
        return;
    }

    // Execute command list
    ID3D12CommandList* commandLists[] = { m_renderCommandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, commandLists);

    // Signal GPU completion for this frame
    frame.fenceValue = SignalGpu();

    // Present
    hr = m_swapChain->Present(1, 0);
    if (FAILED(hr))
    {
        OutputDebugStringW(L"Present failed\n");
        return;
    }
}

HRESULT CD3D12Renderer::CreateVideoTextures(UINT width, UINT height)
{
    const UINT chromaWidth = (width + 1) / 2;
    const UINT chromaHeight = (height + 1) / 2;

    HRESULT hr = CreatePlaneTexture(width, height, DXGI_FORMAT_R8_UNORM, m_yTexture);
    if (FAILED(hr))
        return hr;

    hr = CreatePlaneTexture(chromaWidth, chromaHeight, DXGI_FORMAT_R8_UNORM, m_uTexture);
    if (FAILED(hr))
        return hr;

    hr = CreatePlaneTexture(chromaWidth, chromaHeight, DXGI_FORMAT_R8_UNORM, m_vTexture);
    if (FAILED(hr))
        return hr;

    m_yUploadInfo = GetPlaneUploadInfo(width, height);
    m_uUploadInfo = GetPlaneUploadInfo(chromaWidth, chromaHeight);
    m_vUploadInfo = GetPlaneUploadInfo(chromaWidth, chromaHeight);
    
    m_yRowPitch = m_yUploadInfo.footprint.Footprint.RowPitch;
    m_uRowPitch = m_uUploadInfo.footprint.Footprint.RowPitch;
    m_vRowPitch = m_vUploadInfo.footprint.Footprint.RowPitch;

    hr = CreateUploadBuffer(m_yUploadInfo.totalBytes, m_yUploadBuffer);
    if (FAILED(hr))
        return hr;

    hr = CreateUploadBuffer(m_uUploadInfo.totalBytes, m_uUploadBuffer);
    if (FAILED(hr))
        return hr;

    hr = CreateUploadBuffer(m_vUploadInfo.totalBytes, m_vUploadBuffer);
    if (FAILED(hr))
        return hr;

    m_videoTexturesInShaderState = false;

    return S_OK;
}

HRESULT CD3D12Renderer::CreatePlaneTexture(UINT width, UINT height, DXGI_FORMAT format, ComPtr<ID3D12Resource>& texture)
{
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    return m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture));
}

HRESULT CD3D12Renderer::CreateUploadBuffer(UINT64 size, ComPtr<ID3D12Resource>& buffer)
{
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    return m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer));
}

PlaneUploadInfo CD3D12Renderer::GetPlaneUploadInfo(UINT width, UINT height)
{
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    PlaneUploadInfo info{};
    m_device->GetCopyableFootprints(&desc, 0, 1, 0, &info.footprint, &info.numRows, &info.rowSize, &info.totalBytes);

    return info;
}

HRESULT CD3D12Renderer::CopyVideoPlanesToGpu()
{
    HRESULT hr = m_uploadCommandAllocator->Reset();
    if (FAILED(hr))
        return hr;

    hr = m_uploadCommandList->Reset(m_uploadCommandAllocator.Get(), nullptr);
    if (FAILED(hr))
        return hr;

    // Transition textures to COPY_DEST before updating
    if (m_videoTexturesInShaderState)
    {
        D3D12_RESOURCE_BARRIER barriers[3]{};

        ID3D12Resource* textures[] =
        {
            m_yTexture.Get(),
            m_uTexture.Get(),
            m_vTexture.Get()
        };

        for (UINT i = 0; i < 3; ++i)
        {
            barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[i].Transition.pResource = textures[i];
            barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        }

        m_uploadCommandList->ResourceBarrier(3, barriers);
    }

    // Copy Y, U and V planes
    CopyPlaneToTexture(m_uploadCommandList.Get(), m_yUploadBuffer.Get(), m_yTexture.Get(), m_yUploadInfo.footprint);
    CopyPlaneToTexture(m_uploadCommandList.Get(), m_uUploadBuffer.Get(), m_uTexture.Get(), m_uUploadInfo.footprint);
    CopyPlaneToTexture(m_uploadCommandList.Get(), m_vUploadBuffer.Get(), m_vTexture.Get(), m_vUploadInfo.footprint);

    // Transition textures to PIXEL_SHADER_RESOURCE
    D3D12_RESOURCE_BARRIER barriers[3]{};

    ID3D12Resource* textures[] =
    {
        m_yTexture.Get(),
        m_uTexture.Get(),
        m_vTexture.Get()
    };

    for (UINT i = 0; i < 3; ++i)
    {
        barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition.pResource = textures[i];
        barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    m_uploadCommandList->ResourceBarrier(3, barriers);

    hr = m_uploadCommandList->Close();
    if (FAILED(hr))
        return hr;

    ID3D12CommandList* lists[] = { m_uploadCommandList.Get()};

    m_commandQueue->ExecuteCommandLists(1, lists);

    m_videoTexturesInShaderState = true;

    // Signal GPU
    m_uploadFenceValue = SignalGpu();
    if (m_uploadFenceValue == 0)
        return E_FAIL;

    return S_OK;
}

HRESULT CD3D12Renderer::CreateVideoSrvHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 3;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_videoSrvHeap));
    if (FAILED(hr))
        return hr;

    m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    return S_OK;
}

void CD3D12Renderer::CreateVideoSrvs()
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Format = DXGI_FORMAT_R8_UNORM;

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_videoSrvHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateShaderResourceView(m_yTexture.Get(), &srvDesc, handle);

    handle.ptr += m_srvDescriptorSize;
    m_device->CreateShaderResourceView(m_uTexture.Get(), &srvDesc, handle);

    handle.ptr += m_srvDescriptorSize;
    m_device->CreateShaderResourceView(m_vTexture.Get(), &srvDesc, handle);
}

HRESULT CD3D12Renderer::CreateRootSignature()
{
    // 1. Define SRV Descriptor Range.
    D3D12_DESCRIPTOR_RANGE srvRange{};

    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 3;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // 2. Define Root Parameter.
    D3D12_ROOT_PARAMETER rootParameter{};
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter.DescriptorTable.NumDescriptorRanges = 1;
    rootParameter.DescriptorTable.pDescriptorRanges = &srvRange;
    rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 3. Define Static Sampler.
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 4. Define Root Signature.
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};

    rootSignatureDesc.NumParameters = 1;
    rootSignatureDesc.pParameters = &rootParameter;

    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers = &sampler;

    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // 5. Serialize Root Signature
    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
        }

        return hr;
    }

    // 6. Create Root Signature
    return m_device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));
}

HRESULT CD3D12Renderer::CreatePipelineState()
{
    // 1. Compile shaders
    HRESULT hr = CompileShader(L"Shaders\\VideoVS.hlsl", "VSMain","vs_5_1", m_vertexShader);
    if (FAILED(hr))
        return hr;

    hr = CompileShader(L"Shaders\\VideoPS.hlsl", "PSMain", "ps_5_1", m_pixelShader);
    if (FAILED(hr))
        return hr;

    // 2. Initialize PSO description
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS.pShaderBytecode = m_vertexShader->GetBufferPointer();
    psoDesc.VS.BytecodeLength = m_vertexShader->GetBufferSize();
    psoDesc.PS.pShaderBytecode = m_pixelShader->GetBufferPointer();
    psoDesc.PS.BytecodeLength = m_pixelShader->GetBufferSize();

    // 3. Blend State
    D3D12_BLEND_DESC& blendDesc = psoDesc.BlendState;
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;

    D3D12_RENDER_TARGET_BLEND_DESC& rtBlend = blendDesc.RenderTarget[0];
    rtBlend.BlendEnable = FALSE;
    rtBlend.LogicOpEnable = FALSE;
    rtBlend.SrcBlend = D3D12_BLEND_ONE;
    rtBlend.DestBlend = D3D12_BLEND_ZERO;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // 4. Rasterizer State
    D3D12_RASTERIZER_DESC& rasterizerDesc = psoDesc.RasterizerState;
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    rasterizerDesc.FrontCounterClockwise = FALSE;
    rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizerDesc.DepthClipEnable = TRUE;
    rasterizerDesc.MultisampleEnable = FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;
    rasterizerDesc.ForcedSampleCount = 0;
    rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // 5. Depth / Stencil State
    D3D12_DEPTH_STENCIL_DESC& depthStencilDesc = psoDesc.DepthStencilState;
    depthStencilDesc.DepthEnable = FALSE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depthStencilDesc.StencilEnable = FALSE;
    depthStencilDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depthStencilDesc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

    // 6. Input Layout
    psoDesc.InputLayout.pInputElementDescs = nullptr;
    psoDesc.InputLayout.NumElements = 0;

    // 7. Primitive Topology
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // 8. Render Target
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

    // 9. Multisampling
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    // 10. Other settings
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.NodeMask = 0;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    // 11. Create PSO
    return m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));
}

HRESULT CD3D12Renderer::CompileShader(const std::wstring& filename, const char* entryPoint, const char* target, ComPtr<ID3DBlob>& shaderBlob)
{
    UINT compileFlags = 0;

#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG;
    compileFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompileFromFile(filename.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, target, compileFlags, 0, &shaderBlob, &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA(
                static_cast<const char*>(errorBlob->GetBufferPointer()));
        }

        return hr;
    }

    return S_OK;
}

UINT64 CD3D12Renderer::SignalGpu()
{
    const UINT64 fenceValue = m_nextFenceValue++;

    HRESULT hr = m_commandQueue->Signal(m_fence.Get(), fenceValue);
    if (FAILED(hr))
    {
        // Handle error appropriately.
        return 0;
    }

    return fenceValue;
}