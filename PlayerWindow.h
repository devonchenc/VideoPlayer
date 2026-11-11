#pragma once

#include <Windows.h>

class CPlayerWindow
{
public:
    bool Create(HWND parentWnd);
    void Destroy();
    HRESULT Resize(UINT width, UINT height);
    HWND GetHandle() const noexcept { return m_hwnd; }

private:
    HWND m_hwnd{ nullptr};
};