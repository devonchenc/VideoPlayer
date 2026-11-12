#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include "CommandQueue.h"
#include "D3D12Renderer.h"
#include "PlayController.h"

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

    void Shutdown();

    void StartRendering();
    void StopRendering();

private:
    HRESULT Play();
    HRESULT Pause();
    HRESULT Resume();
    HRESULT Stop();

    void RenderThread(std::stop_token stopToken);
    void ProcessPendingCommands(std::stop_token stopToken);
    void ProcessVideoFrames();
    void ProcessCommand(const PlayerCommand& command);

private:
    std::mutex m_mutex;
    std::jthread m_renderThread;

    CCommandQueue<PlayerCommand> m_commandQueue;
    std::atomic<PlaybackState> m_playbackState{ PlaybackState::Stopped };

    HWND m_hwnd{ nullptr };
    RECT m_windowPos{};
    bool m_initialized{ false };
    std::unique_ptr<CD3D12Renderer> m_renderer;
    std::unique_ptr<CPlayController> m_controller;
};
