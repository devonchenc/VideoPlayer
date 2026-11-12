# 1. RenderThread

事件唤醒 + PTS Deadline

``` C++
void PlayerWindow::RenderThread(std::stop_token stopToken)
{
    while (!stopToken.stop_requested())
    {
        // 1. Process all pending commands
        ProcessPendingCommands();

        if (stopToken.stop_requested())
            break;

        // 2. Process video frames
        ProcessVideoFrames();

        // 3. Render only when necessary
        if (m_renderRequired)
        {
            m_renderer->Render();
            m_renderRequired = false;
        }

        // 4. Calculate next wakeup time
        auto nextWakeTime = GetNextPresentationTime();

        // 5. Wait for commands, video frames or deadline
        WaitForRenderEvent(nextWakeTime, stopToken);
    }
}
```

其中：
- ProcessPendingCommands()：处理所有待处理命令，而不是每次只处理一个。
- ProcessVideoFrames()：根据 PTS 和播放时钟决定显示哪一帧。
- m_renderRequired：有新帧、Resize 或其他需要重新绘制的事件时设置。
- GetNextPresentationTime()：计算下一帧的显示时间。
- WaitForRenderEvent()：等待命令、视频帧或者下一个显示时间。


对于目前第一版不处理音频的设计，可以先使用基于 std::chrono::steady_clock 的 Video Clock 来调度视频帧，后续加入音频时再切换到 Audio Master Clock。

# 2. Question

## 2.1 Playback commands such as Play, Pause, Resume, and Stop are handled by PlayController and do not need to be routed through the rendering command queue?

## 2.2 When play is paused, should D3D12 keep rendering?