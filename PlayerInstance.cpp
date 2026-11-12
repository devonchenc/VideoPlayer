#include "PlayerInstance.h"

CPlayerInstance::CPlayerInstance()
{
    m_renderer = std::make_unique<CD3D12Renderer>();
    m_controller = std::make_unique< CPlayController>();
}

CPlayerInstance::~CPlayerInstance()
{
    StopRendering();
    m_renderer->Shutdown();

    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

HRESULT CPlayerInstance::Initialize(HWND parentWnd)
{
    std::lock_guard lock(m_mutex);

    if (m_initialized)
        return S_OK;

    // Create child window
    m_hwnd = CreateWindowExW(0, L"VideoPlayerWindow", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0,
        parentWnd, nullptr, GetModuleHandleW(nullptr), this);

    if (m_hwnd == nullptr)
        return HRESULT_FROM_WIN32(GetLastError());

    // Initialize D3D12 renderer
    HRESULT hr = m_renderer->Initialize(m_hwnd);
    if (FAILED(hr))
        return hr;

    if (!m_controller->Initialize())
        return E_FAIL;

    // Start rendering thread
    StartRendering();

    Play();

    m_initialized = true;

    return S_OK;
}

HRESULT CPlayerInstance::SetWindowPos(RECT newWindowPos)
{
    std::lock_guard lock(m_mutex);

    if (!m_initialized)
        return E_FAIL;

    m_windowPos = newWindowPos;

    UINT width = m_windowPos.right - m_windowPos.left;
    UINT height = m_windowPos.bottom - m_windowPos.top;

    if (!::SetWindowPos(m_hwnd, nullptr, m_windowPos.left, m_windowPos.top, width, height, SWP_NOZORDER | SWP_NOACTIVATE))
        return HRESULT_FROM_WIN32(GetLastError());

    m_commandQueue.Push({ PlayerCommandType::Resize, width, height });

    return S_OK;
}

HRESULT CPlayerInstance::GetWindowPos(RECT* windowPos)
{
    std::lock_guard lock(m_mutex);

    if (!m_initialized)
        return E_FAIL;

    *windowPos = m_windowPos;

    return S_OK;
}

HRESULT CPlayerInstance::PlayLive(long cameraNumber, long playSpeed)
{
    const std::string url = "qable-sample-720p-10s.mp4";
    m_controller->PlayLive(url);

    return S_OK;
}

HRESULT CPlayerInstance::PlayStop()
{
    return S_OK;
}

HRESULT CPlayerInstance::PlayPause()
{
    return S_OK;
}

HRESULT CPlayerInstance::Play()
{
    m_commandQueue.Push(PlayerCommand{ PlayerCommandType::Play });
    return S_OK;
}

HRESULT CPlayerInstance::Pause()
{
    m_commandQueue.Push(PlayerCommand{ PlayerCommandType::Pause });
    return S_OK;
}

HRESULT CPlayerInstance::Resume()
{
    m_commandQueue.Push(PlayerCommand{ PlayerCommandType::Resume });
    return S_OK;
}

HRESULT CPlayerInstance::Stop()
{
    m_commandQueue.Push(PlayerCommand{ PlayerCommandType::Stop });
    return S_OK;
}

void CPlayerInstance::Shutdown()
{
    StopRendering();

    //if (m_player)
    //{
    //    m_player->Stop();
    //    m_player.reset();
    //}
}

void CPlayerInstance::StartRendering()
{
    m_renderThread = std::jthread([this](std::stop_token stopToken) { RenderThread(stopToken); });
}

void CPlayerInstance::StopRendering()
{
    if (m_renderThread.joinable())
    {
        m_renderThread.request_stop();
        m_renderThread.join();
    }
}

void CPlayerInstance::RenderThread(std::stop_token stopToken)
{
    while (!stopToken.stop_requested())
    {
        // Process all pending commands
        ProcessPendingCommands(stopToken);

        if (stopToken.stop_requested())
            break;

        // Process video frames
        ProcessVideoFrames();

        if (stopToken.stop_requested())
            break;

        if (m_playbackState == PlaybackState::Playing)
        {
            m_renderer->Render();
        }
    }

    m_renderer->Shutdown();
}

void CPlayerInstance::ProcessPendingCommands(std::stop_token stopToken)
{
    PlayerCommand command;

    // Continue processing queued commands
    while (m_commandQueue.TryPop(command))
    {
        ProcessCommand(command);

        if (stopToken.stop_requested())
            break;
    }
}

void CPlayerInstance::ProcessCommand(const PlayerCommand& command)
{
    switch (command.type)
    {
    case PlayerCommandType::Resize:
    {
        if (command.width == 0 || command.height == 0)
            break;

        HRESULT hr = m_renderer->Resize(command.width, command.height);
        if (FAILED(hr))
        {
            OutputDebugStringW(L"Renderer resize failed\n");
        }
        break;
    }
    case PlayerCommandType::Play:
    {
        if (m_playbackState != PlaybackState::Stopped)
            break;

        m_playbackState = PlaybackState::Playing;
        break;
    }
    case PlayerCommandType::Pause:
    {
        if (m_playbackState != PlaybackState::Playing)
            break;

        m_playbackState = PlaybackState::Paused;

        // Notify playback controller
        //m_playbackController.Pause();

        break;
    }

    case PlayerCommandType::Resume:
    {
        if (m_playbackState != PlaybackState::Paused)
            break;

        //m_playbackController.Resume();

        m_playbackState = PlaybackState::Playing;

        break;
    }

    case PlayerCommandType::Stop:
    {
        if (m_playbackState == PlaybackState::Stopped)
            break;

        //m_playbackController.Stop();

        m_playbackState = PlaybackState::Stopped;

        break;
    }

    default:
        break;
    }
}

void CPlayerInstance::ProcessVideoFrames()
{

}
