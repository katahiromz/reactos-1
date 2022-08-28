// Example of RichMenu control
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <strsafe.h>
#include "richmenu.h"

#include <vector>
std::vector<HRICHMENU> m_vecRichMenus;

INT_PTR CALLBACK
DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        return TRUE;
    case WM_RBUTTONUP:
        {
            INT x = (SHORT)LOWORD(lParam);
            INT y = (SHORT)HIWORD(lParam);
            HMENU hMenu = ::LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(1));
            HRICHMENU hRichMenu = RichMenu_FromHMENU(hMenu, NULL);
            m_vecRichMenus.push_back(hRichMenu);

            RichMenu_CheckRadioItem(hRichMenu, 1, 3, 2, MF_BYPOSITION);
            RichMenu_EnableItem(hRichMenu, 3, MF_BYPOSITION | MF_GRAYED);

            LOGFONT lf;
            ::GetObject(GetStockFont(DEFAULT_GUI_FONT), sizeof(lf), &lf);
            lf.lfHeight = -18;
            lf.lfCharSet = DEFAULT_CHARSET;
            lf.lfWeight = FW_NORMAL;
            RichMenu_SetLogFont(hRichMenu, &lf);

            POINT pt = { x, y };
            ::ClientToScreen(hwnd, &pt);
            RichMenu_ShowPopup(hRichMenu, 0, pt.x, pt.y, hwnd, NULL);
            ::DestroyMenu(hMenu);
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            if (HDC hdc = BeginPaint(hwnd, &ps))
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                DrawText(hdc, TEXT("(Right-Click me)"), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                EndPaint(hwnd, &ps);
            }
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        case IDCANCEL:
            for (auto& item : m_vecRichMenus)
            {
                RichMenu_CloseHandle(item);
            }
            m_vecRichMenus.clear();
            EndDialog(hwnd, IDOK);
            break;
        default:
            {
                UINT nID = LOWORD(wParam);
                TCHAR szText[32];
                StringCchPrintf(szText, _countof(szText), TEXT("Clicked %u"), nID);
                MessageBox(hwnd, TEXT("Menu Clicked!"), szText, MB_ICONINFORMATION);
            }
            break;
        }
    }
    return 0;
}

INT WINAPI
WinMain(HINSTANCE   hInstance,
        HINSTANCE   hPrevInstance,
        LPSTR       lpCmdLine,
        INT         nCmdShow)
{
    InitCommonControls();
    RichMenu_Init();
    DialogBox(hInstance, MAKEINTRESOURCE(1), NULL, DialogProc);
    RichMenu_Exit();
    return 0;
}
