# dMenu External Control API

dMenu exposes a small versioned API for SKSE plugins that need to open, close,
toggle, or query the menu without simulating the configured hotkey.

The public ABI definition is in `src/include/dmenu_api.h`.

## License

The public interoperability header is separately available under the MIT
License; see `LICENSES/dMenu-API-MIT.txt`. The implementation compiled into
`dmenu.dll` remains covered by the main dMenu NG project license.

## SKSE messaging path

Register a listener for sender `dmenu` during your plugin load. dMenu broadcasts
`dmenu_api::kMessageInterface` at SKSE `kPostLoad`.

```cpp
#include "dmenu_api.h"

namespace
{
	dmenu_api::Interface g_dmenu{};

	void DMenuMessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg ||
		    a_msg->type != dmenu_api::kMessageInterface ||
		    !a_msg->data ||
		    a_msg->dataLen < sizeof(dmenu_api::Interface)) {
			return;
		}

		const auto* api = static_cast<const dmenu_api::Interface*>(a_msg->data);
		if (api->interfaceVersion < 1 || api->structSize < sizeof(dmenu_api::Interface)) {
			return;
		}

		g_dmenu = *api;
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	auto* messaging = SKSE::GetMessagingInterface();
	if (messaging) {
		messaging->RegisterListener("dmenu", DMenuMessageHandler);
	}

	return true;
}
```

Use the callbacks only after `g_dmenu.interfaceVersion` is non-zero:

```cpp
if (g_dmenu.interfaceVersion != 0 && g_dmenu.OpenMenu) {
	g_dmenu.OpenMenu();
}

if (g_dmenu.interfaceVersion != 0 && g_dmenu.IsMenuOpen && g_dmenu.CloseMenu) {
	if (g_dmenu.IsMenuOpen()) {
		g_dmenu.CloseMenu();
	}
}
```

## Export lookup fallback

If you prefer direct DLL exports, look up `dMenu_GetInterface` from `dmenu.dll`:

```cpp
using GetInterfaceFn = const dmenu_api::Interface* (*)(std::uint32_t);

auto* module = GetModuleHandleW(L"dmenu.dll");
auto* getInterface = module ?
	reinterpret_cast<GetInterfaceFn>(GetProcAddress(module, "dMenu_GetInterface")) :
	nullptr;

const dmenu_api::Interface* api = getInterface ?
	getInterface(dmenu_api::kInterfaceVersion) :
	nullptr;
```

Exported functions are also available individually:

- `dMenu_SetMenuOpen(bool open)`
- `dMenu_OpenMenu()`
- `dMenu_CloseMenu()`
- `dMenu_ToggleMenu()`
- `dMenu_IsMenuOpen()`
- `dMenu_IsReady()`
- `dMenu_GetApiVersion()`
- `dMenu_GetInterface(uint32_t requestedVersion)`

`OpenMenu` and `CloseMenu` are idempotent. Existing dMenu hotkey behavior is
unchanged and uses the same internal state as this API.
