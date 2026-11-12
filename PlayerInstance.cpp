#include "PlayerInstance.h"

namespace
{
    constexpr double EARLY_FRAME_THRESHOLD_SECONDS = 0.003;
    constexpr double LATE_FRAME_THRESHOLD_SECONDS = 0.500;
}

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

    m_commandEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_renderTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!m_commandEvent || !m_stopEvent || !m_renderTimer)
        return HRESULT_FROM_WIN32(GetLastError());

    // Start rendering thread
    StartRendering();

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
        SetEvent(m_commandEvent);
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
    m_controller->PlayLive(url);

    std::lock_guard lock(m_commandMutex);

    m_commandQueue.push(PlayerCommand{ PlayerCommandType::Play });
    SetEvent(m_commandEvent);

    return S_OK;
}

HRESULT CPlayerInstance::PlayStop()
{
    std::lock_guard lock(m_commandMutex);

    m_commandQueue.push(PlayerCommand{ PlayerCommandType::Stop });
    SetEvent(m_commandEvent);

    return S_OK;
}

HRESULT CPlayerInstance::PlayPause()
{
    std::lock_guard lock(m_commandMutex);

    m_commandQueue.push(PlayerCommand{ PlayerCommandType::Pause });
    SetEvent(m_commandEvent);

    return S_OK;
}

HRESULT CPlayerInstance::PlayResume()
{
    std::lock_guard lock(m_commandMutex);

    m_commandQueue.push(PlayerCommand{ PlayerCommandType::Resume });
    SetEvent(m_commandEvent);

    return S_OK;
}

void CPlayerInstance::Shutdown()
{
    SetEvent(m_stopEvent);

    StopRendering();
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
        ProcessVideoFrames(stopToken);

        if (stopToken.stop_requested())
            break;

        if (m_playbackState == PlaybackState::Playing)
        {
            m_renderer->Render();
        }

        // Calculate the next wake-up time
        const DWORD timeoutMs = CalculateNextWakeUpTimeout();
        // 5. Wait for DXGI when a frame is ready
        const bool waitForFrameLatency = (m_playbackState == PlaybackState::Playing) && m_pendingVideoFrame.has_value() && (timeoutMs == 0);

        const RenderWaitResult result = WaitForRenderEvents(timeoutMs, waitForFrameLatency);
        if (result == RenderWaitResult::StopRequested || result == RenderWaitResult::Failed)
            break;
    }

    m_renderer->Shutdown();
}

void CPlayerInstance::ProcessPendingCommands(std::stop_token stopToken)
{
    std::queue<PlayerCommand> commands;

    {
        std::lock_guard lock(m_commandMutex);

        commands.swap(m_commandQueue);
        ResetEvent(m_commandEvent);
    }

    std::optional<SIZE> pendingResize;

    while (!commands.empty())
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
        case PlayerCommandType::Play:
        {
            if (m_playbackState != PlaybackState::Stopped)
                break;

            m_playbackState = PlaybackState::Playing;
            break;
        }
        default:
            break;
        }

        if (stopToken.stop_requested())
            break;
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

void CPlayerInstance::ProcessVideoFrames(std::stop_token stopToken)
{
    // Only process video frames while playing
    if (m_playbackState != PlaybackState::Playing)
        return;

    static int count = 0;

    // Process frames until we find one that is ready to render.
    while (m_playbackState == PlaybackState::Playing)
    {
        // Get the next frame if there is no pending frame.
        if (!m_pendingVideoFrame)
        {
            VideoFrame frame;
            if (!m_controller->TryGetVideoFrame(frame))
                return;

            m_pendingVideoFrame = std::move(frame);
        }

        const double masterClock = m_controller->GetMasterClock();
        const double difference = m_pendingVideoFrame->pts - masterClock;

        // Frame is too early
        if (difference > EARLY_FRAME_THRESHOLD_SECONDS)
        {
            return;
        }
        // Frame is too late
        if (difference < -LATE_FRAME_THRESHOLD_SECONDS)
        {
            m_pendingVideoFrame.reset();
            continue;
        }

        // Render the frame
        const HRESULT hr = m_renderer->RenderFrame(m_pendingVideoFrame->frame.get());
        if (FAILED(hr))
        {
            // Handle rendering errors.
            //HandleRenderError(hr);

            m_pendingVideoFrame.reset();
            return;
        }

        // Frame has been submitted for rendering.
        m_pendingVideoFrame.reset();

        // Render at most one frame per invocation
        return;
    }
}

DWORD CPlayerInstance::CalculateNextWakeUpTimeout() const
{
    if (m_playbackState != PlaybackState::Playing)
        return INFINITE;

    if (!m_pendingVideoFrame)
        return INFINITE;

    const double difference = m_pendingVideoFrame->pts - m_controller->GetMasterClock();
    const double waitSeconds = difference - EARLY_FRAME_THRESHOLD_SECONDS;
    if (waitSeconds <= 0.0)
        return 0;

    return static_cast<DWORD>(std::ceil(waitSeconds * 1000.0));
}

RenderWaitResult CPlayerInstance::WaitForRenderEvents(DWORD timeoutMs, bool waitForFrameLatency)
{
    // Configure the render timer.
    if (timeoutMs == INFINITE || timeoutMs == 0)
    {
        CancelWaitableTimer(m_renderTimer);
    }
    else
    {
        LARGE_INTEGER dueTime{};

        dueTime.QuadPart = -static_cast<LONGLONG>(timeoutMs) * 10'000;

        if (!SetWaitableTimer(m_renderTimer, &dueTime, 0, nullptr, nullptr, FALSE))
        {
            return RenderWaitResult::Failed;
        }
    }

    HANDLE handles[5]{};
    DWORD count = 0;

    // Stop event has the highest priority
    handles[count++] = m_stopEvent;
    // Commands must be processed immediately
    handles[count++] = m_commandEvent;
    // Wait for a new video frame only when there is no pending frame
    if (!m_pendingVideoFrame)
    {
        handles[count++] = m_controller->GetVideoFrameEvent();
    }
    // Wait for DXGI to accept another frame
    if (waitForFrameLatency)
    {
        const HANDLE frameLatencyHandle = m_renderer->GetFrameLatencyWaitableObject();
        if (!frameLatencyHandle)
        {
            return RenderWaitResult::Failed;
        }

        handles[count++] = frameLatencyHandle;
    }

    if (timeoutMs != INFINITE && timeoutMs != 0)
    {
        handles[count++] = m_renderTimer;
    }

    const DWORD waitTimeout = (timeoutMs == 0) ? 0 : INFINITE;

    const DWORD result = WaitForMultipleObjects(count, handles, FALSE, INFINITE);

    if (result == WAIT_FAILED)
    {
        return RenderWaitResult::Failed;
    }
    else if (result == WAIT_TIMEOUT)
    {
        // Zero-timeout polling
        return RenderWaitResult::TimerExpired;
    }
    else if (result < WAIT_OBJECT_0 || result >= WAIT_OBJECT_0 + count)
    {
        return RenderWaitResult::Failed;
    }

    const DWORD index = result - WAIT_OBJECT_0;

    // Handle the signaled object
    if (index == 0)
    {
        return RenderWaitResult::StopRequested;
    }
    else if (index == 1)
    {
        return RenderWaitResult::CommandReceived;
    }

    DWORD nextIndex = 2;

    if (!m_pendingVideoFrame)
    {
        if (index == nextIndex)
        {
            return RenderWaitResult::VideoFrameAvailable;
        }

        ++nextIndex;
    }

    if (waitForFrameLatency)
    {
        if (index == nextIndex)
        {
            return RenderWaitResult::FrameLatencyAvailable;
        }

        ++nextIndex;
    }

    if (timeoutMs != INFINITE && timeoutMs != 0)
    {
        if (index == nextIndex)
        {
            return RenderWaitResult::TimerExpired;
        }
    }

    return RenderWaitResult::Failed;
}
