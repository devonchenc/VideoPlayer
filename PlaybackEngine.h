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
#include "ResourceUtils.h"
#include "BlockingQueue.h"

struct PlayerConfig
{
    std::chrono::milliseconds videoBufferDurationMs{ 1000 };
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
    bool PlayLive(const std::string& url);
    bool PlayRecorded(const std::string& url);
    void PlayStop();
    void PlayPause();
    void PlayResume();
    void Shutdown();

    double GetMasterClock() const;
    bool TryGetVideoFrame(VideoFrame& frame);
    HANDLE GetVideoFrameEvent() const { return m_videoFrameEvent; }
    HANDLE GetStateChangedEvent() const { return m_stateChangedEvent; }
    PlaybackState GetPlaybackState() const { return m_playbackState; }

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

    bool ConvertFrameToBGRA(AVFrame* srcFrame, AVFrame* rgbFrame);
    static int InterruptCallback(void* opaque);
    static void LogFFmpegError(const char* operation, int error);
    size_t GetMaxVideoFrameQueueSize() const;

private:
    // FFmpeg objects
    FormatPtr m_formatContext;
    CodecPtr m_videoCodecContext;

    AVStream* m_videoAVStream{ nullptr };
    FramePtr m_decodedFrame;

    int m_videoStreamIndex{ -1 };
    double m_timestampOrigin{ 0.0 };

    HANDLE m_videoFrameEvent{ nullptr };
    HANDLE m_stateChangedEvent{ nullptr };

    // Queues
    CBlockingQueue<PacketPtr> m_videoPacketQueue{ 128 };
    CBlockingQueue<VideoFrame> m_videoFrameQueue{ 30 };

    SwsContext* m_swsContext{ nullptr };

    // Worker threads
    std::jthread m_demuxThread;
    std::jthread m_videoDecodeThread;
    std::stop_source m_stopSource;
    std::mutex m_mutex;

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

    PlayerConfig m_config;

    std::string m_url;
    StreamType m_streamType{ StreamType::Live };
    std::atomic<PlaybackState> m_playbackState{ PlaybackState::Stopped };
};
