// Intercepts RegOpenKeyEx / RegQueryValueEx to fake NLS registry entries.
//
// Strategy:
//   Hook RegOpenKeyExW/A to record which HKEY handles correspond to NLS paths.
//   Hook RegQueryValueExW/A to substitute our Chinese values for those handles.
//   Hook RegCloseKey to evict closed handles from the tracking map.

#include <windows.h>
#include <string>
#include <map>
#include <mutex>
#include <algorithm>

#include "MinHook.h"
#include "registry_hooks.h"

// ---- faked value table -------------------------------------------------------

struct FakedEntry { const wchar_t* path; const wchar_t* name; const wchar_t* value; };

// All paths and names stored lowercase; compared after normalisation.
static const FakedEntry g_faked[] = {
    // Code page
    { L"hklm\\system\\currentcontrolset\\control\\nls\\codepage", L"acp",             L"936"      },
    { L"hklm\\system\\currentcontrolset\\control\\nls\\codepage", L"oemcp",            L"936"      },
    // Language IDs
    { L"hklm\\system\\currentcontrolset\\control\\nls\\language", L"default",          L"0804"     },
    { L"hklm\\system\\currentcontrolset\\control\\nls\\language", L"installlanguage",  L"0804"     },
    // User locale
    { L"hkcu\\control panel\\international",                       L"locale",           L"00000804" },
    { L"hkcu\\control panel\\international",                       L"slanguage",        L"chs"      },
    { nullptr, nullptr, nullptr }
};

// ---- handle → path map -------------------------------------------------------

static std::map<HKEY, std::wstring> g_keys;
static std::mutex                   g_keys_mutex;

static std::wstring Lower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

static std::wstring RootStr(HKEY h)
{
    if (h == HKEY_LOCAL_MACHINE) return L"hklm";
    if (h == HKEY_CURRENT_USER)  return L"hkcu";
    if (h == HKEY_CLASSES_ROOT)  return L"hkcr";
    if (h == HKEY_USERS)         return L"hku";
    return L"";
}

static void TrackKey(HKEY h, std::wstring path)
{
    std::lock_guard<std::mutex> lk(g_keys_mutex);
    g_keys[h] = std::move(path);
}

static void UntrackKey(HKEY h)
{
    std::lock_guard<std::mutex> lk(g_keys_mutex);
    g_keys.erase(h);
}

static std::wstring LookupKey(HKEY h)
{
    std::lock_guard<std::mutex> lk(g_keys_mutex);
    auto it = g_keys.find(h);
    return it != g_keys.end() ? it->second : L"";
}

// Build a normalised full path from an open/predefined root plus an optional subkey.
static std::wstring MakePath(HKEY root, const wchar_t* sub)
{
    std::wstring base = RootStr(root);
    if (base.empty()) base = LookupKey(root);   // root is an opened handle
    if (base.empty()) return L"";
    if (sub && sub[0]) return base + L"\\" + Lower(sub);
    return base;
}

// ---- intercept helper --------------------------------------------------------

// Returns true and sets *out if we are faking this (keyPath, valueName) pair.
// Writes REG_SZ wide data into lpData / lpcbData following RegQueryValueEx semantics.
static bool TryFakeW(const std::wstring& keyPath, LPCWSTR valueName,
                     LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData, LSTATUS* out)
{
    if (keyPath.empty() || !valueName) return false;
    const std::wstring normName = Lower(valueName);

    for (const FakedEntry* fe = g_faked; fe->path; ++fe) {
        if (keyPath != fe->path || normName != fe->name) continue;

        if (lpType) *lpType = REG_SZ;
        const DWORD needed = (DWORD)(wcslen(fe->value) + 1) * sizeof(wchar_t);

        if (lpcbData) {
            const DWORD avail = *lpcbData;
            *lpcbData = needed;
            if (!lpData)        { *out = ERROR_SUCCESS;   return true; }
            if (avail < needed) { *out = ERROR_MORE_DATA; return true; }
        }
        if (lpData) memcpy(lpData, fe->value, needed);
        *out = ERROR_SUCCESS;
        return true;
    }
    return false;
}

// ---- hook typedefs & originals -----------------------------------------------

typedef LSTATUS (WINAPI *PFN_RegOpenKeyExW   )(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
typedef LSTATUS (WINAPI *PFN_RegOpenKeyExA   )(HKEY, LPCSTR,  DWORD, REGSAM, PHKEY);
typedef LSTATUS (WINAPI *PFN_RegQueryValueExW)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LSTATUS (WINAPI *PFN_RegQueryValueExA)(HKEY, LPCSTR,  LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LSTATUS (WINAPI *PFN_RegCloseKey     )(HKEY);

static PFN_RegOpenKeyExW    Orig_RegOpenKeyExW    = nullptr;
static PFN_RegOpenKeyExA    Orig_RegOpenKeyExA    = nullptr;
static PFN_RegQueryValueExW Orig_RegQueryValueExW = nullptr;
static PFN_RegQueryValueExA Orig_RegQueryValueExA = nullptr;
static PFN_RegCloseKey      Orig_RegCloseKey      = nullptr;

// ---- hooks -------------------------------------------------------------------

LSTATUS WINAPI Hook_RegOpenKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions,
                                   REGSAM sam, PHKEY phkResult)
{
    LSTATUS ret = Orig_RegOpenKeyExW(hKey, lpSubKey, ulOptions, sam, phkResult);
    if (ret == ERROR_SUCCESS && phkResult && *phkResult) {
        std::wstring path = MakePath(hKey, lpSubKey);
        if (!path.empty()) TrackKey(*phkResult, path);
    }
    return ret;
}

LSTATUS WINAPI Hook_RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions,
                                   REGSAM sam, PHKEY phkResult)
{
    LSTATUS ret = Orig_RegOpenKeyExA(hKey, lpSubKey, ulOptions, sam, phkResult);
    if (ret == ERROR_SUCCESS && phkResult && *phkResult) {
        std::wstring wSub;
        if (lpSubKey && lpSubKey[0]) {
            int n = MultiByteToWideChar(CP_ACP, 0, lpSubKey, -1, nullptr, 0);
            wSub.resize(n);
            MultiByteToWideChar(CP_ACP, 0, lpSubKey, -1, &wSub[0], n);
        }
        std::wstring path = MakePath(hKey, wSub.empty() ? nullptr : wSub.c_str());
        if (!path.empty()) TrackKey(*phkResult, path);
    }
    return ret;
}

LSTATUS WINAPI Hook_RegQueryValueExW(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved,
                                      LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
    const std::wstring keyPath = LookupKey(hKey);
    LSTATUS result;
    if (TryFakeW(keyPath, lpValueName, lpType, lpData, lpcbData, &result))
        return result;
    return Orig_RegQueryValueExW(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
}

LSTATUS WINAPI Hook_RegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved,
                                      LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
    const std::wstring keyPath = LookupKey(hKey);
    if (!keyPath.empty() && lpValueName && lpValueName[0]) {
        int n = MultiByteToWideChar(CP_ACP, 0, lpValueName, -1, nullptr, 0);
        std::wstring wName(n, L'\0');
        MultiByteToWideChar(CP_ACP, 0, lpValueName, -1, &wName[0], n);

        const std::wstring normName = Lower(wName);
        for (const FakedEntry* fe = g_faked; fe->path; ++fe) {
            if (keyPath != fe->path || normName != fe->name) continue;

            if (lpType) *lpType = REG_SZ;
            // Caller expects ANSI REG_SZ — convert our wide value
            int needed = WideCharToMultiByte(CP_ACP, 0, fe->value, -1,
                                             nullptr, 0, nullptr, nullptr);
            if (lpcbData) {
                DWORD avail = *lpcbData;
                *lpcbData   = (DWORD)needed;
                if (!lpData)            return ERROR_SUCCESS;
                if (avail < (DWORD)needed) return ERROR_MORE_DATA;
            }
            if (lpData)
                WideCharToMultiByte(CP_ACP, 0, fe->value, -1,
                                    (LPSTR)lpData, needed, nullptr, nullptr);
            return ERROR_SUCCESS;
        }
    }
    return Orig_RegQueryValueExA(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
}

LSTATUS WINAPI Hook_RegCloseKey(HKEY hKey)
{
    UntrackKey(hKey);
    return Orig_RegCloseKey(hKey);
}

// ---- installation ------------------------------------------------------------

static LPVOID FindReg(const char* name)
{
    LPVOID p = (LPVOID)GetProcAddress(GetModuleHandleW(L"advapi32.dll"),  name);
    if (!p) p = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), name);
    return p;
}

#define INSTALL_REG(fn, orig) \
    do { LPVOID _t = FindReg(#fn); if (_t) MH_CreateHook(_t, (LPVOID)Hook_##fn, (LPVOID*)&orig); } while(0)

bool InstallRegistryHooks()
{
    INSTALL_REG(RegOpenKeyExW,    Orig_RegOpenKeyExW);
    INSTALL_REG(RegOpenKeyExA,    Orig_RegOpenKeyExA);
    INSTALL_REG(RegQueryValueExW, Orig_RegQueryValueExW);
    INSTALL_REG(RegQueryValueExA, Orig_RegQueryValueExA);
    INSTALL_REG(RegCloseKey,      Orig_RegCloseKey);
    return true;
}

void RemoveRegistryHooks()
{
    std::lock_guard<std::mutex> lk(g_keys_mutex);
    g_keys.clear();
}
