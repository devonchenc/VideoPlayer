#pragma once

#include <memory>

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
