# Experimental Runtime Discovery

This branch is for remote compatibility testing, particularly GOG Skyrim
1.6.1179. It does **not** declare official GOG or general future-runtime support.
No GOG offsets have been added to the verified runtime table.

## Resolution and Safety

The exact audited offsets for 1.5.97.0, 1.6.1170.0, 1.7.99.0 and 1.7.104.0
remain authoritative. These runtimes do not use discovery.

Unlisted Skyrim 1.x AE runtimes at or above 1.6.629 can attempt discovery using
their matching Address Library. Unknown SE/VR and pre-629 layouts are rejected:
the current discovery definitions do not provide sufficiently verified target
IDs and contexts for them.

For each of the four required hooks:

1. Find the relocation and expected target IDs in CommonLib's Address Library
   enumeration. A missing mapping fails preflight.
2. Require the relocation to coincide with the start of a Windows x64 unwind
   function entry in Skyrim's executable `.text` section.
3. Require the **entire function** to fit within `kMaxDiscoveryFunctionBytes`
   (4 KiB). Missing unwind metadata, unreadable/non-executable memory or a larger
   function fails closed; there is no fallback to scanning neighboring code.
4. Walk instruction boundaries using the HDE64 decoder already pinned by
   CommonLib's patch-safety dependency. Reuse the callsite context rules and
   existing target/chain validation. Decoder errors or truncated instructions
   fail the whole search, even after a plausible match.
5. Accept exactly one fully validated candidate. Zero or multiple candidates
   fail. Duplicate callsites or immediate CALL targets across hooks also fail.

The function limit bounds work and prevents scanning beyond one function. It is
not a guessed offset or a radius around a known-runtime offset. Large functions,
unwind fragments, embedded data and instructions unsupported by HDE64 can cause
conservative rejection. Do not enlarge the bound simply to obtain a match.

All four results are held privately until the complete preflight succeeds.
No renderer or gameplay hook is installed on discovery failure. The normal
lifecycle then runs: renderer/weather hooks at load and the input hook once at
`kDataLoaded`. Deferred input installation revalidates its pinned callsite and
current downstream target; it never searches for a new location after other
hooks are installed. A later conflicting plugin patch can still cause deferred
input validation to fail; already installed renderer/weather hooks stay resident.

## Loader Metadata

The pinned CommonLibSSE-NG 7.2.0 revision remains
`7a60f4de794095d7b0f8928d1b930a52e9a7da83`. Its `SKSE::PluginVersionData` uses
`UsesAddressLibrary()` and `UsesUpdatedStructs()`; this branch also retains its
`kVersionIndependentEx_AddressLibraryV5` encoding flag. The older boolean setters
are not this revision's interface.

These flags allow the loader to reach preflight when the appropriate Address
Library exists. Neither `UsesNoStructs()` nor `UsesSigScanning()` is asserted:
dMenu uses game structures and depends on Address Library IDs. The verified
version list remains in the metadata, and the legacy SE query still admits
1.5.97. See the [SKSE metadata definitions](https://github.com/ianpatt/skse64/blob/master/skse64/PluginAPI.h)
and [GOG-era SKSE loader checks](https://github.com/ianpatt/skse64/blob/v2.2.6/skse64/PluginManager.cpp).

Startup logs include all four SKSE version components, including the packed
sub/storefront component, plus the separate REL executable version used by
CommonLib. These can differ; neither is reduced to three components in logs.

## Remote Test

Build with the normal pinned toolchain and `COPY_OUTPUT=OFF`, for example:

```powershell
cmake --preset vs2022-windows -B build-runtime-discovery -DCOPY_OUTPUT=OFF -DDMENU_BUILD_TESTS=ON
cmake --build build-runtime-discovery --config Release
ctest --test-dir build-runtime-discovery -C Release --output-on-failure
```

The DLL is `build-runtime-discovery/src/Release/dmenu.dll`. Keep the normal
dMenu data files and matching game/SKSE/Address Library installation. Back up the
installed DLL before replacing it for a test. This branch does not auto-deploy.

The startup banner must say `EXPERIMENTAL RUNTIME DISCOVERY`. Send the complete
`dmenu.log` and `skse64.log` from the same launch, along with the game storefront,
SKSE version and a list of other overlay/input plugins. No executable is needed.
Logs include relocation bases, search ranges, every decoded CALL candidate,
context/target rejection reasons, accepted relative offsets and the final
all-hooks preflight result. Missing Address Library files may be rejected by
SKSE before this DLL loads; malformed libraries can also be fatal inside the
pinned CommonLib loader before dMenu's preflight. Those cases need the SKSE log.

After successful preflight, test opening/closing with keyboard and gamepad,
modifier bindings, overlays, public API controls and ordinary menu interactions.
Test with a disposable save. Report failures as well as successful startup.

## Limits

Callsite validation cannot prove every game structure, virtual function or
unrelated relocation used elsewhere by dMenu is compatible with an unlisted
runtime. The implementation assumes the layouts supplied by the pinned
CommonLib remain applicable. Successful discovery is evidence about these four
hooks, not certification of the entire plugin on that runtime.

Pre-existing chains are checked under the existing executable-chain policy;
executable memory cannot prove another plugin's function signature or behavior.
The optional SimpleIME bridge keeps its existing verified-runtime guard and is
not enabled merely because the four primary hooks were discovered.

The public dMenu API, plugin identity, messaging sender, configuration paths,
renderer initialization, cooperative wheel integration and FLICK/SKSEMF paths
are unchanged. No game runtime has been tested by the unit tests.
