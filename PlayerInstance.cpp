#include "PlayerInstance.h"

CPlayerInstance::~CPlayerInstance()
{
    StopRendering();
    m_renderer.Shutdown();

    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

HRESULT CPlayerInstance::Initialize(HWND parentWnd)
{
    // Create child window
    m_hwnd = CreateWindowExW(0, L"VideoPlayerWindow", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0,
        parentWnd, nullptr, GetModuleHandleW(nullptr), this);
    if (m_hwnd == nullptr)
        return HRESULT_FROM_WIN32(GetLastError());

    // Initialize D3D12 renderer
    HRESULT hr = m_renderer.Initialize(m_hwnd);
    if (FAILED(hr))
        return hr;

    // Start rendering thread
    StartRendering();

    Play();

    return S_OK;
}

HRESULT CPlayerInstance::SetWindowPos(RECT newWindowPos)
{
    if (!m_hwnd || !IsWindow(m_hwnd))
        return E_HANDLE;

    UINT width = 0;
    UINT height = 0;

    {
        std::lock_guard lock(m_mutex);

        m_windowPos = newWindowPos;

        width = m_windowPos.right - m_windowPos.left;
        height = m_windowPos.bottom - m_windowPos.top;
    }

    if (!::SetWindowPos(m_hwnd, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE))
        return HRESULT_FROM_WIN32(GetLastError());

    m_commandQueue.Push({ PlayerCommandType::Resize, width, height });

    m_cv.notify_one();

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
        m_cv.notify_all();
        m_renderThread.join();
    }
}

void CPlayerInstance::RenderThread(std::stop_token stopToken)
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

void CPlayerInstance::ProcessCommand(const PlayerCommand& command)
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
