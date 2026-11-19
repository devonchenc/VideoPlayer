#include "GdiRenderer.h"

HRESULT CGdiRenderer::Initialize(HWND hwnd)
{
    return S_OK;
}

HRESULT CGdiRenderer::Resize(UINT width, UINT height)
{
    return S_OK;
}

HRESULT CGdiRenderer::UploadVideoFrame(AVFrame* frame)
{
    return S_OK;
}

HANDLE CGdiRenderer::GetFrameLatencyWaitableObject() const
{
    return nullptr;
}

HRESULT CGdiRenderer::Render()
{
    return S_OK;
}

HRESULT CGdiRenderer::Clear()
{
    return S_OK;
}

void CGdiRenderer::Shutdown()
{

}