<p align="center">
  <img src="https://staticdelivery.nexusmods.com/mods/1704/images/166751/166751-1766729765-1623383754.png" alt="dMenu NG" width="220">
</p>

# dMenu NG
dMenu NG is an unofficial standalone DLL update for
[dMenu](https://github.com/D7ry/dMenu). It keeps the
original mod's asset setup, but modernises the plugin for current Skyrim
runtimes, improves controller and text-input workflows, and expands what custom
dMenu pages can do.

This GitHub repo is mainly here for source, API documentation, and reference
builds. For normal users, the Nexus page is still the main release page:

https://www.nexusmods.com/skyrimspecialedition/mods/166751

## Highlights

- CommonLibSSE-NG port for modern SE/AE runtimes.
- Strict startup callsite validation that preserves compatible pre-existing
  SKSE hook chains.
- Crash and safety fixes around AIM spawning, settings handling, and newer game
  versions.
- Gamepad navigation, separate gamepad toggle/modifier bindings, and a gamepad
  hint toggle.
- Native IME input for Chinese, Japanese, and Korean text fields.
- Built-in on-screen keyboard for controller-driven text entry.
- Localisation through `Data/SKSE/Plugins/dMenu/translations.txt`.
- Better glyph coverage and custom font support.
- Grid layout support for custom mod settings pages.
- Optional FLICK and SKSE Menu Framework settings frontends.
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
Release archives include the compiled plugin; the source repository does not
track `dmenu.dll`. The installed layout should include:

```text
Data/SKSE/Plugins/dmenu.dll
Data/SKSE/Plugins/dMenu/hint_media.ini
Data/SKSE/Plugins/dMenu/translations.txt
Data/SKSE/Plugins/dMenu/customSettings/...
Data/SKSE/Plugins/dMenu/docs/...
Data/SKSE/Plugins/dMenu/hints/...
```

Keep dMenu NG below the original dMenu mod in your mod manager so this DLL takes
priority. The dMenu NG archive contains its DLL and the NG runtime additions
listed above; it does not redistribute the original mod's base assets.

## Optional Settings Frontends

dMenu NG can optionally expose its custom settings pages through
[FLICK](https://github.com/Fuzzlesz/FUCK) and
[SKSE Menu Framework 3](https://github.com/QTR-Modding/SKSE-Menu-Framework-3),
using their public APIs. The SKSE Menu Framework public API is available
[here](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API).

Both integrations are optional. Neither framework is bundled with dMenu NG or
hard-linked into `dmenu.dll`, and native dMenu remains usable without either.
These frontends use dMenu's existing settings model and do not create separate
settings stores.

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
dMenu_SetMenuOpen(bool open)
dMenu_GetApiVersion()
dMenu_GetInterface(uint32_t requestedVersion)
```

See [README_dmenu_api.md](README_dmenu_api.md) for the SKSE messaging and export
lookup examples.

## Requirements

- Visual Studio 2022 with the C++ desktop workload
- CMake 3.22 or newer
- vcpkg
- [CommonLibSSE-NG 7.2.0](https://github.com/alandtse/CommonLibSSE-NG/releases/tag/v7.2.0),
  checked out at `7a60f4de794095d7b0f8928d1b930a52e9a7da83`
- Address Library for SKSE Plugins with the database matching the installed
  Skyrim executable (1.7.99 and 1.7.104 use Address Library format 5)

| Skyrim runtime | Matching SKSE |
| --- | --- |
| 1.5.97 | 2.0.20 |
| 1.6.1170 | 2.2.6 |
| 1.7.99 | 2.3.0 |
| 1.7.104 | 2.3.1 |

Set these environment variables before configuring:

```powershell
$env:VCPKG_ROOT = "C:\Path\To\vcpkg"
$env:CommonLibSSEPath_NG = "C:\Path\To\CommonLibSSE-NG"
```

The configure step checks both the CommonLibSSE-NG version and exact Git
revision. Supported Skyrim runtimes are 1.5.97, 1.6.1170, 1.7.99, and 1.7.104;
other versions fail closed before any hooks are installed.

## Build

Configure without copying the output anywhere:

```powershell
cmake --preset vs2022-windows -DCOPY_OUTPUT=OFF
```

Build the plugin:

```powershell
cmake --build build --config Release
```

To build and run the runtime compatibility regression tests, configure with
`-DDMENU_BUILD_TESTS=ON`, then run:

```powershell
cmake --build build --config Release --target dmenu_runtime_compatibility_tests
ctest --test-dir build -C Release --output-on-failure
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

## License

dMenu NG as a combined project is distributed under `GPL-3.0-or-later` with the
CommonLibSSE-NG Modding Exception and GPL-3.0 Linking Exception (with
Corresponding Source).
This licensing applies because the plugin statically links CommonLibSSE-NG
7.2.0. See [LICENSE](LICENSE), [EXCEPTIONS.md](EXCEPTIONS.md), and
[NOTICE.md](NOTICE.md) for the complete terms and attribution.

[LICENSES/](LICENSES/README.md) preserves license texts for specifically
identified upstream, API, and dependency components. The component-specific
MIT, LGPL, and GPL terms apply to those identified components. Their presence
does not mean the complete dMenu NG project is triple-licensed or offered
under every license in that directory.

The original D7ry/dMenu source was released under MIT. Its original copyright
and license text are preserved in
[LICENSES/D7ry-dMenu-MIT.txt](LICENSES/D7ry-dMenu-MIT.txt). CommonLibSSE-NG
license provenance is preserved under [LICENSES](LICENSES/).

The public interoperability header `src/include/dmenu_api.h` is separately
available under MIT so other SKSE plugins can include the ABI declaration. See
[LICENSES/dMenu-API-MIT.txt](LICENSES/dMenu-API-MIT.txt). The API implementation
compiled into `dmenu.dll` remains part of the GPL-licensed project.

Binary distributors must provide the Corresponding Source required by these
terms. The reproducible dependency identity used by this project is
CommonLibSSE-NG 7.2.0 at commit
`7a60f4de794095d7b0f8928d1b930a52e9a7da83`.

Each binary release should therefore be accompanied by a source archive that
contains the exact dMenu NG release source and build files, the exact pinned
CommonLibSSE-NG source, and the source/build material for the non-System static
and header-only dependencies incorporated into the DLL. Repository links,
version numbers, and commit hashes are useful provenance and reproducibility
metadata, but are not substitutes for the source archive.

## Credits / Permissions

dMenu NG is a modified derivative of the original dMenu mod by dTry/D7ry.

- dMenu by dTry/D7ry
- GitHub: https://github.com/D7ry/dMenu
- Original source licensed under the MIT License

This project is not affiliated with or endorsed by dTry/D7ry. All original
credits remain with the original author and contributors.

Thanks to dTry for creating the original mod and open-sourcing the code, and to
the authors and maintainers of CommonLibSSE-NG, Dear ImGui, SimpleIni, spdlog,
xbyak, nlohmann/json, rapidcsv, rsm-binary-io, fast-cpp-csv-parser, and the
other libraries used by the project.

dMenu NG optionally integrates with these projects through their public APIs.

- [FLICK](https://github.com/Fuzzlesz/FUCK) by Fuzzlesz.
- [SKSE Menu Framework 3](https://github.com/QTR-Modding/SKSE-Menu-Framework-3)
  by QTR-Modding, with its
  [public API](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API).

SimpleIME compatibility was validated with
[SimpleIME 2.2.1](https://github.com/cyfewlp/SimpleIME) by cyfewlp [JamieYin101](https://www.nexusmods.com/profile/JamieYin101). SimpleIME
is installed separately and is not bundled with or linked into dMenu NG.

Thanks to [Risasre22](https://github.com/Risasre22) [risarei](https://www.nexusmods.com/profile/risarei) for the external API
integration request and compatibility feedback.

See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md) for license and attribution
details.
