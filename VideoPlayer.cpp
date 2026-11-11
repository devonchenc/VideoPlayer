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

    // 3. Start rendering thread
    StartRendering();

    return S_OK;
}

HRESULT CVideoPlayer::SetWindowPos(RECT newWindowPos)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_shutdown)
        return E_ABORT;

    m_windowPos = newWindowPos;
    return m_renderer.Resize(m_windowPos.right - m_windowPos.left, m_windowPos.bottom - m_windowPos.top);
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
        m_renderer.Clear();

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

