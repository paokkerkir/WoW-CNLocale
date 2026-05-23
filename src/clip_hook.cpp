// clip_hook.cpp — hooks WoW 1.12's UnitXP so Lua can call
//   UnitXP("CNLocale", "copy", name)
// to write a player name to the Windows clipboard.
// Auto-detects UTF-8 vs GBK so private-server (UTF-8) and retail (GBK) clients
// both work correctly.

#include <windows.h>
#include <string>
#include "MinHook.h"
#include "clip_hook.h"

static const UINT CN_CP = 936;

// ---- WoW 1.12 Lua C API types and addresses --------------------------------

typedef int   (__fastcall* LUA_CFUNCTION)(void*);
typedef int   (__fastcall* FN_lua_gettop)(void*);
typedef const char* (__fastcall* FN_lua_tostring)(void*, int);
typedef void  (__fastcall* FN_lua_pushstring)(void*, const char*);

static auto p_lua_gettop    = reinterpret_cast<FN_lua_gettop>   (0x006F3070);
static auto p_lua_tostring  = reinterpret_cast<FN_lua_tostring> (0x006F3690);
static auto p_lua_pushstring= reinterpret_cast<FN_lua_pushstring>(0x006F3890);

static auto p_UnitXP        = reinterpret_cast<LUA_CFUNCTION>(0x517350);
static LUA_CFUNCTION p_orig_UnitXP = nullptr;

// ---- clipboard helper ------------------------------------------------------

// Returns true if the byte sequence is valid UTF-8 (including pure ASCII).
// GBK-encoded Chinese characters have lead bytes in 0x81-0xBF or trail bytes
// below 0x80 — both patterns fail this check, so GBK reliably returns false.
static bool IsValidUtf8(const char* s, int len)
{
    const unsigned char* p   = reinterpret_cast<const unsigned char*>(s);
    const unsigned char* end = p + len;
    while (p < end) {
        unsigned char c = *p++;
        int extra;
        if      (c < 0x80)           extra = 0;
        else if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return false;
        while (extra--) {
            if (p >= end || (*p & 0xC0) != 0x80) return false;
            ++p;
        }
    }
    return true;
}

static void WriteClipboard(const char* text, int len)
{
    // Private servers send names as UTF-8; retail/GBK clients use CP 936.
    // Detect which encoding to use based on byte validity.
    UINT cp = IsValidUtf8(text, len) ? CP_UTF8 : CN_CP;

    int wlen = MultiByteToWideChar(cp, 0, text, len, nullptr, 0);
    if (wlen <= 0) return;

    if (!OpenClipboard(NULL)) return;
    EmptyClipboard();

    HGLOBAL hUni = GlobalAlloc(GMEM_MOVEABLE, (wlen + 1) * sizeof(wchar_t));
    if (hUni) {
        wchar_t* wb = static_cast<wchar_t*>(GlobalLock(hUni));
        if (wb) {
            MultiByteToWideChar(cp, 0, text, len, wb, wlen);
            wb[wlen] = L'\0';
            GlobalUnlock(hUni);
            if (!SetClipboardData(CF_UNICODETEXT, hUni))
                GlobalFree(hUni);
        } else {
            GlobalFree(hUni);
        }
    }

    // CF_TEXT is intentionally omitted: Hook_GetClipboardData synthesises it
    // from CF_UNICODETEXT on demand.  Setting CF_TEXT here would re-enter
    // Hook_SetClipboardData, which would overwrite our correct CF_UNICODETEXT
    // with a GBK-decoded version — corrupting UTF-8 input.

    CloseClipboard();
}

// ---- UnitXP hook -----------------------------------------------------------

static int __fastcall Hook_UnitXP(void* L)
{
    if (!L) goto passthrough;

    if (p_lua_gettop(L) >= 2) {
        const char* arg1 = p_lua_tostring(L, 1);
        if (arg1 && std::string(arg1) == "CNLocale") {
            const char* subcmd = p_lua_tostring(L, 2);
            if (subcmd && std::string(subcmd) == "copy") {
                if (p_lua_gettop(L) >= 3) {
                    const char* text = p_lua_tostring(L, 3);
                    if (text && text[0]) {
                        WriteClipboard(text, static_cast<int>(strlen(text)));
                        p_lua_pushstring(L, "ok");
                    } else {
                        p_lua_pushstring(L, "error|empty text");
                    }
                } else {
                    p_lua_pushstring(L, "error|text required");
                }
                return 1;
            }
            p_lua_pushstring(L, "error|unknown subcommand");
            return 1;
        }
    }

passthrough:
    if (p_orig_UnitXP) return p_orig_UnitXP(L);
    return 0;
}

// ---- install / remove -------------------------------------------------------

bool InstallClipHook()
{
    if (MH_CreateHook(reinterpret_cast<LPVOID>(p_UnitXP),
                      reinterpret_cast<LPVOID>(Hook_UnitXP),
                      reinterpret_cast<LPVOID*>(&p_orig_UnitXP)) != MH_OK)
        return false;
    return true;
}

void RemoveClipHook()
{
    MH_RemoveHook(reinterpret_cast<LPVOID>(p_UnitXP));
    p_orig_UnitXP = nullptr;
}
