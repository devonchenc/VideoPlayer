#pragma once

#define NOMINMAX
#include <Windows.h>

struct AVFrame;

class IRenderer
{
public:
    virtual ~IRenderer() = default;

public:
    virtual HRESULT Initialize(HWND hwnd) = 0;
    virtual HRESULT Resize(UINT width, UINT height) = 0;
    virtual HRESULT UploadVideoFrame(AVFrame* frame) = 0;
    virtual HRESULT Render() = 0;
    virtual void Shutdown() = 0;

protected:
    HWND m_hwnd;
    UINT m_videoWidth{ 0 };
    UINT m_videoHeight{ 0 };
    UINT m_windowWidth{ 0 };
    UINT m_windowHeight{ 0 };
};
