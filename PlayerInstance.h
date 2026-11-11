#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>
#include "CommandQueue.h"
#include "D3D12Renderer.h"

enum class PlayerCommandType
{
    Resize,
    Play,
    Pause,
    Resume,
    Stop
};

struct PlayerCommand
{
    PlayerCommandType type{ PlayerCommandType::Pause };

    UINT width{ 0 };
    UINT height{ 0 };
};

enum class PlaybackState
{
    Stopped,
    Playing,
    Paused
};

class CPlayerInstance
{
public:
    CPlayerInstance() = default;
    ~CPlayerInstance();

    CPlayerInstance(const CPlayerInstance&) = delete;
    CPlayerInstance(CPlayerInstance&&) = delete;
    CPlayerInstance& operator=(const CPlayerInstance&) = delete;
    CPlayerInstance& operator=(CPlayerInstance&&) = delete;

public:
    HRESULT Initialize(HWND parentWnd);
    HRESULT SetWindowPos(RECT newWindowPos);
    HRESULT Play();
    HRESULT Pause();
    HRESULT Resume();
    HRESULT Stop();

    void Shutdown();

    void StartRendering();
    void StopRendering();

private:
    void RenderThread(std::stop_token stopToken);

    void ProcessCommand(const PlayerCommand& command);

private:
    std::mutex m_mutex;
    std::jthread m_renderThread;
    std::condition_variable_any m_cv;

    CCommandQueue<PlayerCommand> m_commandQueue;
    std::atomic<PlaybackState> m_playbackState{ PlaybackState::Stopped };

    HWND m_hwnd{ nullptr };
    CD3D12Renderer m_renderer;
    RECT m_windowPos{};
};