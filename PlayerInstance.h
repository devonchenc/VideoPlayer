#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <Windows.h>
#include "ResourceUtils.h"

class IRenderer;
class CPlaybackEngine;

enum class PlayerCommandType
{
    Resize,
    Clear
};

struct PlayerCommand
{
    PlayerCommandType type{ PlayerCommandType::Clear };
    UINT width{ 0 };
    UINT height{ 0 };
};

enum class RenderWaitResult
{
    StopRequested,
    CommandReceived,
    PlaybackStateChanged,
    VideoFrameAvailable,
    TimerExpired,
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

private:
    void RenderThread(std::stop_token stopToken);
    void ProcessPendingCommands(std::stop_token stopToken);
    void ProcessVideoFrames(std::stop_token stopToken);
    DWORD CalculateNextWakeUpTimeout() const;
    RenderWaitResult WaitForRenderEvents(DWORD timeoutMs);

private:
    std::mutex m_mutex;
    std::jthread m_renderThread;

    std::mutex m_commandMutex;
    std::queue<PlayerCommand> m_commandQueue;

    HWND m_hwnd{ nullptr };
    RECT m_windowPos{};
    bool m_initialized{ false };
    std::unique_ptr<IRenderer> m_renderer;
    std::unique_ptr<CPlaybackEngine> m_playbackEngine;

    std::optional<VideoFrame> m_pendingVideoFrame;

    HANDLE m_commandEvent{ nullptr };
    HANDLE m_stopEvent{ nullptr };
    HANDLE m_renderTimer{ nullptr };
};
