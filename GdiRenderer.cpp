#include "GdiRenderer.h"
#include "ResourceUtils.h"

HRESULT CGdiRenderer::Initialize(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return E_INVALIDARG;

    m_hwnd = hwnd;

    return S_OK;
}

HRESULT CGdiRenderer::Resize(UINT width, UINT height)
{
    if (width == 0 || height == 0)
    {
        m_isMinimized = true;
        return S_OK;
    }

    m_isMinimized = false;

    if (width == m_windowWidth && height == m_windowHeight)
        return S_OK;

    m_windowWidth = width;
    m_windowHeight = height;

    return S_OK;
}

HRESULT CGdiRenderer::UploadVideoFrame(AVFrame* frame)
{
    if (m_isMinimized)
        return S_OK;

    if (!frame || !frame->data[0])
        return E_POINTER;

    if (frame->format != AV_PIX_FMT_BGRA)
        return E_INVALIDARG;

    const int width = frame->width;
    const int height = frame->height;
    const int srcStride = frame->linesize[0];
    const int dstStride = width * 4;

    if (srcStride < dstStride)
        return E_INVALIDARG;

    size_t bufferSize = static_cast<size_t>(dstStride) * height;
    m_videoBuffer.resize(bufferSize);

    // Copy BGRA data row by row
    for (int y = 0; y < height; ++y)
    {
        const uint8_t* src = frame->data[0] + y * srcStride;
        uint8_t* dst = m_videoBuffer.data() + y * dstStride;
        memcpy_s(dst, bufferSize, src, dstStride);
    }

    m_videoWidth = width;
    m_videoHeight = height;

    return S_OK;
}

HRESULT CGdiRenderer::Render()
{
    if (m_isMinimized)
        return S_OK;

    if (!m_hwnd)
        return E_FAIL;

    if (m_videoBuffer.empty())
        return S_FALSE;

    HDC hdc = GetDC(m_hwnd);
    if (!hdc)
        return HRESULT_FROM_WIN32(GetLastError());

    // Calculate destination rectangle while preserving aspect ratio
    const double scale = std::min(static_cast<double>(m_windowWidth) / m_videoWidth, static_cast<double>(m_windowHeight) / m_videoHeight);
    const int dstWidth = static_cast<int>(m_videoWidth * scale);
    const int dstHeight = static_cast<int>(m_videoHeight * scale);
    const int dstX = (m_windowWidth - dstWidth) / 2;
    const int dstY = (m_windowHeight - dstHeight) / 2;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_videoWidth;
    bmi.bmiHeader.biHeight = -static_cast<LONG>(m_videoHeight);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage = static_cast<DWORD>(m_videoBuffer.size());

    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);

    int result = StretchDIBits(hdc, dstX, dstY, dstWidth, dstHeight, 0, 0,
        m_videoWidth, m_videoHeight, m_videoBuffer.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);

    ReleaseDC(m_hwnd, hdc);

    if (result == GDI_ERROR || result == 0)
        return E_FAIL;

    return S_OK;
}

void CGdiRenderer::Shutdown()
{
    m_videoBuffer.clear();
}
