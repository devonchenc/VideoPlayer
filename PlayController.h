#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "BlockingQueue.h"

struct PlayerConfig
{
    std::chrono::milliseconds videoBufferDurationMs{ 1000 };
};

enum class StreamType
{
    Live,
    Recorded
};

enum class PlaybackState
{
    Playing,
    Paused,
    Stopped
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

private:
    struct FormatDeleter
    {
        void operator()(AVFormatContext* p) const
        {
            if (p)
                avformat_close_input(&p);
        }
    };
    struct CodecDeleter
    {
        void operator()(AVCodecContext* p) const
        {
            if (p)
                avcodec_free_context(&p);
        }
    };
    struct PacketDeleter
    {
        void operator()(AVPacket* p) const
        {
            if (p)
                av_packet_free(&p);
        }
    };
    struct FrameDeleter
    {
        void operator()(AVFrame* p) const
        {
            if (p)
                av_frame_free(&p);
        }
    };
    struct SwsDeleter
    {
        void operator()(SwsContext* p) const
        {
            if (p)
                sws_freeContext(p);
        }
    };

    using FormatPtr = std::unique_ptr<AVFormatContext, FormatDeleter>;
    using CodecPtr = std::unique_ptr<AVCodecContext, CodecDeleter>;
    using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
    using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
    using SwsPtr = std::unique_ptr<SwsContext, SwsDeleter>;

    struct VideoFrame
    {
        FramePtr frame;
        double pts{ 0.0 }; // normalized seconds
    };

    bool InitializeFFmpeg(const std::string& url);
    bool InitializeVideoDecoder();
    bool InitializeSDL();

    void Play();
    void StartThreads();

    void DemuxThread(std::stop_token stopToken);
    void VideoDecodeThread(std::stop_token stopToken);

    bool DecodeVideoPacket(AVPacket* packet, std::stop_token stopToken);
    bool ReceiveVideoFrames(std::stop_token stopToken);
    bool FlushVideoDecoder(std::stop_token stopToken);

    void NotifyDataReady();
    void ScheduleVideoFrame();
    void RenderVideoFrame(AVFrame* frame);

    double NormalizeTimestamp(std::int64_t timestamp, AVRational timeBase) const;
    double GetMasterClock() const;
    int GetNextVideoWaitTimeoutMs() const;

    static int InterruptCallback(void* opaque);
    static void LogFFmpegError(const char* operation, int error);
    size_t GetMaxVideoFrameQueueSize() const;
    void PrintStatistics();

private:
    // FFmpeg objects
    FormatPtr m_formatContext;
    CodecPtr m_videoCodecContext;
    SwsPtr m_swsContext;

    AVStream* m_videoAVStream{ nullptr };

    int m_videoStreamIndex{ -1 };
    double m_timestampOrigin{ 0.0 };

    AVFrame* m_yuvFrame{ nullptr };

    std::atomic<bool> m_dataReadyEventPending{ false };

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
    std::optional<VideoFrame> m_pendingVideoFrame;
    bool m_videoClockStarted{ false };
    double m_videoClockBasePts{ 0.0 };
    std::chrono::steady_clock::time_point m_videoClockBaseTime{};

    // FPS stats
    std::chrono::steady_clock::time_point m_lastStatsTime{};
    std::atomic<uint64_t> m_decodedFrameCount{ 0 };
    std::atomic<uint64_t> m_renderedFrameCount{ 0 };
    std::uint64_t m_lastDecodedFrameCount{ 0 };
    std::uint64_t m_lastRenderedFrameCount{ 0 };

    PlayerConfig m_config;

    StreamType m_streamType{ StreamType::Live };
    std::atomic<PlaybackState> m_playbackState{ PlaybackState::Stopped };
};
