// CNLocale.dll - Chinese Simplified locale emulator for WoW 1.12
// Hooks NLS and registry APIs so the process sees zh-CN (LCID 0x0804, ACP 936).

#include <windows.h>
#include <fstream>
#include <mutex>

#include "MinHook.h"
#include "locale_hooks.h"
#include "registry_hooks.h"
#include "clip_hook.h"

static std::ofstream g_log;
static std::mutex    g_log_mutex;

static void Log(const char* msg)
{
    std::lock_guard<std::mutex> lk(g_log_mutex);
    if (g_log.is_open())
        g_log << msg << "\n";
}

static void OpenLog(HMODULE hMod)
{
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(hMod, path, MAX_PATH)) return;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wcscat_s(path, MAX_PATH, L"CNLocale.log");
    g_log.open(path, std::ios::app);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        OpenLog(hModule);
        Log("CNLocale: attach");

        if (MH_Initialize() != MH_OK) {
            Log("CNLocale: MH_Initialize failed");
            return FALSE;
        }
        if (!InstallLocaleHooks()) {
            Log("CNLocale: InstallLocaleHooks failed");
            MH_Uninitialize();
            return FALSE;
        }
        if (!InstallRegistryHooks()) {
            Log("CNLocale: InstallRegistryHooks failed");
            MH_Uninitialize();
            return FALSE;
        }
        // UnitXP hook is optional: if WoWTranslate.dll is already loaded it may
        // have patched UnitXP first.  Failing here must NOT abort the DLL —
        // the locale and clipboard conversion hooks are still essential.
        if (!InstallClipHook())
            Log("CNLocale: InstallClipHook failed (non-fatal; WoWTranslate may own UnitXP)");

        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
            Log("CNLocale: MH_EnableHook failed");
            MH_Uninitialize();
            return FALSE;
        }
        Log("CNLocale: hooks active — locale=zh-CN (0x0804), ACP=936");
        break;

    case DLL_PROCESS_DETACH:
        Log("CNLocale: detach");
        MH_DisableHook(MH_ALL_HOOKS);
        RemoveClipHook();
        RemoveLocaleHooks();
        RemoveRegistryHooks();
        MH_Uninitialize();
        {
            std::lock_guard<std::mutex> lk(g_log_mutex);
            if (g_log.is_open()) g_log.close();
        }
        break;
    }
    return TRUE;
}
