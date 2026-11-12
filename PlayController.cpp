#include "PlayController.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
    constexpr size_t DEFAULT_VIDEO_FRAME_QUEUE_SIZE = 50;
    constexpr double DEFAULT_VIDEO_FRAME_RATE = 1.0 / 30.0;
    constexpr int AUDIO_SAMPLE_RATE = 48000;
    constexpr int AUDIO_CHANNELS = 2;
    constexpr int BYTES_PER_SAMPLE = 2;
    constexpr int BYTES_PER_AUDIO_FRAME = AUDIO_CHANNELS * BYTES_PER_SAMPLE;
    constexpr double EARLY_FRAME_THRESHOLD_SECONDS = 0.003;
    constexpr double LATE_FRAME_THRESHOLD_SECONDS = 0.500;

    bool IsValidTimestamp(double value)
    {
        return std::isfinite(value);
    }
}

CPlayController::~CPlayController()
{
    Stop();
}

void CPlayController::LogFFmpegError(const char* operation, int error)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(error, buffer, sizeof(buffer));
    std::cerr << operation << " failed: " << buffer << std::endl;
}

bool CPlayController::Initialize()
{
    Stop();

    m_videoPacketQueue.Reset();
    m_videoFrameQueue.Reset();

    m_stopping = false;
    m_videoDone = false;
    m_videoFrameEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    m_videoNextPts = 0.0;
    m_videoNextPtsValid = false;

    m_videoClockStarted = false;

    return true;
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
    rawFormat->interrupt_callback.opaque = this;

    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    // rw_timeout is in microseconds for FFmpeg protocols that support it
    av_dict_set(&options, "rw_timeout", "5000000", 0);

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

bool CPlayController::InitializeSDL()
{
    const int width = std::max(1, m_videoCodecContext->width);
    const int height = std::max(1, m_videoCodecContext->height);

    m_yuvFrame = av_frame_alloc();
    if (!m_yuvFrame)
        return false;

    m_yuvFrame->format = AV_PIX_FMT_YUV420P;
    m_yuvFrame->width = width;
    m_yuvFrame->height = height;

    const int result = av_frame_get_buffer(m_yuvFrame, 32);
    if (result < 0)
    {
        LogFFmpegError("av_frame_get_buffer", result);
        return false;
    }

    return true;
}

double CPlayController::NormalizeTimestamp(std::int64_t timestamp, AVRational timeBase) const
{
    if (timestamp == AV_NOPTS_VALUE)
        return std::numeric_limits<double>::quiet_NaN();
    else
        return static_cast<double>(timestamp) * av_q2d(timeBase) - m_timestampOrigin;
}

int CPlayController::InterruptCallback(void* opaque)
{
    const auto* player = static_cast<CPlayController*>(opaque);
    return player->m_stopping ? 1 : 0;
}

void CPlayController::StartThreads()
{
    m_demuxThread = std::jthread([this](std::stop_token token) { DemuxThread(token); });

    m_videoDecodeThread = std::jthread([this](std::stop_token token) { VideoDecodeThread(token); });
}

void CPlayController::Stop()
{
    m_stopping = true;

    m_demuxThread.request_stop();
    m_videoDecodeThread.request_stop();

    m_videoPacketQueue.Close();
    m_videoFrameQueue.Close();

    if (m_demuxThread.joinable())
        m_demuxThread.join();
    if (m_videoDecodeThread.joinable())
        m_videoDecodeThread.join();

    if (m_yuvFrame)
        av_frame_free(&m_yuvFrame);

    m_swsContext.reset();

    m_videoCodecContext.reset();
    m_formatContext.reset();

    m_videoAVStream = nullptr;
    m_videoStreamIndex = -1;

    m_videoClockStarted = false;
    m_videoDone = false;
}

void CPlayController::Pause()
{
    if (m_playbackState == PlaybackState::Stopped)
        return;

    if (m_playbackState.exchange(PlaybackState::Paused) == PlaybackState::Paused)
        return;

    if (m_streamType == StreamType::Live)
    {
        m_videoFrameQueue.SetDropOldestWhenFull(true);
    }
}

void CPlayController::Resume()
{
    if (m_playbackState != PlaybackState::Paused)
        return;

    if (m_streamType == StreamType::Live)
    {
        m_videoFrameQueue.Clear();
        m_videoFrameQueue.SetDropOldestWhenFull(false);
    }

    m_playbackState = PlaybackState::Playing;

    NotifyDataReady();
}

void CPlayController::DemuxThread(std::stop_token stopToken)
{
    while (!stopToken.stop_requested() && !m_stopping)
    {
        PacketPtr packet(av_packet_alloc());
        if (!packet)
            break;

        const int result = av_read_frame(m_formatContext.get(), packet.get());
        if (result < 0)
        {
            if (result != AVERROR_EOF && !m_stopping)
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

    if (!stopToken.stop_requested() && !m_stopping)
        FlushVideoDecoder(stopToken);

    m_videoDone = true;
    NotifyDataReady();
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
    if (m_stopping)
        return;

    // Notify Render Thread
    SetEvent(m_videoFrameEvent);
}

double CPlayController::GetMasterClock() const
{
    if (!m_videoClockStarted)
        return 0.0;

    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_videoClockBaseTime).count();

    return m_videoClockBasePts + elapsed;
}

bool CPlayController::TryGetVideoFrame(VideoFrame& frame)
{
    bool result = m_videoFrameQueue.TryPop(frame);

    if (result)
    {
        // Start video clock
        if (!m_videoClockStarted)
        {
            m_videoClockBasePts = frame.pts;
            m_videoClockBaseTime = std::chrono::steady_clock::now();
            m_videoClockStarted = true;
        }
    }
    else
    {
        ResetEvent(m_videoFrameEvent);
    }

    return result;
}

void CPlayController::RenderVideoFrame(AVFrame* frame)
{
    if (!frame || !m_yuvFrame)
        return;

    const int width = frame->width;
    const int height = frame->height;

    SwsContext* cachedContext = sws_getCachedContext(m_swsContext.release(), width, height, static_cast<AVPixelFormat>(frame->format),
        width, height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!cachedContext)
    {
        std::cerr << "sws_getCachedContext failed" << std::endl;
        return;
    }

    m_swsContext.reset(cachedContext);

    if (m_yuvFrame->width != width || m_yuvFrame->height != height)
    {
        av_frame_unref(m_yuvFrame);
        m_yuvFrame->format = AV_PIX_FMT_YUV420P;
        m_yuvFrame->width = width;
        m_yuvFrame->height = height;

        const int allocResult = av_frame_get_buffer(m_yuvFrame, 32);
        if (allocResult < 0)
        {
            LogFFmpegError("av_frame_get_buffer(resize)", allocResult);
            return;
        }
    }

    if (av_frame_make_writable(m_yuvFrame) < 0)
        return;

    std::uint8_t* destinationData[4] =
    {
        m_yuvFrame->data[0],
        m_yuvFrame->data[1],
        m_yuvFrame->data[2],
        nullptr
    };

    int destinationLinesize[4] =
    {
        m_yuvFrame->linesize[0],
        m_yuvFrame->linesize[1],
        m_yuvFrame->linesize[2],
        0
    };

    sws_scale(m_swsContext.get(), frame->data, frame->linesize, 0, height, destinationData, destinationLinesize);
}

bool CPlayController::PlayLive(const std::string& url)
{
    if (!Initialize())
        return false;

    if (!InitializeFFmpeg(url) || !InitializeSDL())
    {
        Stop();
        return false;
    }

    m_streamType = StreamType::Live;

    StartThreads();

    return true;
}

bool CPlayController::PlayRecorded(const std::string& url)
{
    if (!Initialize())
        return false;

    if (!InitializeFFmpeg(url) || !InitializeSDL())
    {
        Stop();
        return false;
    }

    m_streamType = StreamType::Recorded;

    StartThreads();

    return true;
}

void CPlayController::Play()
{
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
