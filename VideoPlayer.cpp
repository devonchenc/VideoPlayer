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

    Play();

    return S_OK;
}

HRESULT CVideoPlayer::SetWindowPos(RECT newWindowPos)
{
    UINT width = 0;
    UINT height = 0;

    {
        std::lock_guard lock(m_mutex);

        m_windowPos = newWindowPos;

        width = m_windowPos.right - m_windowPos.left;
        height = m_windowPos.bottom - m_windowPos.top;
    }

    HRESULT hr = m_playerWindow.Resize(width, height);
    if (FAILED(hr))
        return hr;

    m_commandQueue.Push({ PlayerCommandType::Resize, width, height });

    m_cv.notify_one();

    return S_OK;
}

HRESULT CVideoPlayer::Play()
{
    m_commandQueue.Push(PlayerCommand{ PlayerCommandType::Play });
    return S_OK;
}

HRESULT CVideoPlayer::Pause()
{
    m_commandQueue.Push(PlayerCommand{ PlayerCommandType::Pause });
    return S_OK;
}

HRESULT CVideoPlayer::Resume()
{
    m_commandQueue.Push(PlayerCommand{ PlayerCommandType::Resume });
    return S_OK;
}

HRESULT CVideoPlayer::Stop()
{
    m_commandQueue.Push(PlayerCommand{ PlayerCommandType::Stop });
    return S_OK;
}

void CVideoPlayer::Shutdown()
{
    StopRendering();

    //if (m_player)
    //{
    //    m_player->Stop();
    //    m_player.reset();
    //}

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
        m_cv.notify_all();
        m_renderThread.join();
    }
}

void CVideoPlayer::RenderThread(std::stop_token stopToken)
{
    while (!stopToken.stop_requested())
    {
        PlayerCommand command;

        // Wait for a command or a rendering timeout
        if (m_commandQueue.WaitPopFor(command, std::chrono::milliseconds(5), stopToken))
        {
            ProcessCommand(command);
        }

        if (stopToken.stop_requested())
            break;

        // Continue processing queued commands
        while (m_commandQueue.TryPop(command))
        {
            ProcessCommand(command);

            if (stopToken.stop_requested())
                break;
        }

        if (stopToken.stop_requested())
            break;

        if (m_playbackState == PlaybackState::Playing)
        {
            m_renderer.Render();
        }
    }

    m_renderer.Shutdown();
}

void CVideoPlayer::ProcessCommand(const PlayerCommand& command)
{
    switch (command.type)
    {
    case PlayerCommandType::Resize:
    {
        if (command.width == 0 || command.height == 0)
            break;

        HRESULT hr = m_renderer.Resize(command.width, command.height);
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
