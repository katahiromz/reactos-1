/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     LGPL-2.0-or-later (https://spdx.org/licenses/LGPL-2.0-or-later)
 * PURPOSE:     RichMenu: lock-free, activation-free, easily-choosable fake menu control
 * COPYRIGHT:   Copyright 2022 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */
/* richmenu.cpp --- RichMenu by katahiromz
 *
 * - Lock-free,
 * - Activation-free,
 * - Easily-choosable menu control.
 *
 */
#include <windows.h>
#include <windowsx.h>
#include <assert.h>
#include <strsafe.h>
#include <string.h>
#include "richmenu.h"

#define RICHMENU_MARGIN 8
#define RICHMENU_CX_CHECK GetSystemMetrics(SM_CXMENUCHECK)
#define RICHMENU_CY_CHECK GetSystemMetrics(SM_CYMENUCHECK)
#define RICHMENU_CX_SEP 6
#define RICHMENU_CY_SEP 6
#define RICHMENU_CX_RIGHT_SPACE 24

#ifndef _countof
    #define _countof(array) (sizeof(array) / sizeof(array[0]))
#endif

#ifdef __REACTOS__
inline void* operator new(size_t size)
{
    return calloc(size, 1);
}

inline void operator delete(void *ptr)
{
    free(ptr);
}
#endif

typedef struct tagRICHMENUITEM
{
    INT m_nID;      // The item ID
    INT m_cxyItem;  // The item height
    UINT m_fType;   // Same as MENUITEMINFO.fType
    UINT m_fState;  // Same as MENUITEMINFO.fState
    struct tagRICHMENU *m_pSubMenu;
    RECT m_rcItem;
    TCHAR m_szText[80];
    struct tagRICHMENUITEM *m_pNext, *m_pPrev; // cyclic
} RICHMENUITEM, *PRICHMENUITEM;

typedef struct tagRICHMENU
{
    LONG m_cRef;
    HWND m_hwnd;
    HWND m_hwndTarget;
    HWND m_hwndFocus;
    HWND m_hwndActive;
    HWND m_hwndForeground;
    INT m_cItems;
    PRICHMENUITEM m_pItems;
    INT m_iSelected;
    INT m_iOldSelect;
    INT m_iOpenSubMenu;
    INT m_iParentItem;
    HFONT m_hFont;
    struct tagRICHMENU *m_pParent;
    SIZE m_sizMenu;
} RICHMENU, *PRICHMENU;

class RichMenu : public RICHMENU
{
public:
    static RichMenu *FromHMENU(HMENU hMenu, RichMenu *pParent = NULL);
    ~RichMenu();

    static BOOL RegisterClass();

    operator HWND() const
    {
        return m_hwnd;
    }

    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();

    BOOL IsAlive() const;
    static BOOL IsRichMenu(HWND hwnd);

    VOID ShowPopup(UINT uFlags, INT x, INT y, HWND hwnd, LPCRECT prcExclude = NULL);
    VOID HidePopup();
    VOID CloseSubMenus();

    INT HitTest(POINT ptClient) const;

    RichMenu *GetRoot();
    RichMenu *GetSubRichMenu(INT iItem);
    INT GetItemID(INT iItem) const;
    PRICHMENUITEM GetItem(INT iItem, BOOL bByPosition = FALSE);
    RECT GetItemRect(INT iItem, BOOL bByPosition = FALSE);

    INT IdToIndex(INT nID, RichMenu **ppOwner = NULL);
    INT IndexToId(INT iItem) const;

    BOOL CheckItem(INT iItem, UINT uCheck);
    BOOL CheckRadioItem(INT iFirst, INT iLast, INT iCheck, UINT uFlags);
    BOOL EnableItem(INT iItem, UINT uEnable);

    INT InsertItem(const MENUITEMINFO *pmii, INT iItem);

    BOOL SetLogFont(LPLOGFONT plf = NULL);

    static LRESULT CALLBACK MouseProc(INT nCode, WPARAM wParam, LPARAM lParam);

    static LONG s_nAliveCount;

protected:
    RichMenu(HMENU hMenu, RichMenu *pParent);
    VOID CalcSize();
    void HookedMouseAction(MOUSEHOOKSTRUCT *pmhs);
    BOOL MouseHook(HWND hwnd, BOOL bHook);
    void BoundCheck(HWND hwnd, LPARAM lParam);
    static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam);

    static inline BOOL IsCrossProcessWindow(HWND hwnd)
    {
        DWORD dwPID;
        GetWindowThreadProcessId(hwnd, &dwPID);
        return dwPID != GetCurrentProcessId();
    }

    static LRESULT CALLBACK
    WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT CALLBACK WindowProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    BOOL OnCreate(HWND hwnd, LPCREATESTRUCT lpCreateStruct);
    BOOL OnEraseBkgnd(HWND hwnd, HDC hdc);
    int OnMouseActivate(HWND hwnd, HWND hwndTopLevel, UINT codeHitTest, UINT msg);
    void OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
    void OnDestroy(HWND hwnd);
    void OnDrawItem(HWND hwnd, const DRAWITEMSTRUCT * lpDrawItem);
    void OnLButtonDown(HWND hwnd, BOOL fDoubleClick, int x, int y, UINT keyFlags);
    void OnLButtonUp(HWND hwnd, int x, int y, UINT keyFlags);
    void OnMeasureItem(HWND hwnd, MEASUREITEMSTRUCT * lpMeasureItem);
    void OnMouseMove(HWND hwnd, int x, int y, UINT keyFlags);
    void OnPaint(HWND hwnd);
    void OnRButtonUp(HWND hwnd, int x, int y, UINT keyFlags);
    void OnTimer(HWND hwnd, UINT id);

    static RichMenu *Get(PRICHMENU pRichMenu)
    {
        return reinterpret_cast<RichMenu*>(pRichMenu);
    }
    static const RichMenu *Get(const RICHMENU *pRichMenu)
    {
        return reinterpret_cast<const RichMenu*>(pRichMenu);
    }
};

RichMenu *g_pOpenedRoot = NULL;

/*static*/ LONG RichMenu::s_nAliveCount = 0;

RichMenu::RichMenu(HMENU hMenu, RichMenu *pParent)
{
    ++s_nAliveCount;

    INT cItems = GetMenuItemCount(hMenu);
    if (cItems == -1)
    {
        assert(0);
        cItems = 0;
    }

    // Initialize
    m_cRef = 1;
    m_hwnd = m_hwndTarget = NULL;
    m_hwndFocus = ::GetFocus();
    m_hwndActive = ::GetActiveWindow();
    m_hwndForeground = ::GetForegroundWindow();
    m_cItems = cItems;
    m_pItems = NULL;
    m_iSelected = m_iOldSelect = m_iOpenSubMenu = m_iParentItem = -1;
    m_hFont = GetStockFont(DEFAULT_GUI_FONT);
    m_pParent = pParent;
    m_sizMenu.cx = m_sizMenu.cy = 0;

    // Populate the items
    for (INT iItem = 0; iItem < cItems; ++iItem)
    {
        TCHAR szText[80];
        MENUITEMINFO mii =
        {
            sizeof(mii),
            MIIM_TYPE | MIIM_ID | MIIM_DATA | MIIM_STATE | MIIM_SUBMENU
        };
        szText[0] = 0;
        mii.cch = _countof(szText);
        mii.dwTypeData = szText;
        GetMenuItemInfo(hMenu, iItem, TRUE, &mii);

        InsertItem(&mii, iItem);

        if (mii.hSubMenu)
        {
            PRICHMENUITEM pItem = GetItem(iItem, TRUE);
            if (pItem)
            {
                RichMenu *pSubMenu = RichMenu::FromHMENU(mii.hSubMenu, this);
                pSubMenu->m_iParentItem = iItem;

                LOGFONT lf;
                ::GetObject(m_hFont, sizeof(lf), &lf);
                pSubMenu->SetLogFont(&lf);

                pItem->m_pSubMenu = pSubMenu;
            }
        }
    }
}

ULONG STDMETHODCALLTYPE RichMenu::AddRef()
{
    return ::InterlockedIncrement(&m_cRef);
}

ULONG STDMETHODCALLTYPE RichMenu::Release()
{
    ULONG cRef = ::InterlockedDecrement(&m_cRef);
    if (!cRef)
        delete this;
    return cRef;
}

/*static*/ RichMenu *RichMenu::FromHMENU(HMENU hMenu, RichMenu *pParent)
{
    void *ptr = calloc(sizeof(RichMenu), 1);
    if (!ptr)
        return NULL;
    RichMenu *ret = new RichMenu(hMenu, pParent);
    return ret;
}

RichMenu::~RichMenu()
{
    if (IsWindowVisible(m_hwnd))
        HidePopup();

    PRICHMENUITEM pItem = m_pItems;
    for (INT iItem = 0; iItem < m_cItems; ++iItem)
    {
        PRICHMENUITEM pNext = pItem->m_pNext;
        RichMenu *pSubMenu = Get(pItem->m_pSubMenu);
        if (pSubMenu)
        {
            pItem->m_pSubMenu = NULL;
            pSubMenu->Release();
        }
        free(pItem);
        pItem = pNext;
    }

    ::DeleteObject(m_hFont);

    m_pParent = NULL;

    if (g_pOpenedRoot == this)
        g_pOpenedRoot = NULL;

    --s_nAliveCount;
}

BOOL RichMenu::IsAlive() const
{
    if (!IsRichMenu(m_hwnd))
    {
        return FALSE;
    }
    if (m_hwndFocus != ::GetFocus())
    {
        return FALSE;
    }
    if (m_hwndActive != ::GetActiveWindow())
    {
        return FALSE;
    }
    if (m_hwndForeground != ::GetForegroundWindow())
    {
        return FALSE;
    }
    return TRUE;
}

/*static*/ BOOL RichMenu::IsRichMenu(HWND hwnd)
{
    TCHAR szText[64];
    if (!GetClassName(hwnd, szText, _countof(szText)))
        return FALSE;
    return lstrcmpi(szText, RICHMENU_CLASSNAME) == 0;
}

VOID RichMenu::CloseSubMenus()
{
    PRICHMENUITEM pItem = m_pItems;
    for (INT iItem = 0; iItem < m_cItems; ++iItem)
    {
        RichMenu *pSubMenu = Get(pItem->m_pSubMenu);
        if (pSubMenu)
        {
            ::PostMessage(*pSubMenu, WM_CLOSE, 0, 0);
        }

        pItem = pItem->m_pNext;
    }
}

RichMenu *RichMenu::GetRoot()
{
    RichMenu* pParent = Get(m_pParent);
    if (!pParent)
        return this;

    while (pParent->m_pParent)
    {
        pParent = Get(pParent->m_pParent);
    }

    return pParent;
}

INT RichMenu::GetItemID(INT iItem) const
{
    const RICHMENUITEM *pItem = m_pItems;
    for (INT i = 0; i < m_cItems; ++i)
    {
        if (i == iItem)
        {
            return pItem->m_nID;
        }
        pItem = pItem->m_pNext;
    }
    return 0;
}

RichMenu *RichMenu::GetSubRichMenu(INT iItem)
{
    PRICHMENUITEM pItem = m_pItems;
    for (INT i = 0; i < m_cItems; ++i)
    {
        if (i == iItem)
        {
            return Get(pItem->m_pSubMenu);
        }
        pItem = pItem->m_pNext;
    }
    return NULL;
}

PRICHMENUITEM RichMenu::GetItem(INT iItem, BOOL bByPosition)
{
    RichMenu *pOwner = this;
    if (!bByPosition)
        iItem = IdToIndex(iItem, &pOwner);
    if (iItem == -1 || !pOwner)
        return NULL;

    PRICHMENUITEM pItem = pOwner->m_pItems;

    for (INT i = 0; pItem && i < pOwner->m_cItems; ++i)
    {
        if (i == iItem)
            break;
        pItem = pItem->m_pNext;
    }

    return pItem;
}

RECT RichMenu::GetItemRect(INT iItem, BOOL bByPosition)
{
    RECT rc;
    PRICHMENUITEM pItem = GetItem(iItem, bByPosition);
    if (!pItem)
    {
        SetRectEmpty(&rc);
        return rc;
    }

    return pItem->m_rcItem;
}

INT RichMenu::IdToIndex(INT nID, RichMenu **ppOwner)
{
    if (ppOwner)
        *ppOwner = NULL;

    if (nID == 0)
        return -1;

    PRICHMENUITEM pItem = m_pItems;
    for (INT i = 0; pItem && i < m_cItems; ++i)
    {
        if (pItem->m_nID == nID)
        {
            *ppOwner = this;
            return i;
        }
        else if (RichMenu *pSubMenu = Get(pItem->m_pSubMenu))
        {
            INT iItem = pSubMenu->IdToIndex(nID, ppOwner);
            if (iItem != -1)
                return iItem;
        }

        pItem = pItem->m_pNext;
    }

    return -1;
}

INT RichMenu::IndexToId(INT iItem) const
{
    if (iItem == -1)
        return 0;

    PRICHMENUITEM pItem = m_pItems;
    for (INT i = 0; pItem && i < m_cItems; ++i)
    {
        if (i == iItem)
        {
            return pItem->m_nID;
        }
        pItem = pItem->m_pNext;
    }

    return 0;
}

INT RichMenu::HitTest(POINT ptClient) const
{
    PRICHMENUITEM pItem = m_pItems;
    for (INT iItem = 0; iItem < m_cItems; ++iItem)
    {
        if (PtInRect(&pItem->m_rcItem, ptClient))
        {
            return iItem;
        }
        pItem = pItem->m_pNext;
    }
    return -1;
}

INT RichMenu::InsertItem(const MENUITEMINFO *pmii, INT iItem)
{
    if (iItem != -1 && (iItem < 0 || m_cItems < iItem))
    {
        return -1;
    }

    PRICHMENUITEM pNewItem = (PRICHMENUITEM)calloc(sizeof(RICHMENUITEM), 1);
    if (!pNewItem)
        return -1;

    // Initialize
    pNewItem->m_nID = pmii->wID;
    pNewItem->m_cxyItem = 0;
    pNewItem->m_fType = pmii->fType;
    pNewItem->m_fState = pmii->fState;
    pNewItem->m_pSubMenu = NULL;
    SetRectEmpty(&pNewItem->m_rcItem);

    pNewItem->m_szText[0] = 0;
    if (!(pmii->fType & MFT_SEPARATOR))
    {
        StringCchCopy(pNewItem->m_szText, _countof(pNewItem->m_szText),
                      (LPCTSTR)pmii->dwTypeData);
    }

    if (m_cItems == 0 || m_pItems == NULL)
    {
        pNewItem->m_pNext = pNewItem->m_pPrev = pNewItem;
        m_pItems = pNewItem;
        m_cItems = 1;
        return 0;
    }

    if (iItem == -1 || iItem == m_cItems)
    {
        pNewItem->m_pNext = m_pItems;
        pNewItem->m_pPrev = m_pItems->m_pPrev;
        m_pItems->m_pPrev->m_pNext = pNewItem;
        m_pItems->m_pPrev = pNewItem;
        ++(m_cItems);
        return m_cItems - 1;
    }

    PRICHMENUITEM pTargetItem = GetItem(iItem, TRUE);
    if (pTargetItem == NULL)
    {
        free(pNewItem);
        return -1;
    }

    pTargetItem->m_pPrev->m_pNext = pNewItem;
    pTargetItem->m_pNext->m_pPrev = pNewItem;
    ++(m_cItems);
    return iItem;
}

VOID RichMenu::CalcSize()
{
    assert(m_hwnd != NULL);

    UINT cxMax = 0, cyMax = 0;
    PRICHMENUITEM pItem = m_pItems;
    for (INT iItem = 0; iItem < m_cItems; ++iItem)
    {
        MEASUREITEMSTRUCT MeasureItem = { ODT_MENU, 0 };
        MeasureItem.itemData = (DWORD_PTR)pItem;
        SendMessage(m_hwnd, WM_MEASUREITEM, 0, (LPARAM)&MeasureItem);
        pItem->m_cxyItem = MeasureItem.itemHeight;
        if (MeasureItem.itemWidth > cxMax)
            cxMax = MeasureItem.itemWidth;
        cyMax += MeasureItem.itemHeight;
        pItem = pItem->m_pNext;
    }

    INT y = 0;
    pItem = m_pItems;
    for (INT iItem = 0; iItem < m_cItems; ++iItem)
    {
        SetRect(&pItem->m_rcItem, 0, y, cxMax, y + pItem->m_cxyItem);
        y += pItem->m_cxyItem;
        pItem = pItem->m_pNext;
    }

    m_sizMenu.cx = cxMax;
    m_sizMenu.cy = cyMax;
}

HHOOK g_hMouseHook = NULL;
INT g_nMouseHookCount = 0;

void RichMenu::HookedMouseAction(MOUSEHOOKSTRUCT *pmhs)
{
    if (!IsAlive())
    {
        RichMenu* pRoot = GetRoot();
        if (pRoot)
            pRoot->HidePopup();
        return;
    }

    if (pmhs->hwnd && IsCrossProcessWindow(pmhs->hwnd))
    {
        RichMenu* pRoot = GetRoot();
        if (pRoot)
            pRoot->HidePopup();
        return;
    }

    HWND hwnd = ::WindowFromPoint(pmhs->pt);
    if (!IsRichMenu(hwnd) || IsCrossProcessWindow(hwnd))
    {
        RichMenu* pRoot = GetRoot();
        if (pRoot)
            pRoot->HidePopup();
    }
}

/*static*/ BOOL CALLBACK RichMenu::EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    if (!RichMenu::IsRichMenu(hwnd) || IsCrossProcessWindow(hwnd))
        return TRUE;

    RichMenu *pRichMenu = (RichMenu *)GetWindowLongPtr(hwnd, 0);
    if (pRichMenu)
    {
        pRichMenu->BoundCheck(hwnd, lParam);
    }

    return TRUE;
}

void RichMenu::BoundCheck(HWND hwnd, LPARAM lParam)
{
    POINT* ppt = (POINT*)lParam;
    RECT rc;
    ::GetWindowRect(hwnd, &rc);
    if (!PtInRect(&rc, *ppt) && ::IsWindow(hwnd))
    {
        if (!IsAlive())
            return;
        m_iOldSelect = m_iSelected;
        m_iSelected = -1;
        RECT rc1 = GetItemRect(m_iOldSelect, TRUE);
        RECT rc2 = GetItemRect(m_iSelected, TRUE);
        InvalidateRect(hwnd, &rc1, FALSE);
        InvalidateRect(hwnd, &rc2, FALSE);
    }
}

LRESULT CALLBACK RichMenu::MouseProc(INT nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0)
        return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);

    if (nCode == HC_ACTION)
    {
        MOUSEHOOKSTRUCT *pmhs = (MOUSEHOOKSTRUCT *)lParam;
        if (RichMenu *pRoot = g_pOpenedRoot)
        {
            pRoot->AddRef();
            switch (wParam)
            {
            case WM_LBUTTONUP:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONDBLCLK:
            case WM_NCLBUTTONUP:
            case WM_NCLBUTTONDOWN:
            case WM_NCLBUTTONDBLCLK:
            case WM_NCRBUTTONUP:
            case WM_NCRBUTTONDOWN:
            case WM_NCRBUTTONDBLCLK:
                if (pRoot)
                {
                    pRoot->HookedMouseAction(pmhs);
                }
                break;
            }
            pRoot->Release();
        }
    }

    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

BOOL RichMenu::MouseHook(HWND hwnd, BOOL bHook)
{
    if (bHook)
    {
        if (g_nMouseHookCount++ == 0)
        {
            g_hMouseHook = SetWindowsHookEx(WH_MOUSE, MouseProc, NULL, GetCurrentThreadId());
            return g_hMouseHook != NULL;
        }
    }
    else
    {
        if (--g_nMouseHookCount == 0)
        {
            BOOL ret = UnhookWindowsHookEx(g_hMouseHook);
            g_hMouseHook = NULL;
            return ret;
        }
    }
    return FALSE;
}

BOOL RichMenu::OnCreate(HWND hwnd, LPCREATESTRUCT lpCreateStruct)
{
    MouseHook(hwnd, TRUE);
    if (!m_pParent)
        SetTimer(hwnd, 999, 100, NULL);
    return TRUE;
}

void RichMenu::OnTimer(HWND hwnd, UINT id)
{
    if (id == 999)
    {
        if (RichMenu* pRoot = GetRoot())
        {
            if (pRoot != g_pOpenedRoot || !pRoot->IsAlive())
            {
                pRoot->HidePopup();
            }
        }

        POINT pt;
        ::GetCursorPos(&pt);
        ::EnumWindows(EnumWindowsProc, (LPARAM)&pt);
    }
}

void RichMenu::OnMeasureItem(HWND hwnd, MEASUREITEMSTRUCT * lpMeasureItem)
{
    PRICHMENUITEM pItem = (PRICHMENUITEM)lpMeasureItem->itemData;

    if (pItem->m_fType == MFT_SEPARATOR)
    {
        lpMeasureItem->itemHeight = RICHMENU_CY_SEP;
        return;
    }

    HDC hdc = CreateCompatibleDC(NULL);
    HGDIOBJ hFontOld = SelectObject(hdc, m_hFont);
    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    SIZE siz;
    GetTextExtentPoint32(hdc, pItem->m_szText, lstrlen(pItem->m_szText), &siz);
    SelectObject(hdc, hFontOld);
    DeleteDC(hdc);

    UINT itemWidth = RICHMENU_CX_CHECK + siz.cx + 2 * RICHMENU_MARGIN + RICHMENU_CX_RIGHT_SPACE;
    UINT itemHeight = tm.tmHeight + 2 * RICHMENU_MARGIN;
    if (itemHeight < 24)
        itemHeight = 24;
    if (lpMeasureItem->itemWidth < itemWidth)
        lpMeasureItem->itemWidth = itemWidth;
    if (lpMeasureItem->itemHeight < itemHeight)
        lpMeasureItem->itemHeight = itemHeight;
}

BOOL CreateMask(HDC& hdcMem, HBITMAP& hbmMask, const RECT& rc, RECT& rcDraw, SIZE& siz)
{
    siz = { rc.right - rc.left, rc.bottom - rc.top };
    hbmMask = CreateBitmap(siz.cx, siz.cy, 1, 1, NULL);
    hdcMem = CreateCompatibleDC(NULL);
    rcDraw = { 0, 0, siz.cx, siz.cy };
    return hbmMask && hdcMem;
}

void RichMenu::OnDrawItem(HWND hwnd, const DRAWITEMSTRUCT * lpDrawItem)
{
    PRICHMENUITEM pItem = (PRICHMENUITEM)lpDrawItem->itemData;
    HDC hdc = lpDrawItem->hDC;
    RECT rcItem = lpDrawItem->rcItem;
    if (!RectVisible(hdc, &rcItem))
        return;

    BOOL bGrayed = (lpDrawItem->itemState & (ODS_GRAYED | ODS_DISABLED));
    BOOL bSelected = (lpDrawItem->itemState & ODS_SELECTED);
    COLORREF rgbText, rgbBack;
    if (pItem->m_fType == MFT_SEPARATOR)
    {
        rgbBack = GetSysColor(COLOR_MENU);
        rgbText = GetSysColor(COLOR_MENUTEXT);
    }
    else if (bGrayed)
    {
        if (bSelected)
        {
            rgbBack = GetSysColor(COLOR_HIGHLIGHT);
            rgbText = GetSysColor(COLOR_GRAYTEXT);
        }
        else
        {
            rgbBack = GetSysColor(COLOR_MENU);
            rgbText = GetSysColor(COLOR_HIGHLIGHTTEXT);
        }
    }
    else if (bSelected)
    {
        rgbBack = GetSysColor(COLOR_HIGHLIGHT);
        rgbText = GetSysColor(COLOR_HIGHLIGHTTEXT);
    }
    else
    {
        rgbBack = GetSysColor(COLOR_MENU);
        rgbText = GetSysColor(COLOR_MENUTEXT);
    }

    SetDCBrushColor(hdc, rgbBack);
    FillRect(hdc, &rcItem, GetStockBrush(DC_BRUSH));

    if (pItem->m_fType == MFT_SEPARATOR)
    {
        SelectObject(hdc, GetStockPen(DC_PEN));
        SetDCPenColor(hdc, GetSysColor(COLOR_3DSHADOW));
        MoveToEx(hdc, rcItem.left, (rcItem.top + rcItem.bottom) / 2, NULL);
        LineTo(hdc, rcItem.right, (rcItem.top + rcItem.bottom) / 2);
        OffsetRect(&rcItem, 1, 1);
        SetDCPenColor(hdc, GetSysColor(COLOR_3DLIGHT));
        MoveToEx(hdc, rcItem.left, (rcItem.top + rcItem.bottom) / 2, NULL);
        LineTo(hdc, rcItem.right, (rcItem.top + rcItem.bottom) / 2);
        return;
    }

    if (pItem->m_fState & MFS_CHECKED)
    {
        RECT rcCheck = rcItem;
        rcCheck.right = rcCheck.left + RICHMENU_CX_CHECK + 2 * RICHMENU_CX_SEP;
        InflateRect(&rcCheck, -RICHMENU_CX_SEP, -RICHMENU_CY_SEP);

        HDC hdcMem;
        HBITMAP hbmMask;
        RECT rcDraw;
        SIZE siz;
        if (CreateMask(hdcMem, hbmMask, rcCheck, rcDraw, siz))
        {
            HGDIOBJ hbmOld = SelectObject(hdcMem, hbmMask);
            UINT uState;
            if (pItem->m_fType & MFT_RADIOCHECK)
                uState = DFCS_MENUBULLET | DFCS_CHECKED;
            else
                uState = DFCS_MENUCHECK | DFCS_CHECKED;
            DrawFrameControl(hdcMem, &rcDraw, DFC_MENU, uState);
            SetTextColor(hdc, GetSysColor(COLOR_MENUTEXT));
            SetBkColor(hdc, rgbBack);
            MaskBlt(hdc, rcCheck.left, rcCheck.top, siz.cx, siz.cy, hdcMem, 0, 0, hbmMask, 0, 0, MAKEROP4(SRCAND, SRCCOPY));
            SelectObject(hdcMem, hbmOld);
        }
        DeleteObject(hbmMask);
        DeleteDC(hdcMem);
    }

    if (pItem->m_pSubMenu)
    {
        RECT rcArrow = rcItem;
        rcArrow.left = rcArrow.right - RICHMENU_CX_RIGHT_SPACE + 2 * RICHMENU_CX_SEP;

        HDC hdcMem;
        HBITMAP hbmMask;
        RECT rcDraw;
        SIZE siz;
        if (CreateMask(hdcMem, hbmMask, rcArrow, rcDraw, siz))
        {
            HGDIOBJ hbmOld = SelectObject(hdcMem, hbmMask);
            DrawFrameControl(hdcMem, &rcDraw, DFC_MENU, DFCS_MENUARROW);
            SelectObject(hdcMem, GetStockBrush(DC_BRUSH));
            SetTextColor(hdc, rgbText);
            SetBkColor(hdc, rgbBack);
            MaskBlt(hdc, rcArrow.left, rcArrow.top, siz.cx, siz.cy, hdcMem, 0, 0, hbmMask, 0, 0, MAKEROP4(SRCAND, SRCCOPY));
            SelectObject(hdcMem, hbmOld);
        }
        DeleteObject(hbmMask);
        DeleteDC(hdcMem);
    }

    rcItem.left += RICHMENU_CX_CHECK + RICHMENU_CX_SEP;
    InflateRect(&rcItem, -RICHMENU_MARGIN, -RICHMENU_MARGIN);

    HGDIOBJ hFontOld = SelectObject(hdc, m_hFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, rgbText);
    SetBkColor(hdc, rgbBack);
    if (bGrayed)
    {
        DrawText(hdc, pItem->m_szText, -1, &rcItem, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (!bSelected)
        {
            SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
            OffsetRect(&rcItem, 1, 1);
            DrawText(hdc, pItem->m_szText, -1, &rcItem, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }
    else
    {
        DrawText(hdc, pItem->m_szText, -1, &rcItem, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hdc, hFontOld);
}

BOOL RichMenu::OnEraseBkgnd(HWND hwnd, HDC hdc)
{
    return TRUE;
}

void RichMenu::OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;

    if (HDC hdc = BeginPaint(hwnd, &ps))
    {
        PRICHMENUITEM pItem = m_pItems;
        for (INT iItem = 0; iItem < m_cItems; ++iItem)
        {
            DRAWITEMSTRUCT DrawItem = { ODT_MENU, 0 };
            DrawItem.itemAction = ODA_DRAWENTIRE;
            DrawItem.itemState = 0;
            if (iItem == m_iSelected)
                DrawItem.itemState |= ODS_SELECTED;
            if (pItem->m_fState & MFS_GRAYED)
                DrawItem.itemState |= ODS_DISABLED | ODS_GRAYED;
            DrawItem.hwndItem = hwnd;
            DrawItem.hDC = hdc;
            DrawItem.rcItem = pItem->m_rcItem;
            DrawItem.itemData = (DWORD_PTR)pItem;
            SendMessage(hwnd, WM_DRAWITEM, 0, (LPARAM)&DrawItem);
            pItem = pItem->m_pNext;
        }

        EndPaint(hwnd, &ps);
    }
}

void RichMenu::OnLButtonDown(HWND hwnd, BOOL fDoubleClick, int x, int y, UINT keyFlags)
{
    POINT pt = { x, y };
    INT iItem = HitTest(pt);

    PRICHMENUITEM pItem = GetItem(iItem, TRUE);
    if (pItem == NULL || (pItem->m_fState & MFS_GRAYED))
        return;
    RichMenu *pSubMenu = Get(pItem->m_pSubMenu);
    if (!pSubMenu)
        return;

    RECT rcItem = pItem->m_rcItem;
    pt.x = (rcItem.left + rcItem.right) / 2;
    pt.y = rcItem.top;
    ::ClientToScreen(m_hwnd, &pt);

    ::MapWindowRect(m_hwnd, NULL, &rcItem);

    pSubMenu->ShowPopup(0, pt.x, pt.y, m_hwndTarget, &rcItem);
}

void RichMenu::OnLButtonUp(HWND hwnd, int x, int y, UINT keyFlags)
{
    POINT pt = { x, y };
    INT iItem = HitTest(pt);
    INT id = IndexToId(iItem);

    PRICHMENUITEM pItem = GetItem(iItem, TRUE);
    if (pItem == NULL || (pItem->m_fState & MFS_GRAYED))
        return;
    if ((pItem->m_fType & MFT_SEPARATOR) || id == 0 || pItem->m_pSubMenu)
        return;

    HWND hwndTarget = m_hwndTarget;
    RichMenu *pRoot = GetRoot();
    if (pRoot)
    {
        pRoot->HidePopup();
    }

    ::PostMessage(hwndTarget, WM_COMMAND, id, 0);
}

void RichMenu::OnRButtonUp(HWND hwnd, int x, int y, UINT keyFlags)
{
    POINT pt = { x, y };
    INT iItem = HitTest(pt);
    INT id = IndexToId(iItem);

    PRICHMENUITEM pItem = GetItem(iItem, TRUE);
    if (pItem == NULL || (pItem->m_fState & MFS_GRAYED))
        return;
    if (pItem->m_fType == MFT_SEPARATOR || id == 0)
        return;

    HWND hwndTarget = m_hwndTarget;
    RichMenu *pRoot = GetRoot();
    if (pRoot)
    {
        pRoot->HidePopup();
    }
    ::PostMessage(hwndTarget, WM_COMMAND, id, 0);
}

void RichMenu::OnMouseMove(HWND hwnd, int x, int y, UINT keyFlags)
{
    POINT pt = { x, y };
    INT iItem = HitTest(pt);
    m_iOldSelect = m_iSelected;
    m_iSelected = iItem;

    RECT rc1 = GetItemRect(m_iOldSelect, TRUE);
    RECT rc2 = GetItemRect(m_iSelected, TRUE);
    ::InvalidateRect(m_hwnd, &rc1, FALSE);
    ::InvalidateRect(m_hwnd, &rc2, FALSE);
}

void RichMenu::OnDestroy(HWND hwnd)
{
    if (!m_pParent)
        KillTimer(hwnd, 999);
    if (!m_pParent)
        CloseSubMenus();
    MouseHook(hwnd, FALSE);
    SetWindowLongPtr(hwnd, 0, 0);
    m_hwnd = NULL;
}

int RichMenu::OnMouseActivate(HWND hwnd, HWND hwndTopLevel, UINT codeHitTest, UINT msg)
{
    return MA_NOACTIVATE;
}

void RichMenu::OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
    ::PostMessage(::GetParent(hwnd), WM_COMMAND, id, 0);
}

LRESULT CALLBACK
RichMenu::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    RichMenu *pRichMenu;
    if (uMsg == WM_CREATE)
    {
        LPCREATESTRUCT lpCreateStruct = (LPCREATESTRUCT)lParam;
        pRichMenu = (RichMenu*)lpCreateStruct->lpCreateParams;
        SetWindowLongPtr(hwnd, 0, (LONG_PTR)pRichMenu);
    }
    else
    {
        pRichMenu = (RichMenu *)GetWindowLongPtr(hwnd, 0);
        if (pRichMenu == NULL)
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    return pRichMenu->WindowProcDx(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK
RichMenu::WindowProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        HANDLE_MSG(hwnd, WM_CREATE, OnCreate);
        HANDLE_MSG(hwnd, WM_TIMER, OnTimer);
        HANDLE_MSG(hwnd, WM_MEASUREITEM, OnMeasureItem);
        HANDLE_MSG(hwnd, WM_DRAWITEM, OnDrawItem);
        HANDLE_MSG(hwnd, WM_ERASEBKGND, OnEraseBkgnd);
        HANDLE_MSG(hwnd, WM_PAINT, OnPaint);
        HANDLE_MSG(hwnd, WM_LBUTTONDOWN, OnLButtonDown);
        HANDLE_MSG(hwnd, WM_LBUTTONUP, OnLButtonUp);
        HANDLE_MSG(hwnd, WM_RBUTTONUP, OnRButtonUp);
        HANDLE_MSG(hwnd, WM_MOUSEMOVE, OnMouseMove);
        HANDLE_MSG(hwnd, WM_MOUSEACTIVATE, OnMouseActivate);
        HANDLE_MSG(hwnd, WM_COMMAND, OnCommand);
        HANDLE_MSG(hwnd, WM_DESTROY, OnDestroy);
        default:
            return ::DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

BOOL RichMenu::RegisterClass(VOID)
{
    WNDCLASSEX wcx = { sizeof(wcx), CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS };

    wcx.lpfnWndProc = RichMenu::WndProc;
    wcx.cbWndExtra = sizeof(PRICHMENU);
    wcx.hInstance = GetModuleHandle(NULL);
    wcx.hIcon = NULL;
    wcx.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcx.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
    wcx.lpszClassName = RICHMENU_CLASSNAME;

    return !!RegisterClassEx(&wcx);
}

VOID RichMenu::HidePopup()
{
    ::ShowWindow(m_hwnd, SW_HIDE);

    PRICHMENUITEM pItem = m_pItems;
    for (INT i = 0; pItem && i < m_cItems; ++i)
    {
        if (RichMenu* pSubMenu = Get(pItem->m_pSubMenu))
        {
            pSubMenu->HidePopup();
        }

        pItem = pItem->m_pNext;
    }

    if (m_pParent)
        m_pParent->m_iOpenSubMenu = -1;
}

VOID RichMenu::ShowPopup(UINT uFlags, INT x, INT y, HWND hwnd, LPCRECT prcExclude)
{
    if (g_pOpenedRoot)
    {
        AddRef();
        if (g_pOpenedRoot != GetRoot())
            g_pOpenedRoot->HidePopup();
        Release();
    }

    assert(hwnd != NULL);
    m_hwndTarget = hwnd;

    DWORD style = WS_POPUP | WS_DLGFRAME;
    DWORD exstyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    if (!::IsWindow(*this))
    {
        m_hwnd = CreateWindowEx(exstyle, RICHMENU_CLASSNAME, NULL,
            style, 0, 0, 1, 1, hwnd, NULL, GetModuleHandle(NULL), this);
        if (!m_hwnd)
            return;
    }

    RichMenu* pParent = Get(m_pParent);
    if (pParent)
    {
        INT iOpenSubMenu = pParent->m_iOpenSubMenu;
        if (iOpenSubMenu != -1)
        {
            RichMenu* pSubMenu = pParent->GetSubRichMenu(iOpenSubMenu);
            if (pSubMenu && pSubMenu != this)
                pSubMenu->HidePopup();
        }
        pParent->m_iOpenSubMenu = m_iParentItem;
    }
    else
    {
        g_pOpenedRoot = this;
    }

    CalcSize();
    SIZE siz = m_sizMenu;

    RECT rcWork;
    ::SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);

    RECT rc = { x, y, x + siz.cx, y + siz.cy};
    ::AdjustWindowRectEx(&rc, style, FALSE, exstyle);
    siz.cx = rc.right - rc.left;
    siz.cy = rc.bottom - rc.top;

    INT x0, y0, x1, y1;

    POINT pt = { x, y };
    if (prcExclude && ::PtInRect(prcExclude, pt))
    {
        if (prcExclude->right + siz.cx < rcWork.right)
        {
            x0 = prcExclude->right;
            x1 = x0 + siz.cx;
        }
        else
        {
            x0 = prcExclude->left - siz.cx;
            x1 = x0 + siz.cx;
        }
        if (prcExclude->top + siz.cy < rcWork.bottom)
        {
            y0 = prcExclude->top;
            y1 = y0 + siz.cy;
        }
        else
        {
            y1 = prcExclude->bottom;
            y0 = y1 - siz.cy;
        }
    }
    else
    {
        if (x + siz.cx < rcWork.right)
        {
            x0 = x;
            x1 = x + siz.cx;
        }
        else
        {
            x1 = x + 1;
            x0 = x - siz.cx + 1;
        }
        if (y + siz.cy < rcWork.bottom)
        {
            y0 = y;
            y1 = y + siz.cy;
        }
        else
        {
            y1 = y + 1;
            y0 = y - siz.cy + 1;
        }
    }

    ::MoveWindow(m_hwnd, x0, y0, x1 - x0, y1 - y0, TRUE);
    ::ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
}

BOOL RichMenu::CheckItem(INT iItem, UINT uCheck)
{
    RichMenu *pOwner = NULL;
    if (!(uCheck & MF_BYPOSITION))
        iItem = IdToIndex(iItem, &pOwner);
    if (!pOwner)
        return FALSE;

    PRICHMENUITEM pItem = pOwner->GetItem(iItem, TRUE);
    if (!pItem)
        return FALSE;

    pItem->m_fType &= ~MFT_RADIOCHECK;
    if (uCheck & MF_CHECKED)
        pItem->m_fState |= MFS_CHECKED;
    else
        pItem->m_fState &= ~MFS_CHECKED;

    RECT rc1 = pOwner->GetItemRect(iItem, TRUE);
    InvalidateRect(*pOwner, &rc1, FALSE);
    return TRUE;
}

BOOL RichMenu::CheckRadioItem(INT iFirst, INT iLast, INT iCheck, UINT uFlags)
{
    RichMenu *pOwner = this;
    if (!(uFlags & MF_BYPOSITION))
    {
        RichMenu *pOwner1, *pOwner2, *pOwner3;
        iFirst = IdToIndex(iFirst, &pOwner1);
        iLast = IdToIndex(iLast, &pOwner2);
        iCheck = IdToIndex(iCheck, &pOwner3);
        if (pOwner1 != pOwner2 || pOwner2 != pOwner3)
            return FALSE;
        pOwner = pOwner1;
    }

    if (!pOwner)
        return FALSE;

    for (INT iItem = iFirst; iItem <= iLast; ++iItem)
    {
        PRICHMENUITEM pItem = pOwner->GetItem(iItem, TRUE);
        if (!pItem)
            continue;

        pItem->m_fType |= MFT_RADIOCHECK;
        if (iItem == iCheck)
            pItem->m_fState |= MFS_CHECKED;
        else
            pItem->m_fState &= ~MFS_CHECKED;

        RECT rc1 = pOwner->GetItemRect(iItem, TRUE);
        InvalidateRect(*pOwner, &rc1, FALSE);
    }

    return TRUE;
}

BOOL RichMenu::EnableItem(INT iItem, UINT uEnable)
{
    RichMenu *pOwner = this;
    if (!(uEnable & MF_BYPOSITION))
    {
        iItem = IdToIndex(iItem, &pOwner);
    }

    if (!pOwner)
        return FALSE;

    PRICHMENUITEM pItem = pOwner->GetItem(iItem, TRUE);
    if (!pItem)
        return FALSE;

    if (uEnable & (MF_GRAYED | MF_DISABLED))
        pItem->m_fState |= MFS_GRAYED;
    else
        pItem->m_fState &= ~MFS_GRAYED;

    RECT rc1 = pOwner->GetItemRect(iItem, TRUE);
    InvalidateRect(*pOwner, &rc1, FALSE);
    return TRUE;
}

BOOL RichMenu::SetLogFont(LPLOGFONT plf)
{
    if (m_hFont)
        DeleteObject(m_hFont);
    if (plf)
        m_hFont = ::CreateFontIndirect(plf);
    else
        m_hFont = GetStockFont(DEFAULT_GUI_FONT);

    PRICHMENUITEM pItem = m_pItems;
    for (INT i = 0; i < m_cItems; ++i)
    {
        if (RichMenu *pSubMenu = Get(pItem->m_pSubMenu))
        {
            pSubMenu->SetLogFont(plf);
        }
        pItem = pItem->m_pNext;
    }
    return NULL;
}

extern "C"
{
    VOID APIENTRY RichMenu_Init(VOID)
    {
        RichMenu::RegisterClass();
    }

    VOID APIENTRY RichMenu_Exit(VOID)
    {
        if (RichMenu::s_nAliveCount > 0)
            assert(0);
        if (RichMenu::s_nAliveCount < 0)
            assert(0);
    }

    HRICHMENU APIENTRY
    RichMenu_FromHMENU(HMENU hMenu, HRICHMENU hParent OPTIONAL)
    {
        return (HRICHMENU)RichMenu::FromHMENU(hMenu, (RichMenu *)hParent);
    }

    BOOL APIENTRY
    RichMenu_ShowPopup(HRICHMENU hRichMenu, UINT uFlags, INT x, INT y, HWND hwnd,
                       LPCRECT prcExclude OPTIONAL)
    {
        auto pRichMenu = ((RichMenu*)hRichMenu);
        if (!pRichMenu)
            return FALSE;

        pRichMenu->ShowPopup(uFlags, x, y, hwnd, prcExclude);
        return TRUE;
    }

    BOOL APIENTRY RichMenu_HidePopup(HRICHMENU hRichMenu)
    {
        auto pRichMenu = ((RichMenu*)hRichMenu);
        if (!pRichMenu)
            return FALSE;

        pRichMenu->HidePopup();
        return TRUE;
    }

    BOOL APIENTRY RichMenu_CloseHandle(HRICHMENU hRichMenu)
    {
        auto pRichMenu = ((RichMenu*)hRichMenu);
        if (!pRichMenu)
            return FALSE;

        pRichMenu->Release();
        return TRUE;
    }

    BOOL APIENTRY RichMenu_CheckItem(HRICHMENU hRichMenu, INT iItem, UINT uCheck)
    {
        auto pRichMenu = ((RichMenu*)hRichMenu);
        if (!pRichMenu)
            return FALSE;
        return pRichMenu->CheckItem(iItem, uCheck);
    }

    BOOL APIENTRY
    RichMenu_CheckRadioItem(HRICHMENU hRichMenu, INT iFirst, INT iLast,
                            INT iCheck, UINT uFlags)
    {
        auto pRichMenu = ((RichMenu*)hRichMenu);
        if (!pRichMenu)
            return FALSE;
        return pRichMenu->CheckRadioItem(iFirst, iLast, iCheck, uFlags);
    }

    BOOL APIENTRY RichMenu_SetLogFont(HRICHMENU hRichMenu, LPLOGFONT plf OPTIONAL)
    {
        auto pRichMenu = ((RichMenu*)hRichMenu);
        if (!pRichMenu)
            return FALSE;
        return pRichMenu->SetLogFont(plf);
    }

    BOOL APIENTRY RichMenu_EnableItem(HRICHMENU hRichMenu, INT iItem, UINT uEnable)
    {
        auto pRichMenu = ((RichMenu*)hRichMenu);
        if (!pRichMenu)
            return FALSE;
        return pRichMenu->EnableItem(iItem, uEnable);
    }
} // extern "C"
