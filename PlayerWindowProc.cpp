#include "PlayerWindowProc.h"

#include "PlayerInstance.h"

LRESULT CALLBACK PlayerWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CPlayerInstance* player = reinterpret_cast<CPlayerInstance*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_NCCREATE:
    {
        const CREATESTRUCTW* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        CPlayerInstance* player = static_cast<CPlayerInstance*>(createStruct->lpCreateParams);
        if (!player)
            return FALSE;

        SetLastError(ERROR_SUCCESS);
        LONG_PTR previousValue = SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(player));
        if (previousValue == 0)
        {
            const DWORD error = GetLastError();
            if (error != ERROR_SUCCESS)
                return FALSE;
        }

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
                //player->SetWindowPos(RECT(0, 0, width, height));
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