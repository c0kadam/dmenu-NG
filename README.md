# dMenu NG

dMenu NG is an unofficial standalone DLL update for
[dMenu](https://www.nexusmods.com/skyrimspecialedition/mods/85707). It keeps the
original mod's asset setup, but modernises the plugin for current Skyrim
runtimes, improves controller and text-input workflows, and expands what custom
dMenu pages can do.

This GitHub repo is mainly here for source, API documentation, and reference
builds. For normal users, the Nexus page is still the main release page:

https://www.nexusmods.com/skyrimspecialedition/mods/166751

## Highlights

- CommonLibSSE-NG port for modern SE/AE runtimes.
- Crash and safety fixes around AIM spawning, settings handling, and newer game
  versions.
- Gamepad navigation, separate gamepad toggle/modifier bindings, and a gamepad
  hint toggle.
- Native IME input for Chinese, Japanese, and Korean text fields.
- Built-in on-screen keyboard for controller-driven text entry.
- Localisation through `Data/SKSE/Plugins/dMenu/translations.txt`.
- Better glyph coverage and custom font support.
- Grid layout support for custom mod settings pages.
- Safer INI saving that edits existing files instead of rebuilding them from
  scratch.
- Animated hint media for custom settings: flipbook, GIF, WebP, and WebM
  previews.
- External API for other SKSE plugins that need to open or close dMenu cleanly.

## Important Note

This is a standalone DLL update. It replaces the plugin file, but the original
dMenu mod is still required for its assets and base content.

For localisation support, make sure the included `translations.txt` file is
present in:

```text
Data/SKSE/Plugins/dMenu/translations.txt
```

## Installation Layout

Install the original dMenu first, then let dMenu NG overwrite the plugin files.
The final layout should include:

```text
Data/SKSE/Plugins/dmenu.dll
Data/SKSE/Plugins/dMenu/hint_media.ini
Data/SKSE/Plugins/dMenu/translations.txt
Data/SKSE/Plugins/dMenu/customSettings/...
Data/SKSE/Plugins/dMenu/docs/...
Data/SKSE/Plugins/dMenu/hints/...
```

Keep dMenu NG below the original dMenu mod in your mod manager so this DLL takes
priority.

## External API For Mod Authors

dMenu NG includes a small versioned API for SKSE plugins that need to open,
close, toggle, or check dMenu without simulating the configured hotkey.

Public header:

```text
src/include/dmenu_api.h
```

Main exported functions:

```text
dMenu_OpenMenu()
dMenu_CloseMenu()
dMenu_ToggleMenu()
dMenu_IsMenuOpen()
dMenu_IsReady()
dMenu_GetInterface(uint32_t requestedVersion)
```

See [README_dmenu_api.md](README_dmenu_api.md) for the SKSE messaging and export
lookup examples.

## Requirements

- Visual Studio 2022 with the C++ desktop workload
- CMake 3.22 or newer
- vcpkg
- CommonLibSSE-NG
- Skyrim Script Extender (SKSE)

Set these environment variables before configuring:

```powershell
$env:VCPKG_ROOT = "C:\dev\vcpkg"
$env:CommonLibSSEPath_NG = "C:\dev\CommonLibSSE-NG"
```

## Build

Configure without copying the output anywhere:

```powershell
cmake --preset vs2022-windows -DCOPY_OUTPUT=OFF
```

Build the plugin:

```powershell
cmake --build build --config Release
```

The DLL is written to:

```text
build/src/Release/dmenu.dll
```

To copy the build output into a mod staging folder automatically, set
`CompiledPluginsPath` to the mod root and configure with `COPY_OUTPUT=ON`:

```powershell
$env:CompiledPluginsPath = "C:\Path\To\Your\Mod"
cmake --preset vs2022-windows -DCOPY_OUTPUT=ON
```

## Credits / Permissions

dMenu NG is a modified derivative of the original dMenu mod by dTry/D7ry.

- dMenu by dTry/D7ry
- GitHub: https://github.com/D7ry/dMenu
- Licensed under the MIT License

This project is not affiliated with or endorsed by dTry/D7ry. All original
credits remain with the original author and contributors.

Thanks to dTry for creating the original mod and open-sourcing the code, and to
the authors and maintainers of CommonLibSSE-NG, Dear ImGui, SimpleIni, spdlog,
xbyak, nlohmann/json, rapidcsv, rsm-binary-io, fast-cpp-csv-parser, and the
other libraries used by the project.

Thanks to [Risasre22](https://github.com/Risasre22) for the external API
integration request and compatibility feedback.

See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md) for license and attribution
details.
