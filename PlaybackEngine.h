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

class CPlaybackEngine
{
public:
    CPlaybackEngine() = default;
    ~CPlaybackEngine();

    CPlaybackEngine(const CPlaybackEngine&) = delete;
    CPlaybackEngine(CPlaybackEngine&&) = delete;
    CPlaybackEngine& operator=(const CPlaybackEngine&) = delete;
    CPlaybackEngine& operator=(CPlaybackEngine&&) = delete;

public:
    bool Initialize();
    bool PlayLive(const std::string& url);
    bool PlayRecorded(const std::string& url);
    void Stop();
    void Pause();
    void Resume();

    double GetMasterClock() const;
    bool TryGetVideoFrame(VideoFrame& frame);
    HANDLE GetVideoFrameEvent() const { return m_videoFrameEvent; }

private:
    void StartThreads();
    void DemuxThread(std::stop_token stopToken);
    void VideoDecodeThread(std::stop_token stopToken);

    bool InitializeFFmpeg(const std::string& url);
    bool InitializeVideoDecoder();

    bool DecodeVideoPacket(AVPacket* packet, std::stop_token stopToken);
    bool ReceiveVideoFrames(std::stop_token stopToken);
    bool FlushVideoDecoder(std::stop_token stopToken);
    void NotifyDataReady();

    double NormalizeTimestamp(std::int64_t timestamp, AVRational timeBase) const;
    void PauseMasterClock();
    void ResumeMasterClock();

    bool ConvertFrameToBGRA(AVFrame* srcFrame);
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

    // RGB frame
    SwsContext* m_swsContext{ nullptr };
    FramePtr m_rgbFrame{ nullptr };

    // Worker threads
    std::jthread m_demuxThread;
    std::jthread m_videoDecodeThread;
    std::stop_source m_stopSource;

    std::atomic<bool> m_videoDone{ false };

    // Video decoder thread only
    double m_videoNextPts{ 0.0 };
    bool m_videoNextPtsValid{ false };

    // Pending video frame and video clock
    bool m_videoClockStarted{ false };
    double m_videoClockBasePts{ 0.0 };
    std::chrono::steady_clock::time_point m_videoClockBaseTime{};

    bool m_clockPaused{ false };
    double m_pausedClock{ 0.0 };

    // FPS stats
    std::chrono::steady_clock::time_point m_lastStatsTime{};
    std::atomic<uint64_t> m_decodedFrameCount{ 0 };
    std::uint64_t m_lastDecodedFrameCount{ 0 };

    PlayerConfig m_config;

    std::string m_url;
    StreamType m_streamType{ StreamType::Live };
};
