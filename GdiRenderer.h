#pragma once

#include <cstdint>
#include <vector>
#include "IRenderer.h"

class CGdiRenderer : public IRenderer
{
public:
    virtual HRESULT Initialize(HWND hwnd);
    virtual HRESULT Resize(UINT width, UINT height);
    virtual HRESULT UploadVideoFrame(AVFrame* frame);
    virtual HANDLE GetFrameLatencyWaitableObject() const;
    virtual HRESULT Render();
    virtual HRESULT Clear();
    virtual void Shutdown();

private:
    std::vector<std::uint8_t> m_videoBuffer;
};
