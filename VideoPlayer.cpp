#include "VideoPlayer.h"

CVideoPlayer::~CVideoPlayer()
{
    StopRendering();
    m_renderer.Shutdown();
    m_playerWindow.Destroy();
}

HRESULT CVideoPlayer::Initialize(HWND parentWnd)
{
    // Create child window
    if (!m_playerWindow.Create(parentWnd))
        return HRESULT_FROM_WIN32(GetLastError());

    // Initialize D3D12 renderer
    HRESULT hr = m_renderer.Initialize(m_playerWindow.GetHandle());
    if (FAILED(hr))
        return hr;

    // Start rendering thread
    StartRendering();

    return S_OK;
}

HRESULT CVideoPlayer::SetWindowPos(RECT newWindowPos)
{
    {
        std::lock_guard lock(m_mutex);

        if (m_shutdown)
            return E_ABORT;

        m_windowPos = newWindowPos;
        const UINT width = m_windowPos.right - m_windowPos.left;
        const UINT height = m_windowPos.bottom - m_windowPos.top;
        m_pendingWidth = width;
        m_pendingHeight = height;
        m_resizePending = true;

        HRESULT hr = m_playerWindow.Resize(width, height);
        if (FAILED(hr))
            return hr;
    }

    m_cv.notify_one();

    return S_OK;
}

void CVideoPlayer::Shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_shutdown)
        return;

    m_shutdown = true;

    //if (m_player)
    //{
    //    m_player->Stop();
    //    m_player.reset();
    //}

    m_renderer.Shutdown();
    m_playerWindow.Destroy();
}

void CVideoPlayer::StartRendering()
{
    m_renderThread = std::jthread([this](std::stop_token stopToken) { RenderThread(stopToken); });
}

void CVideoPlayer::StopRendering()
{
    if (m_renderThread.joinable())
    {
        m_renderThread.request_stop();
        m_renderThread.join();
    }
}

void CVideoPlayer::RenderThread(std::stop_token stopToken)
{
    while (!stopToken.stop_requested())
    {
        UINT width = 0;
        UINT height = 0;
        bool resizePending = false;

        {
            std::unique_lock lock(m_mutex);

            m_cv.wait_for(lock, stopToken, std::chrono::milliseconds(1), [this]{ return m_resizePending; });

            if (stopToken.stop_requested())
                break;

            if (m_resizePending)
            {
                width = m_pendingWidth;
                height = m_pendingHeight;

                m_resizePending = false;
                resizePending = true;
            }
        }

        if (resizePending)
        {
            // Window is minimized
            if (width == 0 || height == 0)
                continue;

            HRESULT hr = m_renderer.Resize(width, height);
            if (FAILED(hr))
                continue;
        }

        m_renderer.Render();

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

