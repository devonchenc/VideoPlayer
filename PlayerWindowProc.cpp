#include "PlayerWindowProc.h"

#include "VideoPlayer.h"

LRESULT CALLBACK PlayerWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CVideoPlayer* player = reinterpret_cast<CVideoPlayer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_NCCREATE:
    {
        const CREATESTRUCTW* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        CVideoPlayer* player = static_cast<CVideoPlayer*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(player));

        return TRUE;
    }

    case WM_SIZE:
    {
        if (player)
        {
            const int width = LOWORD(lParam);
            const int height = HIWORD(lParam);

            if (width > 0 && height > 0)
            {
                //player->OnWindowSizeChanged(width, height);
            }
        }

        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_NCDESTROY:
    {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}