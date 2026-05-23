// Hooks Windows NLS APIs to report Chinese Simplified (LCID 0x0804, ACP 936).
//
// Why we go beyond GetACP():
//   MultiByteToWideChar(CP_ACP), WideCharToMultiByte(CP_ACP), and
//   GetClipboardData(CF_TEXT) all read the NLS code-page table in kernel32
//   directly — they do NOT call GetACP(). Hooking GetACP() alone is therefore
//   insufficient; we must also intercept these three functions.

#include <windows.h>
#include <mutex>
#include "MinHook.h"
#include "locale_hooks.h"

static const UINT   CN_CP     = 936;
static const LCID   CN_LCID   = 0x0804;   // Chinese Simplified, PRC
static const LANGID CN_LANGID = 0x0804;   // MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)

// ---- typedefs ---------------------------------------------------------------

typedef UINT   (WINAPI *PFN_GetACP)();
typedef UINT   (WINAPI *PFN_GetOEMCP)();
typedef LCID   (WINAPI *PFN_LCID_fn)();
typedef LANGID (WINAPI *PFN_LANGID_fn)();
typedef int    (WINAPI *PFN_GetLocaleInfoA)(LCID, LCTYPE, LPSTR,  int);
typedef int    (WINAPI *PFN_GetLocaleInfoW)(LCID, LCTYPE, LPWSTR, int);
typedef int    (WINAPI *PFN_MultiByteToWideChar)(UINT, DWORD, LPCCH,  int, LPWSTR, int);
typedef int    (WINAPI *PFN_WideCharToMultiByte)(UINT, DWORD, LPCWCH, int, LPSTR, int, LPCCH, LPBOOL);
typedef HANDLE (WINAPI *PFN_GetClipboardData)(UINT);
typedef HANDLE (WINAPI *PFN_SetClipboardData)(UINT, HANDLE);

// ---- original pointers -------------------------------------------------------

static PFN_GetACP               Orig_GetACP                    = nullptr;
static PFN_GetOEMCP             Orig_GetOEMCP                  = nullptr;
static PFN_LCID_fn              Orig_GetUserDefaultLCID         = nullptr;
static PFN_LCID_fn              Orig_GetSystemDefaultLCID       = nullptr;
static PFN_LANGID_fn            Orig_GetUserDefaultLangID       = nullptr;
static PFN_LANGID_fn            Orig_GetSystemDefaultLangID     = nullptr;
static PFN_LANGID_fn            Orig_GetUserDefaultUILanguage   = nullptr;
static PFN_LANGID_fn            Orig_GetSystemDefaultUILanguage = nullptr;
static PFN_GetLocaleInfoA       Orig_GetLocaleInfoA             = nullptr;
static PFN_GetLocaleInfoW       Orig_GetLocaleInfoW             = nullptr;
static PFN_MultiByteToWideChar  Orig_MultiByteToWideChar        = nullptr;
static PFN_WideCharToMultiByte  Orig_WideCharToMultiByte        = nullptr;
static PFN_GetClipboardData     Orig_GetClipboardData           = nullptr;
static PFN_SetClipboardData     Orig_SetClipboardData           = nullptr;

// ---- hook implementations ---------------------------------------------------

UINT   WINAPI Hook_GetACP()                     { return CN_CP; }
UINT   WINAPI Hook_GetOEMCP()                   { return CN_CP; }
LCID   WINAPI Hook_GetUserDefaultLCID()         { return CN_LCID; }
LCID   WINAPI Hook_GetSystemDefaultLCID()       { return CN_LCID; }
LANGID WINAPI Hook_GetUserDefaultLangID()       { return CN_LANGID; }
LANGID WINAPI Hook_GetSystemDefaultLangID()     { return CN_LANGID; }
LANGID WINAPI Hook_GetUserDefaultUILanguage()   { return CN_LANGID; }
LANGID WINAPI Hook_GetSystemDefaultUILanguage() { return CN_LANGID; }

static LCID EffectiveLCID(LCID lcid)
{
    return (lcid == LOCALE_USER_DEFAULT || lcid == LOCALE_SYSTEM_DEFAULT)
           ? CN_LCID : lcid;
}

int WINAPI Hook_GetLocaleInfoA(LCID Locale, LCTYPE LCType, LPSTR lpLCData, int cchData)
{
    return Orig_GetLocaleInfoA(EffectiveLCID(Locale), LCType, lpLCData, cchData);
}

int WINAPI Hook_GetLocaleInfoW(LCID Locale, LCTYPE LCType, LPWSTR lpLCData, int cchData)
{
    return Orig_GetLocaleInfoW(EffectiveLCID(Locale), LCType, lpLCData, cchData);
}

// Redirect CP_ACP (0) → 936 so the actual NLS conversion uses GBK.
// Explicit code pages (e.g. 1252, 65001) pass through unchanged.
int WINAPI Hook_MultiByteToWideChar(UINT CodePage, DWORD dwFlags,
                                     LPCCH lpMultiByteStr, int cbMultiByte,
                                     LPWSTR lpWideCharStr, int cchWideChar)
{
    if (CodePage == CP_ACP) CodePage = CN_CP;
    return Orig_MultiByteToWideChar(CodePage, dwFlags,
                                    lpMultiByteStr, cbMultiByte,
                                    lpWideCharStr, cchWideChar);
}

int WINAPI Hook_WideCharToMultiByte(UINT CodePage, DWORD dwFlags,
                                     LPCWCH lpWideCharStr, int cchWideChar,
                                     LPSTR lpMultiByteStr, int cbMultiByte,
                                     LPCCH lpDefaultChar, LPBOOL lpUsedDefaultChar)
{
    if (CodePage == CP_ACP) CodePage = CN_CP;
    return Orig_WideCharToMultiByte(CodePage, dwFlags,
                                    lpWideCharStr, cchWideChar,
                                    lpMultiByteStr, cbMultiByte,
                                    lpDefaultChar, lpUsedDefaultChar);
}

// When CF_TEXT is requested Windows synthesises it from CF_UNICODETEXT using
// the system ACP (1252 on Western installs) — producing "????". We intercept
// and do the Unicode→GBK conversion ourselves so WoW gets valid GBK bytes.
static HGLOBAL   g_fake_clip = nullptr;
static std::mutex g_clip_mutex;

HANDLE WINAPI Hook_GetClipboardData(UINT uFormat)
{
    if (uFormat != CF_TEXT)
        return Orig_GetClipboardData(uFormat);

    // Fetch the authoritative Unicode text from the clipboard.
    HANDLE hUni = Orig_GetClipboardData(CF_UNICODETEXT);
    if (!hUni)
        return Orig_GetClipboardData(CF_TEXT);

    const wchar_t* wText = static_cast<const wchar_t*>(GlobalLock(hUni));
    if (!wText)
        return Orig_GetClipboardData(CF_TEXT);

    // Measure required GBK buffer, then encode.
    int cb = WideCharToMultiByte(CN_CP, 0, wText, -1, nullptr, 0, nullptr, nullptr);
    if (cb <= 0) { GlobalUnlock(hUni); return Orig_GetClipboardData(CF_TEXT); }

    std::lock_guard<std::mutex> lk(g_clip_mutex);
    if (g_fake_clip) { GlobalFree(g_fake_clip); g_fake_clip = nullptr; }
    g_fake_clip = GlobalAlloc(GMEM_MOVEABLE, cb);

    HANDLE result = Orig_GetClipboardData(CF_TEXT);   // fallback
    if (g_fake_clip) {
        char* buf = static_cast<char*>(GlobalLock(g_fake_clip));
        if (buf) {
            WideCharToMultiByte(CN_CP, 0, wText, -1, buf, cb, nullptr, nullptr);
            GlobalUnlock(g_fake_clip);
            result = g_fake_clip;
        }
    }
    GlobalUnlock(hUni);
    return result;
}

// When WoW copies in-game text it calls SetClipboardData(CF_TEXT, <GBK bytes>).
// Windows then synthesises CF_UNICODETEXT from those bytes using the *real*
// system ACP (1252) — corrupting the Unicode before our GetClipboardData hook
// can read it.  We intercept here and push a correct CF_UNICODETEXT ourselves
// (GBK → Unicode with CP 936) so the round-trip stays clean.
HANDLE WINAPI Hook_SetClipboardData(UINT uFormat, HANDLE hMem)
{
    HANDLE ret = Orig_SetClipboardData(uFormat, hMem);

    if (uFormat == CF_TEXT && hMem) {
        const char* src = static_cast<const char*>(GlobalLock(hMem));
        if (src) {
            int wlen = MultiByteToWideChar(CN_CP, 0, src, -1, nullptr, 0);
            if (wlen > 0) {
                HGLOBAL hUni = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
                if (hUni) {
                    wchar_t* dst = static_cast<wchar_t*>(GlobalLock(hUni));
                    if (dst) {
                        MultiByteToWideChar(CN_CP, 0, src, -1, dst, wlen);
                        GlobalUnlock(hUni);
                        // SetClipboardData takes ownership; only free on failure
                        if (!Orig_SetClipboardData(CF_UNICODETEXT, hUni))
                            GlobalFree(hUni);
                    } else {
                        GlobalFree(hUni);
                    }
                }
            }
            GlobalUnlock(hMem);
        }
    }

    return ret;
}

// ---- installation -----------------------------------------------------------

static LPVOID FindKernel(const char* name)
{
    LPVOID p = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),   name);
    if (!p) p = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), name);
    return p;
}

static LPVOID FindUser32(const char* name)
{
    return (LPVOID)GetProcAddress(GetModuleHandleW(L"user32.dll"), name);
}

#define INSTALL_K(fn, orig) \
    do { LPVOID _t = FindKernel(#fn); if (_t) MH_CreateHook(_t, (LPVOID)Hook_##fn, (LPVOID*)&orig); } while(0)

#define INSTALL_U(fn, orig) \
    do { LPVOID _t = FindUser32(#fn); if (_t) MH_CreateHook(_t, (LPVOID)Hook_##fn, (LPVOID*)&orig); } while(0)

bool InstallLocaleHooks()
{
    // Locale identity hooks (affect callers that invoke these functions)
    INSTALL_K(GetACP,                     Orig_GetACP);
    INSTALL_K(GetOEMCP,                   Orig_GetOEMCP);
    INSTALL_K(GetUserDefaultLCID,         Orig_GetUserDefaultLCID);
    INSTALL_K(GetSystemDefaultLCID,       Orig_GetSystemDefaultLCID);
    INSTALL_K(GetUserDefaultLangID,       Orig_GetUserDefaultLangID);
    INSTALL_K(GetSystemDefaultLangID,     Orig_GetSystemDefaultLangID);
    INSTALL_K(GetUserDefaultUILanguage,   Orig_GetUserDefaultUILanguage);
    INSTALL_K(GetSystemDefaultUILanguage, Orig_GetSystemDefaultUILanguage);
    INSTALL_K(GetLocaleInfoA,             Orig_GetLocaleInfoA);
    INSTALL_K(GetLocaleInfoW,             Orig_GetLocaleInfoW);

    // Conversion hooks — these are what actually move bytes and bypass GetACP()
    INSTALL_K(MultiByteToWideChar,  Orig_MultiByteToWideChar);
    INSTALL_K(WideCharToMultiByte,  Orig_WideCharToMultiByte);
    INSTALL_U(GetClipboardData,     Orig_GetClipboardData);
    INSTALL_U(SetClipboardData,     Orig_SetClipboardData);

    return true;
}

void RemoveLocaleHooks()
{
    std::lock_guard<std::mutex> lk(g_clip_mutex);
    if (g_fake_clip) { GlobalFree(g_fake_clip); g_fake_clip = nullptr; }
}
