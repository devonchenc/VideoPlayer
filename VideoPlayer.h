#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>
#include "PlayerWindow.h"
#include "D3D12Renderer.h"

class CVideoPlayer
{
public:
    CVideoPlayer() = default;
    ~CVideoPlayer();

    CVideoPlayer(const CVideoPlayer&) = delete;
    CVideoPlayer(CVideoPlayer&&) = delete;
    CVideoPlayer& operator=(const CVideoPlayer&) = delete;
    CVideoPlayer& operator=(CVideoPlayer&&) = delete;

public:
    HRESULT Initialize(HWND parentWnd);
    HRESULT SetWindowPos(RECT newWindowPos);
    void Shutdown();

    void StartRendering();
    void StopRendering();

private:
    void RenderThread(std::stop_token stopToken);

private:
    std::mutex m_mutex;
    bool m_shutdown{ false };
    std::jthread m_renderThread;
    std::condition_variable_any m_cv;

    bool m_resizePending{ false };
    UINT m_pendingWidth{ 0 };
    UINT m_pendingHeight{ 0 };

    CPlayerWindow m_playerWindow;
    CD3D12Renderer m_renderer;
    RECT m_windowPos{};
};