#pragma once

#include "IRenderer.h"

class CGdiRenderer : public IRenderer
{
public:
    virtual ~CGdiRenderer() = default;

    virtual HRESULT Initialize(HWND hwnd);
    virtual HRESULT Resize(UINT width, UINT height);
    virtual HRESULT UploadVideoFrame(AVFrame* frame);
    virtual HANDLE GetFrameLatencyWaitableObject() const;
    virtual HRESULT Render();
    virtual HRESULT Clear();
    virtual void Shutdown();
};
