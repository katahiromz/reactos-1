/*
 * PROJECT:     ReactOS Keyboard Layouts
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Multiple keyboard table switcher for Japanese and Korean
 * COPYRIGHT:   Copyright 2026 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winuser.h>
#include <winreg.h>
#include <wchar.h>
#include <ndk/ntndk.h>
#include <ndk/kbd.h>
#include <ntstrsafe.h>

/* KBDTABLE_DESC.dwType */
#define KBD_TYPE_IBM_ENHANCED 4
#define KBD_TYPE_JAPANESE     7
#define KBD_TYPE_KOREAN       8

/* KBDTABLE_DESC.dwSubType (values seen in kbdjpn.dll / kbdkor.dll) */
#define KBD_SUBTYPE_ENGLISH_101  0
#define KBD_SUBTYPE_JAPANESE_106 2
#define KBD_SUBTYPE_KOREAN_103   6
#define KBD_SUBTYPE_JAPANESE_NEC 0x0D02

typedef struct tagCLIENTKEYBOARDTYPE
{
    ULONG Type;
    ULONG SubType;
    ULONG FunctionKey;
} CLIENTKEYBOARDTYPE, *PCLIENTKEYBOARDTYPE;

#define I8042PRT_PARAMS \
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\i8042prt\\Parameters"
#define TS_KBDTYPE_MAPPING \
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Terminal Server\\KeyboardType Mapping\\"
#define KBD_DYNAMIC_TABLES_KEY \
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Keyboard Layout\\Dynamic Tables\\"

/* Subkey under "Dynamic Tables". NOTE: the original kbdkor.dll also uses "kbdjpn". */
#ifndef KBD_DYNAMIC_TABLES_NAME
    #define KBD_DYNAMIC_TABLES_NAME L"kbdjpn"
#endif

/* Reads REG_SZ-like value pwszValueName under pwszKeyPath into pszOut */
static BOOL
QueryRealDllName(
    _In_ PCWSTR pwszKeyPath,
    _In_ PCWSTR pwszValueName,
    _Out_writes_(cchOut) PWCHAR pszOut,
    _In_ UINT cchOut)
{
    UNICODE_STRING KeyName, ValueName;
    OBJECT_ATTRIBUTES oa;
    HANDLE hKey;
    NTSTATUS Status;
    ULONG cbResult = 0, cbEnd;
    ULONGLONG aullBuf[64];
    PBYTE pbBuf = (PBYTE)aullBuf;
    PKEY_VALUE_FULL_INFORMATION pInfo = (PKEY_VALUE_FULL_INFORMATION)aullBuf;

    RtlInitUnicodeString(&KeyName, pwszKeyPath);
    InitializeObjectAttributes(&oa, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&hKey, KEY_READ, &oa);
    if (!NT_SUCCESS(Status))
        return FALSE;

    RtlInitUnicodeString(&ValueName, pwszValueName);
    Status = NtQueryValueKey(hKey, &ValueName, KeyValueFullInformation,
                             pbBuf, sizeof(aullBuf) - sizeof(WCHAR), &cbResult);
    NtClose(hKey);
    if (!NT_SUCCESS(Status))
        return FALSE;
    if (!pInfo->DataLength || pInfo->Type != REG_SZ)
        return FALSE;
    if ((pInfo->DataOffset | pInfo->DataLength) % sizeof(WCHAR))
        return FALSE;

    /* original trusts DataOffset/DataLength; we bound them */
    cbEnd = pInfo->DataOffset + pInfo->DataLength;
    if (cbEnd < pInfo->DataOffset || cbEnd > cbResult || cbEnd > sizeof(aullBuf) - sizeof(WCHAR))
        return FALSE;

    ((PWCHAR)pbBuf)[cbEnd / sizeof(WCHAR)] = UNICODE_NULL;

    Status = RtlStringCchCopyW(pszOut, cchOut, (PCWSTR)(pbBuf + pInfo->DataOffset));
    return NT_SUCCESS(Status);
}

/* Terminal Server (Hydra) path: pClientKbdType != NULL */
static BOOL
KbdLayerRealDllFileForWBT(
    _In_ HKL hKL,
    _Out_writes_(cchRealDllName) PWCHAR realDllName,
    _In_ UINT cchRealDllName,
    _In_ PCLIENTKEYBOARDTYPE pClientKbdType)
{
    WCHAR wszKey[MAX_PATH], wszValue[8 + 4 + 1];
    WORD wLang = PRIMARYLANGID(HandleToUlong(hKL));
    NTSTATUS Status;

    /* Fill the default values */
    if (wLang == LANG_JAPANESE)
    {
        RtlStringCchCopyW(realDllName, cchRealDllName, L"kbd101.dll");
        RtlStringCbCopyW(wszKey, sizeof(wszKey), TS_KBDTYPE_MAPPING L"JPN");
    }
    else if (wLang == LANG_KOREAN)
    {
        RtlStringCchCopyW(realDllName, cchRealDllName, L"kbd101a.dll");
        RtlStringCbCopyW(wszKey, sizeof(wszKey), TS_KBDTYPE_MAPPING L"KOR");
    }
    else
    {
        return FALSE;
    }

    if (pClientKbdType->FunctionKey >= 10000)
        return FALSE;

    Status = RtlStringCbPrintfW(wszValue, sizeof(wszValue), L"%08X%04u",
                                pClientKbdType->SubType, pClientKbdType->FunctionKey);
    if (!NT_SUCCESS(Status))
        return FALSE;

    /* Read from registry */
    if (!QueryRealDllName(wszKey, wszValue, realDllName, cchRealDllName))
    {
        /* Re-try with truncated value name */
        wszValue[8] = UNICODE_NULL;
        QueryRealDllName(wszKey, wszValue, realDllName, cchRealDllName);
    }

    return TRUE;
}

/* ------------------------------------------------------------------------
 * KbdLayerRealDllFile @5
 * ------------------------------------------------------------------------ */
BOOL WINAPI
KbdLayerRealDllFile(
    _In_ HKL hKL,
    _Out_writes_(MAX_PATH) PWCHAR realDllName,
    _In_opt_ PCLIENTKEYBOARDTYPE pClientKbdType,
    _In_opt_ PVOID reserved)
{
    WORD wLang;
    PCWSTR suffix;
    WCHAR wszValue[16];

    UNREFERENCED_PARAMETER(reserved);

    if (pClientKbdType)
        return KbdLayerRealDllFileForWBT(hKL, realDllName, MAX_PATH, pClientKbdType);

    wLang = PRIMARYLANGID(HandleToUlong(hKL));
    if (wLang == LANG_JAPANESE)
        suffix = L" JPN";
    else if (wLang == LANG_KOREAN)
        suffix = L" KOR";
    else
        suffix = L"";

    RtlStringCbCopyW(wszValue, sizeof(wszValue), L"LayerDriver");
    RtlStringCbCatW(wszValue, sizeof(wszValue), suffix);

    return QueryRealDllName(I8042PRT_PARAMS, wszValue, realDllName, MAX_PATH);
}

/* ------------------------------------------------------------------------
 * KbdLayerRealDllFileNT4 @3
 * ------------------------------------------------------------------------ */
BOOL WINAPI
KbdLayerRealDllFileNT4(
    _Out_writes_(MAX_PATH) PWCHAR realDllName)
{
    return QueryRealDllName(I8042PRT_PARAMS, L"LayerDriver", realDllName, MAX_PATH);
}

static BOOL
ParseDynamicTableEntry(
    _Out_ PKBDTABLE_MULTI pMulti,
    _In_ PKEY_VALUE_FULL_INFORMATION pInfo,
    _In_ ULONG cbInfo)
{
    PKBDTABLE_DESC pDesc = &pMulti->aKbdTables[pMulti->nTables];
    const DWORD *pdwData;
    PWCHAR pch;

    if (pInfo->Type != REG_BINARY ||
        !pInfo->NameLength ||
        pInfo->NameLength >= sizeof(pDesc->wszDllName) ||
        pInfo->NameLength % sizeof(WCHAR) ||
        pInfo->DataLength != 3 * sizeof(DWORD) ||
        pInfo->DataOffset > cbInfo ||
        cbInfo - pInfo->DataOffset < pInfo->DataLength)
    {
        return FALSE;
    }

    /* Value names are not NUL-terminated. (The original only terminates
       element [31]; we terminate right after the name.) */
    RtlCopyMemory(pDesc->wszDllName, pInfo->Name, pInfo->NameLength);
    pDesc->wszDllName[pInfo->NameLength / sizeof(WCHAR)] = UNICODE_NULL;

    /* "kbd106.dll,something" -> "kbd106.dll" */
    pch = wcschr(pDesc->wszDllName, L',');
    if (pch)
        *pch = UNICODE_NULL;

    pdwData = (const DWORD *)((PBYTE)pInfo + pInfo->DataOffset);
    if (pdwData[0] != 0)
        return FALSE;

    pDesc->dwType = pdwData[1];
    pDesc->dwSubType = pdwData[2];
    return TRUE;
}

/* TRUE if at least one table was read. A malformed entry discards everything. */
static BOOL
LoadDynamicTables(_In_ PCWSTR pwszName, _Out_ PKBDTABLE_MULTI pMulti)
{
    WCHAR wszPath[260];
    UNICODE_STRING KeyName;
    OBJECT_ATTRIBUTES oa;
    HANDLE hKey;
    NTSTATUS Status;
    ULONG cbResult;
    ULONGLONG aullBuf[0x400 / sizeof(ULONGLONG)]; /* aligned */

    Status = RtlStringCbCopyW(wszPath, sizeof(wszPath), KBD_DYNAMIC_TABLES_KEY);
    if (!NT_SUCCESS(Status))
        return FALSE;
    Status = RtlStringCbCatW(wszPath, sizeof(wszPath), pwszName);
    if (!NT_SUCCESS(Status))
        return FALSE;

    RtlInitUnicodeString(&KeyName, wszPath);
    InitializeObjectAttributes(&oa, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&hKey, KEY_READ, &oa);
    if (!NT_SUCCESS(Status))
        return FALSE;

    pMulti->nTables = 0;
    for (;;)
    {
        Status = NtEnumerateValueKey(hKey, pMulti->nTables, KeyValueFullInformation,
                                     aullBuf, sizeof(aullBuf), &cbResult);
        if (Status == STATUS_NO_MORE_ENTRIES)
            break;
        if (!NT_SUCCESS(Status))
        {
            pMulti->nTables = 0;
            break;
        }

        if (!ParseDynamicTableEntry(pMulti, (PKEY_VALUE_FULL_INFORMATION)aullBuf, cbResult))
        {
            pMulti->nTables = 0;
            break;
        }

        if (++pMulti->nTables >= KBDTABLE_MULTI_MAX)
            break;
    }

    NtClose(hKey);
    return pMulti->nTables != 0;
}

static VOID
SetDefaultTables(_Out_ PKBDTABLE_MULTI pMulti)
{
    static const struct { PCWSTR pwszDll; DWORD dwType, dwSubType; } s_Defaults[] =
    {
#ifdef KBD_KOREAN  /* kbdkor.dll */
        { L"kbd101a.dll", KBD_TYPE_IBM_ENHANCED, KBD_SUBTYPE_ENGLISH_101 },
        { L"kbd103.dll",  KBD_TYPE_KOREAN,       KBD_SUBTYPE_KOREAN_103 },
#else              /* kbdjpn.dll */
        { L"kbd101.dll",  KBD_TYPE_IBM_ENHANCED, KBD_SUBTYPE_ENGLISH_101 },
        { L"kbd106.dll",  KBD_TYPE_JAPANESE,     KBD_SUBTYPE_JAPANESE_106 },
        { L"kbdnec.dll",  KBD_TYPE_JAPANESE,     KBD_SUBTYPE_JAPANESE_NEC },
#endif
    };
    SIZE_T i;

    RtlZeroMemory(pMulti, sizeof(*pMulti));
    pMulti->nTables = ARRAYSIZE(s_Defaults);
    for (i = 0; i < ARRAYSIZE(s_Defaults); ++i)
    {
        RtlStringCbCopyW(pMulti->aKbdTables[i].wszDllName,
                         sizeof(pMulti->aKbdTables[i].wszDllName),
                         s_Defaults[i].pwszDll);
        pMulti->aKbdTables[i].dwType = s_Defaults[i].dwType;
        pMulti->aKbdTables[i].dwSubType = s_Defaults[i].dwSubType;
    }
}

/* ------------------------------------------------------------------------
 * KbdLayerMultiDescriptor @6
 * ---------------------------------------------------------------------- */
BOOL WINAPI KbdLayerMultiDescriptor(_Out_ PKBDTABLE_MULTI pMulti)
{
    if (!pMulti)
        return FALSE;
    RtlZeroMemory(pMulti, sizeof(*pMulti));
    if (!LoadDynamicTables(KBD_DYNAMIC_TABLES_NAME, pMulti))
        SetDefaultTables(pMulti);
    return TRUE;
}
