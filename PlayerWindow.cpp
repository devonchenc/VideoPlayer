#include "PlayerWindow.h"

bool CPlayerWindow::Create(HWND parentWnd)
{
    RECT rc{};
    GetClientRect(parentWnd, &rc);

    m_hwnd = CreateWindowExW(
        0,
        L"VideoPlayerWindow",
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0,
        0,
        rc.right - rc.left,
        rc.bottom - rc.top,
        parentWnd,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    return m_hwnd != nullptr;
}

void CPlayerWindow::Destroy()
{
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}
