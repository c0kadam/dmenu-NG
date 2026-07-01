# dMenu NG

dMenu NG is an SKSE plugin that renders an ImGui based in-game configuration
menu for Skyrim Special Edition / Anniversary Edition.

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

## Runtime Data

The `Data/` directory contains runtime configuration, translations, docs, and
sample hint media. Install or package it alongside the built DLL so the final
layout contains:

```text
Data/SKSE/Plugins/dmenu.dll
Data/SKSE/Plugins/dMenu/...
```

Generated build directories, local CMake user presets, logs, packaged archives,
DLL/PDB outputs, and editor backups are intentionally excluded from source
control.

## External API

dMenu exposes a small versioned API for other SKSE plugins to open, close,
toggle, and query the menu without simulating the configured hotkey.

See [README_dmenu_api.md](README_dmenu_api.md) and
[`src/include/dmenu_api.h`](src/include/dmenu_api.h).

## License and Attribution

This project is derived from [D7ry/dMenu](https://github.com/D7ry/dMenu),
which is distributed under the MIT License.

This C0kadam-maintained distribution remains MIT licensed. See
[LICENSE](LICENSE) and [NOTICE.md](NOTICE.md) for upstream attribution,
modification notices, and third-party source notices.

External API integration feedback and validation support:

- [Risasre22](https://github.com/Risasre22)
