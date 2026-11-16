#include "PlayController.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
    constexpr size_t DEFAULT_VIDEO_FRAME_QUEUE_SIZE = 50;
    constexpr double DEFAULT_VIDEO_FRAME_RATE = 1.0 / 30.0;

    bool IsValidTimestamp(double value)
    {
        return std::isfinite(value);
    }
}

CPlayController::~CPlayController()
{
    Stop();
}

bool CPlayController::Initialize()
{
    Stop();

    m_videoPacketQueue.Reset();
    m_videoFrameQueue.Reset();

    m_videoDone = false;
    m_videoFrameEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    m_rgbFrame.reset(av_frame_alloc());

    m_videoNextPts = 0.0;
    m_videoNextPtsValid = false;

    m_videoClockStarted = false;

    return true;
}

bool CPlayController::PlayLive(const std::string& url)
{
    if (!Initialize())
        return false;

    m_url = url;
    m_streamType = StreamType::Live;

    StartThreads();

    return true;
}

bool CPlayController::PlayRecorded(const std::string& url)
{
    if (!Initialize())
        return false;

    m_url = url;
    m_streamType = StreamType::Recorded;

    StartThreads();

    return true;
}

void CPlayController::Stop()
{
    m_stopSource.request_stop();

    m_videoPacketQueue.Close();
    m_videoFrameQueue.Close();

    if (m_demuxThread.joinable())
        m_demuxThread.join();
    if (m_videoDecodeThread.joinable())
        m_videoDecodeThread.join();

    m_videoCodecContext.reset();
    m_formatContext.reset();

    m_videoAVStream = nullptr;
    m_videoStreamIndex = -1;

    m_videoClockStarted = false;
    m_videoDone = false;
}

void CPlayController::Pause()
{
    if (m_streamType == StreamType::Live)
    {
        m_videoFrameQueue.SetDropOldestWhenFull(true);
    }
    else
    {
        PauseMasterClock();
    }
}

void CPlayController::Resume()
{
    if (m_streamType == StreamType::Live)
    {
        m_videoFrameQueue.Clear();
        m_videoFrameQueue.SetDropOldestWhenFull(false);
    }
    else
    {
        ResumeMasterClock();
    }

    NotifyDataReady();
}

void CPlayController::StartThreads()
{
    m_stopSource = std::stop_source{};
    const std::stop_token token = m_stopSource.get_token();

    m_demuxThread = std::jthread([this, token]() { DemuxThread(token); });

    m_videoDecodeThread = std::jthread([this, token]() { VideoDecodeThread(token); });
}

void CPlayController::DemuxThread(std::stop_token stopToken)
{
    if (!InitializeFFmpeg(m_url))
    {
        m_stopSource.request_stop();
        return;
    }

    while (!stopToken.stop_requested())
    {
        PacketPtr packet(av_packet_alloc());
        if (!packet)
            break;

        const int result = av_read_frame(m_formatContext.get(), packet.get());
        if (result < 0)
        {
            if (result != AVERROR_EOF)
                LogFFmpegError("av_read_frame", result);
            break;
        }

        if (packet->stream_index == m_videoStreamIndex)
        {
            if (!m_videoPacketQueue.Push(std::move(packet), stopToken))
                break;
        }
    }

    m_videoPacketQueue.Close();
}

void CPlayController::VideoDecodeThread(std::stop_token stopToken)
{
    PacketPtr packet;

    while (!stopToken.stop_requested() && m_videoPacketQueue.Pop(packet, stopToken))
    {
        if (!DecodeVideoPacket(packet.get(), stopToken))
        {
            std::cerr << "Video decoding failed" << std::endl;
            break;
        }
        packet.reset();
    }

    if (!stopToken.stop_requested())
        FlushVideoDecoder(stopToken);

    m_videoDone = true;
    NotifyDataReady();
}

bool CPlayController::InitializeFFmpeg(const std::string& url)
{
    AVFormatContext* rawFormat = avformat_alloc_context();
    if (!rawFormat)
    {
        std::cerr << "avformat_alloc_context failed" << std::endl;
        return false;
    }

    rawFormat->interrupt_callback.callback = &CPlayController::InterruptCallback;
    rawFormat->interrupt_callback.opaque = &m_stopSource;

    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "timeout", "5000000", 0);

    const int openResult = avformat_open_input(&rawFormat, url.c_str(), nullptr, &options);
    av_dict_free(&options);

    if (openResult < 0)
    {
        LogFFmpegError("avformat_open_input", openResult);
        if (rawFormat)
            avformat_free_context(rawFormat);
        return false;
    }

    m_formatContext.reset(rawFormat);

    int result = avformat_find_stream_info(m_formatContext.get(), nullptr);
    if (result < 0)
    {
        LogFFmpegError("avformat_find_stream_info", result);
        return false;
    }

    m_videoStreamIndex = av_find_best_stream(m_formatContext.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_videoStreamIndex < 0)
    {
        std::cerr << "The input has no decodable video stream" << std::endl;
        return false;
    }

    m_videoAVStream = m_formatContext->streams[m_videoStreamIndex];

    // Use one common timestamp origin for stream
    if (m_formatContext->start_time != AV_NOPTS_VALUE)
    {
        m_timestampOrigin = static_cast<double>(m_formatContext->start_time) / AV_TIME_BASE;
    }
    else
    {
        double earliest = std::numeric_limits<double>::max();

        for (unsigned int i = 0; i < m_formatContext->nb_streams; ++i)
        {
            const AVStream* stream = m_formatContext->streams[i];
            if (stream->start_time == AV_NOPTS_VALUE)
                continue;

            const double start = static_cast<double>(stream->start_time) * av_q2d(stream->time_base);
            earliest = std::min(earliest, start);
        }

        m_timestampOrigin = (earliest == std::numeric_limits<double>::max()) ? 0.0 : earliest;
    }

    if (!InitializeVideoDecoder())
        return false;

    return true;
}

bool CPlayController::InitializeVideoDecoder()
{
    const AVCodec* codec = avcodec_find_decoder(m_videoAVStream->codecpar->codec_id);
    if (!codec)
    {
        std::cerr << "Video decoder not found" << std::endl;
        return false;
    }

    AVCodecContext* raw = avcodec_alloc_context3(codec);
    if (!raw)
        return false;

    m_videoCodecContext.reset(raw);

    int result = avcodec_parameters_to_context(m_videoCodecContext.get(), m_videoAVStream->codecpar);
    if (result < 0)
    {
        LogFFmpegError("avcodec_parameters_to_context(video)", result);
        return false;
    }

    m_videoCodecContext->thread_count = 0;
    m_videoCodecContext->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

    result = avcodec_open2(m_videoCodecContext.get(), codec, nullptr);
    if (result < 0)
    {
        LogFFmpegError("avcodec_open2(video)", result);
        return false;
    }

    // Initialize video frame queue capacity
    const size_t maxQueueSize = GetMaxVideoFrameQueueSize();
    m_videoFrameQueue.SetCapacity(maxQueueSize);

    return true;
}

/* static */int CPlayController::InterruptCallback(void* opaque)
{
    std::stop_token* token = static_cast<std::stop_token*>(opaque);
    return token->stop_requested() ? 1 : 0;
}

void CPlayController::LogFFmpegError(const char* operation, int error)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(error, buffer, sizeof(buffer));
    std::cerr << operation << " failed: " << buffer << std::endl;
}

bool CPlayController::DecodeVideoPacket(AVPacket* packet, std::stop_token stopToken)
{
    int result = avcodec_send_packet(m_videoCodecContext.get(), packet);
    if (result == AVERROR(EAGAIN))
    {
        if (!ReceiveVideoFrames(stopToken))
            return false;

        result = avcodec_send_packet(m_videoCodecContext.get(), packet);
    }

    if (result < 0)
    {
        LogFFmpegError("avcodec_send_packet(video)", result);
        return false;
    }

    return ReceiveVideoFrames(stopToken);
}

bool CPlayController::ReceiveVideoFrames(std::stop_token stopToken)
{
    while (true)
    {
        FramePtr frame(av_frame_alloc());
        if (!frame)
            return false;

        const int result = avcodec_receive_frame(m_videoCodecContext.get(), frame.get());
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
            return true;

        if (result < 0)
        {
            LogFFmpegError("avcodec_receive_frame(video)", result);
            return false;
        }

        double pts = NormalizeTimestamp(frame->best_effort_timestamp, m_videoAVStream->time_base);
        AVRational frameRate = av_guess_frame_rate(m_formatContext.get(), m_videoAVStream, frame.get());

        double frameDuration = DEFAULT_VIDEO_FRAME_RATE;
        if (frameRate.num > 0 && frameRate.den > 0)
            frameDuration = av_q2d(av_inv_q(frameRate));

        if (!IsValidTimestamp(pts))
            pts = m_videoNextPtsValid ? m_videoNextPts : 0.0;

        m_videoNextPts = pts + frameDuration;
        m_videoNextPtsValid = true;

        VideoFrame output;
        output.frame = std::move(frame);
        output.pts = pts;

        ++m_decodedFrameCount;

        if (!m_videoFrameQueue.Push(std::move(output), stopToken))
            return false;

        NotifyDataReady();
    }
}

bool CPlayController::FlushVideoDecoder(std::stop_token stopToken)
{
    int result = avcodec_send_packet(m_videoCodecContext.get(), nullptr);
    if (result == AVERROR(EAGAIN))
    {
        if (!ReceiveVideoFrames(stopToken))
            return false;

        result = avcodec_send_packet(m_videoCodecContext.get(), nullptr);
    }

    if (result < 0 && result != AVERROR_EOF)
    {
        LogFFmpegError("video decoder flush", result);
        return false;
    }

    return ReceiveVideoFrames(stopToken);
}

void CPlayController::NotifyDataReady()
{
    // Notify Render Thread
    SetEvent(m_videoFrameEvent);
}

double CPlayController::NormalizeTimestamp(std::int64_t timestamp, AVRational timeBase) const
{
    if (timestamp == AV_NOPTS_VALUE)
        return std::numeric_limits<double>::quiet_NaN();
    else
        return static_cast<double>(timestamp) * av_q2d(timeBase) - m_timestampOrigin;
}

double CPlayController::GetMasterClock() const
{
    if (!m_videoClockStarted)
        return 0.0;

    if (m_clockPaused)
        return m_pausedClock;

    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_videoClockBaseTime).count();

    return m_videoClockBasePts + elapsed;
}

void CPlayController::PauseMasterClock()
{
    if (m_clockPaused)
        return;

    m_pausedClock = GetMasterClock();
    m_clockPaused = true;
}

void CPlayController::ResumeMasterClock()
{
    if (!m_clockPaused)
        return;

    m_videoClockBasePts = m_pausedClock;
    m_videoClockBaseTime = std::chrono::steady_clock::now();
    m_clockPaused = false;
}

bool CPlayController::TryGetVideoFrame(VideoFrame& frame)
{
    VideoFrame yuvFrame;
    bool result = m_videoFrameQueue.TryPop(yuvFrame);
    if (result)
    {
        result = ConvertFrameToBGRA(yuvFrame.frame.get());
        if (result)
        {
            // Start video clock
            if (!m_videoClockStarted)
            {
                m_videoClockBasePts = yuvFrame.pts;
                m_videoClockBaseTime = std::chrono::steady_clock::now();
                m_videoClockStarted = true;
            }

            frame.frame = std::move(m_rgbFrame);
            frame.pts = yuvFrame.pts;

            return true;
        }
    }

    ResetEvent(m_videoFrameEvent);

    return result;
}

size_t CPlayController::GetMaxVideoFrameQueueSize() const
{
    AVRational frameRate = av_guess_frame_rate(m_formatContext.get(), m_videoAVStream, nullptr);

    if (frameRate.num <= 0 || frameRate.den <= 0)
        return DEFAULT_VIDEO_FRAME_QUEUE_SIZE;

    const double fps = av_q2d(frameRate);
    const double bufferSeconds = std::chrono::duration<double>(m_config.videoBufferDurationMs).count();

    return std::max<size_t>(1, static_cast<size_t>(std::ceil(fps * bufferSeconds)));
}

bool CPlayController::ConvertFrameToBGRA(AVFrame* srcFrame)
{
    m_rgbFrame.reset(av_frame_alloc());

    if (!srcFrame)
        return false;

    if (srcFrame->width <= 0 || srcFrame->height <= 0)
        return false;

    if (srcFrame->format == AV_PIX_FMT_NONE)
        return false;

    const int width = srcFrame->width;
    const int height = srcFrame->height;
    const auto srcFormat = static_cast<AVPixelFormat>(srcFrame->format);

    // Update SwsContext if the source properties have changed
    m_swsContext = sws_getCachedContext(m_swsContext, width, height, srcFormat, width, height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_swsContext)
        return false;

    // Release the previous frame buffer
    av_frame_unref(m_rgbFrame.get());

    // Configure destination frame
    m_rgbFrame->format = AV_PIX_FMT_BGRA;
    m_rgbFrame->width = width;
    m_rgbFrame->height = height;

    // Allocate destination buffer
    if (av_frame_get_buffer(m_rgbFrame.get(), 32) < 0)
        return false;

    // Convert source frame to BGRA
    const int outputHeight = sws_scale(m_swsContext, srcFrame->data, srcFrame->linesize, 0, height, m_rgbFrame->data, m_rgbFrame->linesize);
    if (outputHeight != height)
    {
        av_frame_unref(m_rgbFrame.get());
        return false;
    }

    return true;
}

void CPlayController::PrintStatistics()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    if (m_lastStatsTime.time_since_epoch().count() == 0)
    {
        m_lastStatsTime = now;
        return;
    }

    const double elapsed = std::chrono::duration<double>(now - m_lastStatsTime).count();

    if (elapsed < 1.0)
        return;

    const double decodedFps = (m_decodedFrameCount - m_lastDecodedFrameCount) / elapsed;

    std::cout << "\n========== Player Statistics ==========\n"
        << "Decoded FPS:  " << decodedFps << '\n'
        << "Video Queue Size:   " << m_videoFrameQueue.Size() << std::endl;

    m_lastDecodedFrameCount = m_decodedFrameCount;
    m_lastStatsTime = now;
}
