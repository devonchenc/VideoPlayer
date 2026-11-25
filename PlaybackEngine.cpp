#include "PlaybackEngine.h"

#include <algorithm>
#include <cmath>
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

CPlaybackEngine::CPlaybackEngine()
{
}

CPlaybackEngine::~CPlaybackEngine()
{
    Shutdown();
}

bool CPlaybackEngine::Initialize()
{
    Shutdown();

    m_decodedFrame.reset(av_frame_alloc());
    if (!m_decodedFrame)
        return false;

    if (!m_videoFrameEvent)
    {
        m_videoFrameEvent.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));
        if (!m_videoFrameEvent)
            return HRESULT_FROM_WIN32(GetLastError());
    }

    if (!m_stateChangedEvent)
    {
        m_stateChangedEvent.reset(CreateEvent(nullptr, FALSE, FALSE, nullptr));
        if (!m_stateChangedEvent)
            return HRESULT_FROM_WIN32(GetLastError());
    }

    m_videoPacketQueue.Reset();
    m_videoFrameQueue.Reset();

    m_videoNextPts = 0.0;
    m_videoNextPtsValid = false;

    m_videoClockStarted = false;

    return true;
}

void CPlaybackEngine::Shutdown()
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
}

bool CPlaybackEngine::PlayLive(const std::string& url)
{
    if (!Initialize())
        return false;

    {
        std::lock_guard lock(m_membersMutex);

        m_url = url;
        m_streamType = StreamType::Live;
        m_playbackState = PlaybackState::Playing;
    }

    SetEvent(m_stateChangedEvent.get());

    StartThreads();

    return true;
}

bool CPlaybackEngine::PlayRecorded(const std::string& url)
{
    if (!Initialize())
        return false;

    {
        std::lock_guard lock(m_membersMutex);

        m_url = url;
        m_streamType = StreamType::Recorded;
        m_playbackState = PlaybackState::Playing;
    }

    SetEvent(m_stateChangedEvent.get());

    StartThreads();

    return true;
}

void CPlaybackEngine::PlayStop()
{
    m_playbackState = PlaybackState::Stopped;
    SetEvent(m_stateChangedEvent.get());
}

void CPlaybackEngine::PlayPause()
{
    if (m_streamType == StreamType::Live)
    {
        m_videoFrameQueue.SetDropOldestWhenFull(true);
    }
    else
    {
        PauseMasterClock();
    }

    m_playbackState = PlaybackState::Paused;
    SetEvent(m_stateChangedEvent.get());
}

void CPlaybackEngine::PlayResume()
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

    m_playbackState = PlaybackState::Playing;
    SetEvent(m_stateChangedEvent.get());
}

void CPlaybackEngine::StartThreads()
{
    m_stopSource = std::stop_source{};
    const std::stop_token token = m_stopSource.get_token();

    m_demuxThread = std::jthread([this, token]() { DemuxThread(token); });
    m_videoDecodeThread = std::jthread([this, token]() { VideoDecodeThread(token); });
}

void CPlaybackEngine::DemuxThread(std::stop_token stopToken)
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

void CPlaybackEngine::VideoDecodeThread(std::stop_token stopToken)
{
    PacketPtr packet;

    while (!stopToken.stop_requested() && m_videoPacketQueue.Pop(packet, stopToken))
    {
        if (!DecodeVideoPacket(packet.get(), stopToken))
        {
            OutputDebugString(L"Video decoding failed.\n");
            break;
        }
        packet.reset();
    }

    if (!stopToken.stop_requested())
        FlushVideoDecoder(stopToken);

    SetEvent(m_videoFrameEvent.get());
}

bool CPlaybackEngine::InitializeFFmpeg(const std::string& url)
{
    AVFormatContext* rawFormat = avformat_alloc_context();
    if (!rawFormat)
    {
        OutputDebugString(L"avformat_alloc_context failed.\n");
        return false;
    }

    rawFormat->interrupt_callback.callback = &CPlaybackEngine::InterruptCallback;
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
        OutputDebugString(L"The input has no decodable video stream.\n");
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

bool CPlaybackEngine::InitializeVideoDecoder()
{
    const AVCodec* codec = avcodec_find_decoder(m_videoAVStream->codecpar->codec_id);
    if (!codec)
    {
        OutputDebugString(L"Video decoder not found.\n");
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

/* static */int CPlaybackEngine::InterruptCallback(void* opaque)
{
    std::stop_token* token = static_cast<std::stop_token*>(opaque);
    return token->stop_requested() ? 1 : 0;
}

void CPlaybackEngine::LogFFmpegError(const char* operation, int error)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(error, buffer, sizeof(buffer));
    std::string str = std::format("{} failed: {}\n", operation, buffer);
    OutputDebugStringA(str.c_str());
}

bool CPlaybackEngine::DecodeVideoPacket(AVPacket* packet, std::stop_token stopToken)
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

bool CPlaybackEngine::ReceiveVideoFrames(std::stop_token stopToken)
{
    while (true)
    {
        av_frame_unref(m_decodedFrame.get());

        const int result = avcodec_receive_frame(m_videoCodecContext.get(), m_decodedFrame.get());
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
            return true;

        if (result < 0)
        {
            LogFFmpegError("avcodec_receive_frame(video)", result);
            return false;
        }

        FramePtr rgbFrame(av_frame_alloc());
        if (!rgbFrame)
            return false;

        if (!ConvertFrameToBGRA(m_decodedFrame.get(), rgbFrame.get()))
            return false;

        double pts = NormalizeTimestamp(m_decodedFrame->best_effort_timestamp, m_videoAVStream->time_base);
        AVRational frameRate = av_guess_frame_rate(m_formatContext.get(), m_videoAVStream, m_decodedFrame.get());

        double frameDuration = DEFAULT_VIDEO_FRAME_RATE;
        if (frameRate.num > 0 && frameRate.den > 0)
            frameDuration = av_q2d(av_inv_q(frameRate));

        if (!IsValidTimestamp(pts))
            pts = m_videoNextPtsValid ? m_videoNextPts : 0.0;

        m_videoNextPts = pts + frameDuration;
        m_videoNextPtsValid = true;

        VideoFrame output;
        output.frame = std::move(rgbFrame);
        output.pts = pts;

        if (!m_videoFrameQueue.Push(std::move(output), stopToken))
            return false;

        SetEvent(m_videoFrameEvent.get());
    }
}

bool CPlaybackEngine::FlushVideoDecoder(std::stop_token stopToken)
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

double CPlaybackEngine::NormalizeTimestamp(std::int64_t timestamp, AVRational timeBase) const
{
    if (timestamp == AV_NOPTS_VALUE)
        return std::numeric_limits<double>::quiet_NaN();
    else
        return static_cast<double>(timestamp) * av_q2d(timeBase) - m_timestampOrigin;
}

double CPlaybackEngine::GetMasterClock() const
{
    if (!m_videoClockStarted)
        return 0.0;

    if (m_clockPaused)
        return m_pausedClock;

    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_videoClockBaseTime).count();

    return m_videoClockBasePts + elapsed;
}

void CPlaybackEngine::PauseMasterClock()
{
    if (m_clockPaused)
        return;

    m_pausedClock = GetMasterClock();
    m_clockPaused = true;
}

void CPlaybackEngine::ResumeMasterClock()
{
    if (!m_clockPaused)
        return;

    m_videoClockBasePts = m_pausedClock;
    m_videoClockBaseTime = std::chrono::steady_clock::now();
    m_clockPaused = false;
}

bool CPlaybackEngine::TryGetVideoFrame(VideoFrame& frame)
{
    bool result = m_videoFrameQueue.TryPop(frame);
    if (!result)
    {
        ResetEvent(m_videoFrameEvent.get());
        return false;
    }

    // Start video clock
    if (!m_videoClockStarted)
    {
        m_videoClockBasePts = frame.pts;
        m_videoClockBaseTime = std::chrono::steady_clock::now();
        m_videoClockStarted = true;
    }

    return true;
}

size_t CPlaybackEngine::GetMaxVideoFrameQueueSize() const
{
    AVRational frameRate = av_guess_frame_rate(m_formatContext.get(), m_videoAVStream, nullptr);

    if (frameRate.num <= 0 || frameRate.den <= 0)
        return DEFAULT_VIDEO_FRAME_QUEUE_SIZE;

    const double fps = av_q2d(frameRate);
    const double bufferSeconds = std::chrono::duration<double>(m_config.videoBufferDurationMs).count();

    return std::max<size_t>(1, static_cast<size_t>(std::ceil(fps * bufferSeconds)));
}

bool CPlaybackEngine::ConvertFrameToBGRA(AVFrame* srcFrame, AVFrame* rgbFrame)
{
    if (!srcFrame || !rgbFrame)
        return false;

    if (srcFrame->width <= 0 || srcFrame->height <= 0)
        return false;

    if (srcFrame->format == AV_PIX_FMT_NONE)
        return false;

    const int width = srcFrame->width;
    const int height = srcFrame->height;
    const AVPixelFormat srcFormat = static_cast<AVPixelFormat>(srcFrame->format);

    bool isFullRange = false;
    AVPixelFormat fixedSrcFormat = FixDeprecatedPixelFormat(srcFormat, isFullRange);

    // Update SwsContext if the source properties have changed
    SwsContext* newCtx = sws_getCachedContext(m_swsContext.get(), width, height, fixedSrcFormat, width, height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (newCtx != m_swsContext.get())
    {
        m_swsContext.release();
        m_swsContext.reset(newCtx);
    }

    if (!m_swsContext)
        return false;

    const int srcRange = isFullRange ? 1 : 0;
    const int* coefficients = sws_getCoefficients(SWS_CS_DEFAULT);
    int result = sws_setColorspaceDetails(m_swsContext.get(), coefficients, srcRange, coefficients, 1, 0, 1 << 16, 1 << 16);
    if (result < 0)
    {
        return false;
    }

    // Configure destination frame
    rgbFrame->format = AV_PIX_FMT_BGRA;
    rgbFrame->width = width;
    rgbFrame->height = height;

    // Allocate destination buffer
    if (av_frame_get_buffer(rgbFrame, 0) < 0)
        return false;

    // Convert source frame to BGRA
    const int outputHeight = sws_scale(m_swsContext.get(), srcFrame->data, srcFrame->linesize, 0, height, rgbFrame->data, rgbFrame->linesize);
    if (outputHeight != height)
    {
        av_frame_unref(rgbFrame);
        return false;
    }

    return true;
}

AVPixelFormat CPlaybackEngine::FixDeprecatedPixelFormat(AVPixelFormat format, bool& isFullRange) const
{
    isFullRange = false;

    switch (format)
    {
    case AV_PIX_FMT_YUVJ420P:
        isFullRange = true;
        return AV_PIX_FMT_YUV420P;
    default:
        return format;
    }
}
