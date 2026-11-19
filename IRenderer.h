#pragma once

#include <Windows.h>

struct AVFrame;

class IRenderer
{
public:
    virtual ~IRenderer() = default;

    virtual HRESULT Initialize(HWND hwnd) = 0;
    virtual HRESULT Resize(UINT width, UINT height) = 0;
    virtual HRESULT UploadVideoFrame(AVFrame* frame) = 0;
    virtual HANDLE GetFrameLatencyWaitableObject() const = 0;
    virtual HRESULT Render() = 0;
    virtual HRESULT Clear() = 0;
    virtual void Shutdown() = 0;
};