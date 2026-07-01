# Notices

This project is a modified distribution of `D7ry/dMenu`.

Upstream project:

- Repository: https://github.com/D7ry/dMenu
- License: MIT
- Original copyright: Copyright (c) 2021 d7ry

This distribution keeps the upstream MIT license and adds copyright for later
modifications:

- Copyright (c) 2026 C0kadam

Contributor credit:

- [Risasre22](https://github.com/Risasre22) - external API integration request,
  feedback, and compatibility validation support.

Unless a file states otherwise, source code, documentation, configuration files,
translations, and project-owned runtime assets in this repository are distributed
under the MIT License in `LICENSE`.

## Notable Modifications

This distribution includes changes beyond the original dMenu project, including:

- Gamepad and keyboard navigation improvements.
- IME and screen keyboard support.
- Hint media support and related runtime configuration/docs.
- External dMenu control API for SKSE plugins.
- Build, packaging, and documentation cleanup.

## Third-Party Source Notices

The repository includes NanoSVG source files under `src/include/lib/`.
Their original license notices are retained in those files and must not be
removed from source distributions:

- `src/include/lib/nanosvg.h`
- `src/include/lib/nanosvgrast.h`

Build dependencies are resolved through vcpkg and remain under their respective
upstream licenses.
