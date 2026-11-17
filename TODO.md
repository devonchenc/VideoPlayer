# 1. Question

## 1.1 Playback commands such as Play, Pause, Resume, and Stop are handled by PlayController and do not need to be routed through the rendering command queue?

## 1.2 


# 2. TODO

## 2.1 使用 Frame pool 来实现

``` C++
DecodeThread()
{
    while (!stopToken.stop_requested())
    {
        // Decode YUV frame
        AVFrame* yuvFrame = ...;

        // Acquire RGB frame from pool
        FramePtr rgbFrame = m_rgbFramePool.Acquire();

        // YUV -> RGB
        ConvertYuvToRgb(yuvFrame, rgbFrame.get());

        // Push RGB frame to queue
        m_videoFrameQueue.Push(std::move(rgbFrame));
    }
}
```

## 2.2 将 Playback State 保存在 PlaybackEngine 中