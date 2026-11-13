#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#define NOMINMAX
#include <Windows.h>
#include "Utility.h"
#include "BlockingQueue.h"

struct PlayerConfig
{
    std::chrono::milliseconds videoBufferDurationMs{ 1000 };
};

class CPlayController
{
public:
    CPlayController() = default;
    ~CPlayController();

    CPlayController(const CPlayController&) = delete;
    CPlayController(CPlayController&&) = delete;
    CPlayController& operator=(const CPlayController&) = delete;
    CPlayController& operator=(CPlayController&&) = delete;

public:
    bool Initialize();
    bool PlayLive(const std::string& url);
    bool PlayRecorded(const std::string& url);
    void Stop();
    void Pause();
    void Resume();
    bool IsPaused() const { return m_playbackState == PlaybackState::Paused; }

    bool TryGetVideoFrame(VideoFrame& frame);
    HANDLE GetVideoFrameEvent() const { return m_videoFrameEvent; }
    double GetMasterClock() const;

private:

    bool InitializeFFmpeg(const std::string& url);
    bool InitializeVideoDecoder();
    bool InitializeSDL();

    void StartThreads();

    void DemuxThread(std::stop_token stopToken);
    void VideoDecodeThread(std::stop_token stopToken);

    bool DecodeVideoPacket(AVPacket* packet, std::stop_token stopToken);
    bool ReceiveVideoFrames(std::stop_token stopToken);
    bool FlushVideoDecoder(std::stop_token stopToken);
    void NotifyDataReady();

    double NormalizeTimestamp(std::int64_t timestamp, AVRational timeBase) const;

    static int InterruptCallback(void* opaque);
    static void LogFFmpegError(const char* operation, int error);
    size_t GetMaxVideoFrameQueueSize() const;
    void PrintStatistics();

private:
    // FFmpeg objects
    FormatPtr m_formatContext;
    CodecPtr m_videoCodecContext;

    AVStream* m_videoAVStream{ nullptr };

    int m_videoStreamIndex{ -1 };
    double m_timestampOrigin{ 0.0 };

    HANDLE m_videoFrameEvent{ nullptr };

    // Queues
    CBlockingQueue<PacketPtr> m_videoPacketQueue{ 128 };
    CBlockingQueue<VideoFrame> m_videoFrameQueue{ 30 };

    // Worker threads
    std::jthread m_demuxThread;
    std::jthread m_videoDecodeThread;

    std::atomic<bool> m_stopping{ false };
    std::atomic<bool> m_videoDone{ false };

    // Video decoder thread only
    double m_videoNextPts{ 0.0 };
    bool m_videoNextPtsValid{ false };

    // Main thread only: pending video frame and video clock
    bool m_videoClockStarted{ false };
    double m_videoClockBasePts{ 0.0 };
    std::chrono::steady_clock::time_point m_videoClockBaseTime{};

    // FPS stats
    std::chrono::steady_clock::time_point m_lastStatsTime{};
    std::atomic<uint64_t> m_decodedFrameCount{ 0 };
    std::uint64_t m_lastDecodedFrameCount{ 0 };

    PlayerConfig m_config;

    StreamType m_streamType{ StreamType::Live };
    std::atomic<PlaybackState> m_playbackState{ PlaybackState::Stopped };
};
