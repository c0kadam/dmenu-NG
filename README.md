# dMenu NG

dMenu NG is a C0kadam-maintained distribution of
[D7ry/dMenu](https://github.com/D7ry/dMenu), an SKSE plugin that provides an
ImGui-based in-game configuration menu for Skyrim Special Edition and
Anniversary Edition.

The goal of this fork is to keep the original dMenu idea intact while making it
more comfortable to use in modern modlists: better controller support, cleaner
text input, richer setting hints, and a small external API for other SKSE
plugins that need to open or close dMenu without simulating a hotkey.

This is not a rewrite from scratch. The project still follows the original
dMenu structure: a native SKSE/CommonLibSSE-NG plugin, an ImGui renderer,
JSON-driven custom setting pages, and runtime files under
`Data/SKSE/Plugins/dMenu`. The changes are focused on usability,
compatibility, and packaging.

## What Changed From Original dMenu

- Gamepad and keyboard navigation were expanded so dMenu can be used more
  comfortably without a mouse.
- Keyboard/mouse and gamepad toggle bindings are split, while the legacy
  `key_toggle_dmenu` setting is still written for compatibility.
- A controller-friendly on-screen keyboard was added for text and numeric
  input.
- IME support was added for East Asian text input, including composition and
  candidate handling.
- Hint media support was added for richer setting descriptions, including
  flipbook BMP frames, GIF/WebP-style assets, and WebM previews.
- A local translation file is supported at
  `Data/SKSE/Plugins/dMenu/translations.txt`, with SKSE translation fallback.
- A versioned external dMenu control API was added for SKSE plugins.
- Build and packaging files were cleaned up so the repository can be shared and
  built without local machine state.
- A ready-built `dmenu.dll` is included in the `Data/` tree for convenience.

## Runtime Layout

The repository includes the runtime files that are meant to ship with the
plugin:

```text
Data/SKSE/Plugins/dmenu.dll
Data/SKSE/Plugins/dMenu/hint_media.ini
Data/SKSE/Plugins/dMenu/translations.txt
Data/SKSE/Plugins/dMenu/customSettings/...
Data/SKSE/Plugins/dMenu/docs/...
Data/SKSE/Plugins/dMenu/hints/...
```

Install or package the `Data/` directory as-is. The source tree does not include
local build directories, user CMake presets, logs, PDBs, archives, or editor
backup files.

## External API

dMenu NG exposes a small versioned API for other SKSE plugins that need to
open, close, toggle, or query dMenu without simulating the configured hotkey.

The public ABI header is:

```text
src/include/dmenu_api.h
```

Available exported functions include:

```text
dMenu_OpenMenu()
dMenu_CloseMenu()
dMenu_ToggleMenu()
dMenu_IsMenuOpen()
dMenu_IsReady()
dMenu_GetApiVersion()
dMenu_GetInterface(uint32_t requestedVersion)
```

The preferred integration path is SKSE messaging: listen for sender `dmenu` and
message type `dmenu_api::kMessageInterface`. Direct DLL export lookup is also
supported as a fallback.

See [README_dmenu_api.md](README_dmenu_api.md) for integration examples.

## Hint Media

Custom setting entries can show richer help than plain text. Hint media is
configured through JSON entries and runtime settings in:

```text
Data/SKSE/Plugins/dMenu/hint_media.ini
```

The included examples show the intended layout for flipbook frames and WebM
previews. Media loading is cached and budgeted so the feature can stay practical
inside larger modlists.

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

## Repository Notes

The repository keeps the upstream project as `upstream` and uses this
distribution as its own `origin`. Generated build output is intentionally not
tracked, except for the packaged runtime DLL under `Data/SKSE/Plugins`.

The bundled runtime assets are small examples and support files for the shipped
configuration pages. Larger mod-specific media packs should generally live in
their own distribution packages instead of being committed here.

## License and Attribution

This project is derived from [D7ry/dMenu](https://github.com/D7ry/dMenu),
which is distributed under the MIT License.

This C0kadam-maintained distribution remains MIT licensed. See
[LICENSE](LICENSE) and [NOTICE.md](NOTICE.md) for upstream attribution,
modification notices, and third-party source notices.

External API integration feedback and validation support:

- [Risasre22](https://github.com/Risasre22)
