# Notices

## Project Licensing

dMenu NG is a modified derivative of
[D7ry/dMenu](https://github.com/D7ry/dMenu). The current combined work is
distributed under GNU GPL version 3 or later with the CommonLibSSE-NG Modding
Exception and GPL-3.0 Linking Exception (with Corresponding Source). The full
terms are in `LICENSE` and `EXCEPTIONS.md`.

Project source: https://github.com/c0kadam/dmenu-NG

Copyright notices for project-owned modifications include:

- Copyright (c) 2026 C0kadam

Contributor credit:

- [Risasre22](https://github.com/Risasre22) - external API integration request,
  feedback, and compatibility validation support.

## Original dMenu Source

The upstream dMenu source was distributed under the MIT License:

- Repository: https://github.com/D7ry/dMenu
- Original copyright: Copyright (c) 2021 d7ry
- Preserved license: `LICENSES/D7ry-dMenu-MIT.txt`

The upstream copyright and MIT permission notice remain applicable to the
upstream-derived portions. Nothing in this repository claims ownership of the
original dMenu code.

The public interoperability header `src/include/dmenu_api.h` is separately
available under the MIT License to support external plugin integration. Its
license is preserved in `LICENSES/dMenu-API-MIT.txt`. The DLL-side API
implementation is part of the GPL-licensed combined work described above.

## SimpleIME Compatibility

dMenu NG supports interoperability with
[SimpleIME](https://github.com/cyfewlp/SimpleIME) by cyfewlp. Compatibility was
validated with SimpleIME 2.2.1, which is distributed under the MIT License. A
copy of its license is preserved in `LICENSES/SimpleIME-MIT.txt`.

SimpleIME is not bundled with dMenu NG and is not linked into `dmenu.dll`. It
must be installed separately by users who want to use this compatibility path.

## CommonLibSSE-NG

dMenu NG statically links CommonLibSSE-NG using this pinned source revision:

- Repository: https://github.com/alandtse/CommonLibSSE-NG
- Version: 7.2.0
- Commit: `7a60f4de794095d7b0f8928d1b930a52e9a7da83`
- License: GPL-3.0-or-later with the Modding Exception and GPL-3.0 Linking
  Exception (with Corresponding Source)

Exact copies of the pinned CommonLibSSE-NG license texts are stored in:

- `LICENSES/CommonLibSSE-NG-GPL-3.0.txt`
- `LICENSES/CommonLibSSE-NG-EXCEPTIONS.md`

The build requires `CommonLibSSEPath_NG` to point to that exact revision and
rejects any other revision. Distributors of compiled binaries remain
responsible for providing the Corresponding Source required by the license and
exceptions. For dMenu NG binary releases, that means providing the exact dMenu
NG source and build scripts, the exact CommonLibSSE-NG source used, and the
source and build material needed for the non-System libraries incorporated
into the DLL. License notices, repository links, and commit identifiers record
provenance but do not by themselves supply that source.

## Third-Party Components

NanoSVG and NanoSVG rasterizer source are bundled under `src/include/lib/`.
Their original zlib-style copyright and license notices are embedded in
`nanosvg.h` and `nanosvgrast.h` and must remain intact.

The build also resolves the following dependencies through vcpkg. They remain
under their respective upstream licenses:

| Dependency | License |
| --- | --- |
| Dear ImGui | MIT |
| SimpleIni | MIT |
| spdlog | MIT |
| fmt | MIT |
| nlohmann/json | MIT |
| rsm-binary-io | MIT |
| DirectXMath | MIT |
| DirectXTK | MIT |
| Microsoft Detours | MIT |
| xbyak | BSD-3-Clause |
| rapidcsv | BSD-3-Clause |
| fast-cpp-csv-parser | BSD-3-Clause |
| libwebp | BSD-3-Clause |

Additional build dependencies and tools remain under their own upstream
licenses. Exact vcpkg copyright texts for the direct dependencies listed above
are preserved under `LICENSES/third-party/` and are included by the CPack
configuration. Binary release packages should retain all notices required by
the exact dependency versions used for that build.
