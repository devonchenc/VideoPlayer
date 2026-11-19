#include "PlayerGlobalContext.h"

extern "C"
{
#include <libavformat/avformat.h>
}

/* static */ CPlayerGlobalContext& CPlayerGlobalContext::GetInstance()
{
    static CPlayerGlobalContext context;
    return context;
}

CPlayerGlobalContext::~CPlayerGlobalContext()
{
    if (m_initialized)
    {
        avformat_network_deinit();
        m_initialized = false;
    }
}

HRESULT CPlayerGlobalContext::Initialize()
{
    std::lock_guard lock(m_mutex);

    if (m_initialized)
        return S_OK;

    HRESULT hr = RegisterWindowClass();
    if (FAILED(hr))
        return hr;

    hr = InitializeFFmpeg();
    if (FAILED(hr))
        return hr;

    hr = InitializeD3D12();
    if (FAILED(hr))
    {
        m_d3d12Available = false;
    }

    m_initialized = true;

    return S_OK;
}

HRESULT CPlayerGlobalContext::RegisterWindowClass()
{
    WNDCLASSEXW wndclass{};
    wndclass.cbSize = sizeof(wndclass);
    wndclass.style = CS_HREDRAW | CS_VREDRAW;
    wndclass.lpfnWndProc = PlayerWindowProc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = GetModuleHandle(nullptr);
    wndclass.lpszMenuName = NULL;
    wndclass.lpszClassName = L"VideoPlayerWindow";

    if (!RegisterClassEx(&wndclass))
    {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS)
            return HRESULT_FROM_WIN32(error);
    }

    return S_OK;
}

/* static */ LRESULT CALLBACK CPlayerGlobalContext::PlayerWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HRESULT CPlayerGlobalContext::InitializeFFmpeg()
{
    const int result = avformat_network_init();

    if (result < 0)
        return E_FAIL;

    return S_OK;
}

HRESULT CPlayerGlobalContext::InitializeD3D12()
{
    UINT factoryFlags = 0;

#ifdef _DEBUG
    // Enable D3D12 debug layer when available
    ComPtr<ID3D12Debug> debugController;

    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    // Create DXGI Factory
    HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory));
    if (FAILED(hr))
        return hr;

    // Select GPU Adapter
    for (UINT index = 0; ; ++index)
    {
        ComPtr<IDXGIAdapter1> adapter;
        hr = m_factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;

        if (FAILED(hr))
            return hr;

        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr);
        if (FAILED(hr))
            continue;

        m_adapter = adapter;
        break;
    }

    if (!m_adapter)
        return DXGI_ERROR_UNSUPPORTED;

    // Create shared D3D12 Device
    hr = D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));

    // Create command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;

    hr = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
    if (FAILED(hr))
        return hr;

    m_d3d12Available = true;

    return hr;
}
