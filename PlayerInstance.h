#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include "Utility.h"
#include "D3D12Renderer.h"
#include "PlayController.h"

enum class PlayerCommandType
{
    Resize,
    Play,
    Pause,
    Resume,
    Stop,
    Clear
};

struct PlayerCommand
{
    PlayerCommandType type{ PlayerCommandType::Pause };

    UINT width{ 0 };
    UINT height{ 0 };
};

enum class RenderWaitResult
{
    StopRequested,
    CommandReceived,
    VideoFrameAvailable,
    TimerExpired,
    FrameLatencyAvailable,
    Failed
};

class CPlayerInstance
{
public:
    CPlayerInstance();
    ~CPlayerInstance();

    CPlayerInstance(const CPlayerInstance&) = delete;
    CPlayerInstance(CPlayerInstance&&) = delete;
    CPlayerInstance& operator=(const CPlayerInstance&) = delete;
    CPlayerInstance& operator=(CPlayerInstance&&) = delete;

public:
    HRESULT Initialize(HWND parentWnd);
    HRESULT SetWindowPos(RECT newWindowPos);
    HRESULT GetWindowPos(RECT* windowPos);
    HRESULT PlayLive(long cameraNumber, long playSpeed);
    HRESULT PlayStop();
    HRESULT PlayPause();
    HRESULT PlayResume();

    void Shutdown();

    void StartRendering();
    void StopRendering();

private:
    void RenderThread(std::stop_token stopToken);
    void ProcessPendingCommands(std::stop_token stopToken);
    void ProcessVideoFrames(std::stop_token stopToken);
    DWORD CalculateNextWakeUpTimeout() const;
    RenderWaitResult WaitForRenderEvents(DWORD timeoutMs, bool waitForFrameLatency);

private:
    std::mutex m_mutex;
    std::jthread m_renderThread;

    std::mutex m_commandMutex;
    std::queue<PlayerCommand> m_commandQueue;
    std::atomic<PlaybackState> m_playbackState{ PlaybackState::Stopped };

    HWND m_hwnd{ nullptr };
    RECT m_windowPos{};
    bool m_initialized{ false };
    std::unique_ptr<CD3D12Renderer> m_renderer;
    std::unique_ptr<CPlayController> m_controller;

    std::optional<VideoFrame> m_pendingVideoFrame;
    std::atomic<uint64_t> m_renderedFrameCount{ 0 };
    std::uint64_t m_lastRenderedFrameCount{ 0 };

    HANDLE m_commandEvent{ nullptr };
    HANDLE m_stopEvent{ nullptr };
    HANDLE m_renderTimer{ nullptr };
};
