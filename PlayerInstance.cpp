#include "PlayerInstance.h"
#include "PlayerGlobalContext.h"
#include "D3D12Renderer.h"
#include "GdiRenderer.h"
#include "PlaybackEngine.h"

namespace
{
    constexpr double EARLY_FRAME_THRESHOLD_SECONDS = 0.003;
    constexpr double LATE_FRAME_THRESHOLD_SECONDS = 0.5;
}

CPlayerInstance::CPlayerInstance()
{
}

CPlayerInstance::~CPlayerInstance()
{
    m_running = false;

    if (m_stopEvent)
        SetEvent(m_stopEvent.get());

    if (m_renderThread.joinable())
        m_renderThread.join();

    if (m_hwnd)
        DestroyWindow(m_hwnd);
}

HRESULT CPlayerInstance::Initialize(HWND parentWnd)
{
    std::lock_guard lock(m_mutex);

    if (m_initialized)
        return S_OK;

    m_commandEvent.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));
    if (!m_commandEvent)
        return HRESULT_FROM_WIN32(GetLastError());

    m_stopEvent.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));
    if (!m_stopEvent)
        return HRESULT_FROM_WIN32(GetLastError());

    m_renderTimer.reset(CreateWaitableTimerEx(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE));
    if (!m_renderTimer)
        return HRESULT_FROM_WIN32(GetLastError());

    m_playbackEngine = std::make_unique<CPlaybackEngine>();
    if (!m_playbackEngine->Initialize())
        return E_FAIL;

    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    // Create child window
    m_hwnd = CreateWindowEx(0, L"VideoPlayerWindow", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 0, 0, parentWnd, nullptr, GetModuleHandleW(nullptr), this);

    if (m_hwnd == nullptr)
        return HRESULT_FROM_WIN32(GetLastError());

    if (CPlayerGlobalContext::GetInstance().IsD3D12Available())
    {
        m_renderer = std::make_unique<CD3D12Renderer>();
    }
    else
    {
        m_renderer = std::make_unique<CGdiRenderer>();
    }

    HRESULT hr = m_renderer->Initialize(m_hwnd);
    if (FAILED(hr))
        return hr;

    // Start rendering thread
    m_running = true;
    m_renderThread = std::thread(&CPlayerInstance::RenderThread, this);

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

    {
        std::lock_guard lock2(m_commandMutex);
        m_commandQueue.push({ PlayerCommandType::Resize, width, height });
        SetEvent(m_commandEvent.get());
    }

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
    m_playbackEngine->PlayRecorded(url);
    //const std::string url = "rtsp://127.0.0.1:8554/test";
    //m_playbackEngine->PlayLive(url);

    return S_OK;
}

HRESULT CPlayerInstance::PlayStop()
{
    m_playbackEngine->PlayStop();

    return S_OK;
}

HRESULT CPlayerInstance::PlayPause()
{
    m_playbackEngine->PlayPause();

    return S_OK;
}

HRESULT CPlayerInstance::PlayResume()
{
    m_playbackEngine->PlayResume();

    return S_OK;
}

void CPlayerInstance::RenderThread()
{
    while (m_running)
    {
        ProcessVideoFrames();

        if (!m_running)
            break;

        // Calculate the next wakeup time
        const DWORD timeoutMs = CalculateNextWakeUpTimeout();

        // Configure the render timer
        if (timeoutMs == INFINITE)
        {
            CancelWaitableTimer(m_renderTimer.get());
        }
        else
        {
            LARGE_INTEGER dueTime{};
            dueTime.QuadPart = -static_cast<LONGLONG>(timeoutMs) * 10000LL;

            if (!SetWaitableTimer(m_renderTimer.get(), &dueTime, 0, nullptr, nullptr, FALSE))
            {
                OutputDebugStringW(L"Failed to set waitable timer.\n");
                continue;
            }
        }

        constexpr DWORD count = 4;
        HANDLE handles[count]{};
        handles[0] = m_stopEvent.get();
        handles[1] = m_commandEvent.get();
        handles[2] = m_playbackEngine->GetStateChangedEvent();

        if (!m_pendingVideoFrame)
        {
            // Wait for a new video frame only when there is no pending frame
            handles[3] = m_playbackEngine->GetVideoFrameEvent();
        }
        else
        {
            handles[3] = m_renderTimer.get();
        }

        const DWORD result = WaitForMultipleObjects(count, handles, FALSE, INFINITE);
        switch (result)
        {
        case WAIT_OBJECT_0:
            m_running = false;
            break;
        case WAIT_OBJECT_0 + 1:
            ProcessPendingCommands();
            break;
        case WAIT_OBJECT_0 + 2:
            m_pendingVideoFrame.reset();
            break;
        case WAIT_OBJECT_0 + 3:
            break;
        default:
            OutputDebugStringW(L"Failed");
            continue;
        }
    }

    m_renderer->Shutdown();
}

void CPlayerInstance::ProcessVideoFrames()
{
    // Only process video frames while playing
    while (m_running && m_playbackEngine->GetPlaybackState() == PlaybackState::Playing)
    {
        // Get the next frame if there is no pending frame
        if (!m_pendingVideoFrame)
        {
            VideoFrame frame;
            if (!m_playbackEngine->TryGetVideoFrame(frame))
                return;

            m_pendingVideoFrame = std::move(frame);
        }

        const double videoClock = m_playbackEngine->GetVideoClock();
        const double difference = m_pendingVideoFrame->pts - videoClock;

        // Frame is too early
        if (difference > EARLY_FRAME_THRESHOLD_SECONDS)
        {
            return;
        }
        // Frame is too late
        else if (difference < -LATE_FRAME_THRESHOLD_SECONDS)
        {
            m_pendingVideoFrame.reset();
            continue;
        }

        const HRESULT hr = m_renderer->UploadVideoFrame(m_pendingVideoFrame->frame.get());
        if (FAILED(hr))
        {
            OutputDebugString(L"Failed to upload video frame.\n");
            m_pendingVideoFrame.reset();
            return;
        }

        m_renderer->Render();

        m_playbackEngine->UpdateVideoClock(m_pendingVideoFrame->pts);

        // Frame has been submitted for rendering
        m_pendingVideoFrame.reset();

        // Render at most one frame per invocation
        return;
    }
}

DWORD CPlayerInstance::CalculateNextWakeUpTimeout() const
{
    if (m_playbackEngine->GetPlaybackState() != PlaybackState::Playing)
        return INFINITE;

    if (!m_pendingVideoFrame)
        return INFINITE;

    const double difference = m_pendingVideoFrame->pts - m_playbackEngine->GetVideoClock();
    const double waitSeconds = difference - EARLY_FRAME_THRESHOLD_SECONDS;
    if (waitSeconds <= 0.0)
        return 0;

    return static_cast<DWORD>(std::ceil(waitSeconds * 1000.0));
}

void CPlayerInstance::ProcessPendingCommands()
{
    std::queue<PlayerCommand> commands;

    {
        std::lock_guard lock(m_commandMutex);

        commands.swap(m_commandQueue);
        ResetEvent(m_commandEvent.get());
    }

    std::optional<SIZE> pendingResize;

    while (m_running && !commands.empty())
    {
        PlayerCommand command = std::move(commands.front());
        commands.pop();

        switch (command.type)
        {
        case PlayerCommandType::Resize:
            pendingResize = SIZE(command.width, command.height);
            break;
        case PlayerCommandType::Clear:
            break;
        default:
            break;
        }
    }

    // Apply the resize request
    if (pendingResize.has_value())
    {
        HRESULT hr = m_renderer->Resize(pendingResize.value().cx, pendingResize.value().cy);
        if (FAILED(hr))
        {
            OutputDebugStringW(L"Renderer resize failed\n");
        }
    }
}
