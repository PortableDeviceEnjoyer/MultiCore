/*
 * PROJECT:     ReactOS OneCore
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     OneCore API
 * COPYRIGHT:   Copyright 2026 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shlguid_undoc.h>
#include <shlobj_undoc.h>
#include <shlwapi_undoc.h>
#include <strsafe.h>

static PWSTR _PathGetArgsLikeCreateProcess(PCWSTR lpString)
{
    PWSTR pch;
    if (*lpString == L'"')
        pch = wcschr(lpString + 1, L'"');
    else
        pch = wcschr(lpString, L' ');
    if (pch)
        return pch + 1;
    return (PWSTR)&lpString[lstrlenW(lpString)];
}

static HRESULT _PathCopyExeAndTrim(PWSTR pszBuff, size_t cchBuff, PCWSTR pszSrc, size_t cchSrc)
{
    *pszBuff = UNICODE_NULL;
    HRESULT hr = StringCchCatNW(pszBuff, cchBuff, pszSrc, cchSrc);
    if (SUCCEEDED(hr))
        StrTrimW(pszBuff, L" \t");
    return hr;
}

static BOOL _PathMatchesSuspicious(PCWSTR lpString)
{
    WCHAR pszPath[MAX_PATH];
    INT cch = lstrlenW(lpString);
    SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, 0, pszPath);
    return _wcsnicmp(lpString, pszPath, cch) == 0;
}

// This function attempts to find where the "arguments" portion of a command-line path string
static PWSTR _PathGuessNextBestArgs(PWSTR pszPath)
{
    PWSTR pSpaceStart = NULL;
    BOOL bValid = TRUE;
    const DWORD PATH_VALID_CHARS = (
        PATH_CHAR_CLASS_DOT | PATH_CHAR_CLASS_SEMICOLON | PATH_CHAR_CLASS_COMMA |
        PATH_CHAR_CLASS_SPACE | PATH_CHAR_CLASS_OTHER_VALID);

    for (;;)
    {
        const WCHAR ch = *pszPath;
        if (!ch)
            break;

        switch (ch)
        {
            case L' ':
                if (!pSpaceStart)
                    pSpaceStart = pszPath;
                break;

            case L'"':
            case L'%':
                bValid = FALSE;
                break;

            case L'\\':
                bValid = !PathIsUNCW(pszPath);
                if (bValid)
                    pSpaceStart = NULL;
                break;

            default:
                bValid = PathIsValidCharW(ch, PATH_VALID_CHARS);
                break;
        }

        if (!bValid)
            break;

        ++pszPath;
    }

    if (pSpaceStart)
    {
        while (*pSpaceStart == L' ')
            ++pSpaceStart;
        return pSpaceStart;
    }

    return bValid ? pszPath : NULL;
}

static DWORD SHWindowsPolicy(REFGUID rpolid, DWORD dwDefaultValue)
{
    DWORD dwData, cbData = sizeof(dwData);
    HRESULT hr = SHWindowsPolicyGetValue(rpolid, &dwData, &cbData);
    if (FAILED(hr))
        return dwDefaultValue;
    return dwData;
}

static inline HRESULT SHCoAlloc(SIZE_T cb, PVOID* ppData)
{
    *ppData = CoTaskMemAlloc(cb);
    return *ppData ? S_OK : E_OUTOFMEMORY;
}

static inline BOOL _PathAppend(PCWSTR key1, PCWSTR key2, PWSTR pszDest, size_t cchDest)
{
    return SUCCEEDED(StringCchCopyW(pszDest, cchDest, key1)) &&
           SUCCEEDED(StringCchCatW(pszDest, cchDest, L"\\")) &&
           SUCCEEDED(StringCchCatW(pszDest, cchDest, key2));
}

static VOID _MakeAppPathKey(PCWSTR pszPath, PWSTR pszDest, UINT cchDest)
{
    if (_PathAppend(L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths",
                    pszPath, pszDest, cchDest))
    {
        if (!*PathFindExtensionW(pszPath))
            StringCchCatW(pszDest, cchDest, L".exe");
    }
}

static BOOL _GetAppPath(PCWSTR pszPath, PWSTR pszValue, DWORD cchValue)
{
    WCHAR szSubKey[MAX_PATH];
    _MakeAppPathKey(pszPath, szSubKey, _countof(szSubKey));
    DWORD cbData = cchValue * sizeof(WCHAR);
    LSTATUS error = SHGetValueW(HKEY_LOCAL_MACHINE, szSubKey, NULL, NULL, pszValue, &cbData);
    return error == ERROR_SUCCESS;
}

static HRESULT _PathExeExists(LPWSTR pszPath)
{
    DWORD dwWhich = WHICH_PIF | WHICH_COM | WHICH_EXE | WHICH_BAT | WHICH_CMD | WHICH_OPTIONAL;
    DWORD attrs;
    if (!PathFileExistsDefExtAndAttributesW(pszPath, dwWhich, &attrs) ||
        (attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        return CO_E_APPNOTFOUND;
    }
    return S_OK;
}

static HRESULT _PathFindInFolder(INT csidl, STRSAFE_LPCWSTR pszSrc, LPWSTR pszPath, size_t cchDest)
{
    HRESULT hr = SHGetFolderPathW(0, csidl, 0, 0, pszPath);
    if (FAILED(hr))
        return hr;

    StringCchCatW(pszPath, cchDest, L"\\");
    hr = StringCchCatW(pszPath, cchDest, pszSrc);
    if (FAILED(hr))
        return hr;

    return _PathExeExists(pszPath);
}

static HRESULT _PathFindInSystem(PWSTR pszPath, UINT cchPath)
{
    WCHAR szPath[MAX_PATH];
    HRESULT hr = _PathFindInFolder(CSIDL_SYSTEM, pszPath, szPath, _countof(szPath));
    if (SUCCEEDED(hr))
        return StringCchCopyW(pszPath, cchPath, szPath);

    hr = _PathFindInFolder(CSIDL_WINDOWS, pszPath, szPath, 260u);
    if (FAILED(hr))
        return hr;

    return StringCchCopyW(pszPath, cchPath, szPath);
}

static inline BOOL PathIsAbsolute(LPCWSTR pszPath)
{
    return PathIsUNCW(pszPath) || (PathGetDriveNumberW(pszPath) != -1 && pszPath[2] == L'\\');
}

/*************************************************************************
 * SHEvaluateSystemCommandTemplate [SHELL32.482] (Vista+)
 * SHEvaluateSystemCommandTemplate [SHLWAPI.552] (XP SP1 and SP2)
 *
 * https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shevaluatesystemcommandtemplate
 */
EXTERN_C
HRESULT WINAPI
SHEvaluateSystemCommandTemplate(
    _In_ PCWSTR pszCmdTemplate,
    _Outptr_ PWSTR *ppszApplication,
    _Outptr_opt_ PWSTR *ppszCommandLine,
    _Outptr_opt_ PWSTR *ppszParameters)
{
    HRESULT hr;
    WCHAR szExe[MAX_PATH];
    PWSTR pszArgs = _PathGetArgsLikeCreateProcess(pszCmdTemplate);
    BOOL bQuoted;

    UINT cchArgs = (UINT)(pszArgs - pszCmdTemplate);
    hr = _PathCopyExeAndTrim(szExe, _countof(szExe), pszCmdTemplate, cchArgs);
    if (FAILED(hr))
        goto Exit;

    // Unquote if necessary
    bQuoted = (szExe[0] == L'"');
    if (bQuoted)
        PathUnquoteSpacesW(szExe);

    if (PathIsAbsolute(szExe))
    {
        if (bQuoted)
        {
            hr = _PathExeExists(szExe);
        }
        else // Not quoted
        {
            if (_PathMatchesSuspicious(szExe)) // ProgramFiles-likely?
                hr = E_ACCESSDENIED;
            else
                hr = _PathExeExists(szExe);
        }

        // Detect the best position
        while (FAILED(hr))
        {
            if (bQuoted || !*pszArgs)
                break;

            pszArgs = _PathGuessNextBestArgs(pszArgs);
            if (!pszArgs)
                break;

            cchArgs = (UINT)(pszArgs - pszCmdTemplate);
            hr = _PathCopyExeAndTrim(szExe, _countof(szExe), pszCmdTemplate, cchArgs);
            if (FAILED(hr))
                break;

            hr = _PathExeExists(szExe);
        }
    }
    else
    {
        if (!PathIsFileSpecW(szExe))
        {
            hr = E_ACCESSDENIED;
            goto Exit;
        }

        if (_GetAppPath(szExe, szExe, _countof(szExe)))
        {
            hr = S_OK;
        }
        else if (SHWindowsPolicy(POLID_UsePathEnvVarForCommandTemplates, FALSE))
        {
            const DWORD PATH_VALID_CHARS = (
                PATH_CHAR_CLASS_DOT | PATH_CHAR_CLASS_SEMICOLON | PATH_CHAR_CLASS_COMMA |
                PATH_CHAR_CLASS_SPACE | PATH_CHAR_CLASS_OTHER_VALID);
            hr = PathFindOnPathExW(szExe, NULL, PATH_VALID_CHARS) ? S_OK : CO_E_APPNOTFOUND;
        }
        else
        {
            hr = _PathFindInSystem(szExe, _countof(szExe));
        }

        pszArgs = PathFindFileNameW(szExe);
    }

Exit:
    *ppszApplication = NULL;
    if (ppszCommandLine)
        *ppszCommandLine = NULL;
    if (ppszParameters)
        *ppszParameters = NULL;

    static WCHAR szEmpty[] = L"";
    if (!pszArgs)
        pszArgs = szEmpty;

    // Create output strings
    if (SUCCEEDED(hr))
        hr = SHStrDupW(szExe, ppszApplication);

    if (SUCCEEDED(hr) && ppszCommandLine)
    {
        size_t cch = lstrlenW(szExe) + lstrlenW(pszArgs) + 4;
        hr = SHCoAlloc(cch * sizeof(WCHAR), (PVOID*)ppszCommandLine);
        if (SUCCEEDED(hr))
            hr = StringCchPrintfW(*ppszCommandLine, cch, L"\"%s\" %s", szExe, pszArgs);
    }

    if (SUCCEEDED(hr) && ppszParameters)
        hr = SHStrDupW(pszArgs, ppszParameters);

    if (FAILED(hr))
    {
        // Clean up
        if (*ppszApplication)
        {
            CoTaskMemFree(*ppszApplication);
            *ppszApplication = NULL;
        }
        if (ppszCommandLine && *ppszCommandLine)
        {
            CoTaskMemFree(*ppszCommandLine);
            *ppszCommandLine = NULL;
        }
    }

    return hr;
}
