/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     LGPL-2.0-or-later (https://spdx.org/licenses/LGPL-2.0-or-later)
 * PURPOSE:     RichMenu: lock-free, activation-free, easily-choosable fake menu control
 * COPYRIGHT:   Copyright 2022 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */
/* richmenu.h --- RichMenu by katahiromz
 *
 * - Lock-free,
 * - Activation-free,
 * - Easily-choosable menu control.
 *
 */
#pragma once

#define RICHMENU_CLASSNAMEA "ReactOS RichMenu"
#define RICHMENU_CLASSNAMEW L"ReactOS RichMenu"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NDEBUG
    DECLARE_HANDLE(HRICHMENU);
#else
    struct tagRICHMENU;
    typedef struct tagRICHMENU *HRICHMENU;
#endif

VOID APIENTRY RichMenu_Init(VOID);
HRICHMENU APIENTRY RichMenu_FromHMENU(HMENU hMenu, HRICHMENU hParent OPTIONAL);
BOOL APIENTRY RichMenu_SetLogFont(HRICHMENU hRichMenu, LPLOGFONT plf OPTIONAL);
BOOL APIENTRY RichMenu_ShowPopup(HRICHMENU hRichMenu, UINT uFlags, INT x, INT y, HWND hwnd,
                                 LPCRECT prcExclude OPTIONAL);
BOOL APIENTRY RichMenu_HidePopup(HRICHMENU hRichMenu);
BOOL APIENTRY RichMenu_CloseHandle(HRICHMENU hRichMenu);
BOOL APIENTRY RichMenu_CheckItem(HRICHMENU hRichMenu, INT iItem, UINT uCheck);
BOOL APIENTRY RichMenu_CheckRadioItem(HRICHMENU hRichMenu, INT iFirst, INT iLast,
                                      INT iCheck, UINT uFlags);
BOOL APIENTRY RichMenu_EnableItem(HRICHMENU hRichMenu, INT iItem, UINT uEnable);
VOID APIENTRY RichMenu_Exit(VOID);

#ifdef __cplusplus
} // extern "C"
#endif

#ifdef UNICODE
    #define RICHMENU_CLASSNAME RICHMENU_CLASSNAMEW
#else
    #define RICHMENU_CLASSNAME RICHMENU_CLASSNAMEA
#endif
