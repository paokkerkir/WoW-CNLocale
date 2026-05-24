# WoW-CNLocale

A small injected DLL that tricks **WoW 1.12** into thinking it's running on a Chinese Windows install. The practical result: Chinese text you paste in-game actually shows up instead of `????`, and player names from Chinese private servers copy/paste correctly.

No MPQ editing, no file patching — just runtime API hooks that live only while WoW is running.

Fully compatible with [wow-translate](https://github.com/paokkerkir/wow-translate/tree/main).


## Download

**[⬇️ Download Latest Release](../../releases/latest)**

## Announcement:

Since an unexpected amount of people were asking me for a way to donate, if you want to support the development of this passion project, either Paypal me at @paokkerkir or Revolut me at @belthazor.


## Install

Extract and copy to your WoW folder:

```
YourWoWFolder/
├── WoW.exe
├── CNLocale.dll        ← From the download
├── dlls.txt                ← Add "CNLocale.dll" to this file
└── Interface/
    └── AddOns/
        └── CNCopyName   ← From the download
```

> **Note:** If `dlls.txt` doesn't exist, create it and add `CNLocale.dll` on the first line.

> You have to run via `VanillaFixes.exe` or any other WoW dll launcher.

>There is a possibility that your AV flags the dll, if this happens you have to add the dll to exclusions. 

**Done!** You can now paste chinese text in WoW 1.12, and copy any chinese name with Ctrl+RightClick.

## Why this exists

WoW 1.12 reads text encoding from Windows locale APIs and the registry. On a Western install those say CP 1252 / en-US, so WoW treats every byte as Latin-1. Chinese text turns into garbage. This DLL hooks the relevant APIs at the process level so WoW sees zh-CN and CP 936 (GBK) without touching anything else on the system.

The trickier part is the clipboard. Windows synthesises `CF_TEXT` from `CF_UNICODETEXT` using the *system* ACP — which is still 1252 — before the locale hooks can do anything about it. There's a dedicated clipboard hook to intercept that conversion and redo it with CP 936.

## What gets hooked

| Hook | What it returns |
|------|----------------|
| `GetACP` / `GetOEMCP` | 936 |
| `GetUserDefaultLCID` / `GetSystemDefaultLCID` | 0x0804 (zh-CN) |
| `GetUserDefaultLangID` / `GetSystemDefaultLangID` | 0x0804 |
| `GetUserDefaultUILanguage` / `GetSystemDefaultUILanguage` | 0x0804 |
| `GetLocaleInfoA/W` | redirects default-locale queries to zh-CN |
| `MultiByteToWideChar` (CP_ACP) | uses CP 936 instead |
| `WideCharToMultiByte` (CP_ACP) | uses CP 936 instead |
| `GetClipboardData(CF_TEXT)` | re-encodes from Unicode as GBK before returning to WoW |
| `SetClipboardData(CF_TEXT)` | also pushes a correct `CF_UNICODETEXT` so the round-trip stays clean |
| `RegQueryValueEx` on NLS keys | returns faked Chinese values (ACP=936, language=0804, etc.) |
| `UnitXP("CNLocale", "copy", name)` | writes a name to the clipboard from Lua (used by CNCopyName addon) |

## Companion addon — CNCopyName

A separate Lua addon that lets you **Ctrl+RightClick** any player name to copy it to the clipboard. Works on:

- Chat frame hyperlinks (any tab)
- PlayerFrame / TargetFrame / Target-of-Target frame
- Guild roster (both the player-status and guild-status views)
- Friends list
- /who results

Uses the DLL's `UnitXP` hook to write the name. Falls back to WoWTranslate if that's loaded instead, or to the chat editbox if neither DLL is available.

Slash command: `/cncopy <text>` — useful for testing the DLL clipboard path directly.

## Building

```bat
build.bat
```

Output: `build\bin\Release\CNLocale.dll`

Manual CMake:

```bat
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A Win32
cmake --build . --config Release
```

Requires Visual Studio 2022 with the Desktop C++ workload and x86 build tools, plus CMake 3.20+.

## Third-party

[MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu — BSD 2-Clause. Header and pre-built x86 static lib are bundled in `third_party/`.

## License

MIT
