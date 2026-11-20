#pragma once

#include <cstdint>
#include <vector>
#include "IRenderer.h"

class CGdiRenderer : public IRenderer
{
public:
    HRESULT Initialize(HWND hwnd) override;
    HRESULT Resize(UINT width, UINT height) override;
    HRESULT UploadVideoFrame(AVFrame* frame) override;
    HRESULT Render() override;
    void Shutdown() override;

private:
    std::vector<std::uint8_t> m_videoBuffer;
};
