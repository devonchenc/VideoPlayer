#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#define NOMINMAX
#include <Windows.h>
#include "ResourceUtils.h"
#include "BlockingQueue.h"

struct PlayerConfig
{
    std::chrono::milliseconds videoBufferDurationMs{ 1000 };
};

enum class SessionResult
{
    Success,
    ConnectionLost,
    InitializationFailed,
    Stopped
};

class CPlaybackEngine
{
public:
    CPlaybackEngine();
    ~CPlaybackEngine();

    CPlaybackEngine(const CPlaybackEngine&) = delete;
    CPlaybackEngine(CPlaybackEngine&&) = delete;
    CPlaybackEngine& operator=(const CPlaybackEngine&) = delete;
    CPlaybackEngine& operator=(CPlaybackEngine&&) = delete;

public:
    bool Initialize();
    void Shutdown();
    bool PlayLive(const std::string& url);
    bool PlayRecorded(const std::string& url);
    void PlayStop();
    void PlayPause();
    void PlayResume();

    bool TryGetVideoFrame(VideoFrame& frame);
    double GetMasterClock() const;
    HANDLE GetVideoFrameEvent() const { return m_videoFrameEvent.get(); }
    HANDLE GetStateChangedEvent() const { return m_stateChangedEvent.get(); }
    PlaybackState GetPlaybackState() const { return m_playbackState; }

private:
    void StartThreads();
    void DemuxThread(std::stop_token stopToken);
    SessionResult RunSession(std::stop_token stopToken);
    void CleanupSession();
    std::chrono::seconds GetReconnectDelay(unsigned int attempt);
    bool WaitForReconnect(std::stop_token stopToken, std::chrono::seconds delay);
    void VideoDecodeThread(std::stop_token stopToken);
    bool OpenInputStream(const std::string& url);
    bool OpenVideoDecoder();
    bool DecodeVideoPacket(AVPacket* packet, std::stop_token stopToken);
    bool ReceiveVideoFrames(std::stop_token stopToken);
    bool FlushVideoDecoder(std::stop_token stopToken);
    double NormalizeTimestamp(std::int64_t timestamp, AVRational timeBase) const;
    void PauseMasterClock();
    void ResumeMasterClock();

    static int InterruptCallback(void* opaque);
    static void LogFFmpegError(const char* operation, int error);
    size_t GetMaxVideoFrameQueueSize() const;
    bool ConvertFrameToBGRA(AVFrame* srcFrame, AVFrame* rgbFrame);
    AVPixelFormat FixDeprecatedPixelFormat(AVPixelFormat format, bool& isFullRange) const;

private:
    // FFmpeg objects
    FormatPtr m_formatContext;
    CodecPtr m_videoCodecContext;
    SwsPtr m_swsContext{ nullptr };
    AVStream* m_videoAVStream{ nullptr };
    FramePtr m_decodedFrame;
    int m_videoStreamIndex{ -1 };
    double m_timestampOrigin{ 0.0 };

    HandlePtr m_videoFrameEvent{ nullptr };
    HandlePtr m_stateChangedEvent{ nullptr };

    CBlockingQueue<PacketPtr> m_videoPacketQueue{ 60 };
    CBlockingQueue<VideoFrame> m_videoFrameQueue{ 30 };

    std::mutex m_membersMutex;
    std::jthread m_demuxThread;
    std::jthread m_videoDecodeThread;
    std::stop_source m_stopSource;
    std::condition_variable_any m_reconnectCondition;
    std::mutex m_reconnectMutex;

    // Video decoder thread only
    double m_videoNextPts{ 0.0 };
    bool m_videoNextPtsValid{ false };

    // Pending video frame and video clock
    bool m_videoClockStarted{ false };
    double m_videoClockBasePts{ 0.0 };
    std::chrono::steady_clock::time_point m_videoClockBaseTime{};

    bool m_clockPaused{ false };
    double m_pausedClock{ 0.0 };

    PlayerConfig m_config;

    std::string m_url;
    StreamType m_streamType{ StreamType::Live };
    std::atomic<PlaybackState> m_playbackState{ PlaybackState::Stopped };
};
