#pragma once

#include <mutex>
#include <Windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>

using Microsoft::WRL::ComPtr;

class CPlayerGlobalContext
{
public:
    static CPlayerGlobalContext& GetInstance();

    CPlayerGlobalContext(const CPlayerGlobalContext&) = delete;
    CPlayerGlobalContext( CPlayerGlobalContext&&) = delete;
    CPlayerGlobalContext& operator=(const CPlayerGlobalContext&) = delete;
    CPlayerGlobalContext& operator=(CPlayerGlobalContext&&) = delete;

public:
    HRESULT Initialize();
    HRESULT Uninitialize();
    bool IsInitialized() const;

    ComPtr<IDXGIFactory6> GetFactory() const { return m_factory; }
    ComPtr<ID3D12Device> GetDevice() const { return m_device; }
    ComPtr<ID3D12CommandQueue> GetCommandQueue() const { return m_commandQueue; }
    IDXGIFactory6* GetDXGIFactory() const;

private:
    CPlayerGlobalContext();
    ~CPlayerGlobalContext();

private:
    HRESULT InitializeFFmpeg();
    HRESULT RegisterWindowClass();
    HRESULT InitializeD3D12();

private:
    mutable std::mutex m_mutex;
    bool m_initialized{ false };

    ComPtr<IDXGIFactory6> m_factory;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<IDXGIAdapter1> m_adapter;
};