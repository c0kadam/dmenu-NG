// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 C0kadam

#pragma once

#include <cstdint>

#ifndef DMENU_API
#	define DMENU_API
#endif

namespace dmenu_api
{
	inline constexpr std::uint32_t kInterfaceVersion = 2;

	// dMenu broadcasts this SKSE message at kPostLoad.
	// Register a listener for sender "dmenu" and cast message->data to Interface.
	inline constexpr std::uint32_t kMessageInterface = 0x444D4946u;  // "DMIF"

	struct Interface;
	using GetApiVersionFn = std::uint32_t (*)();
	using GetInterfaceFn = const Interface* (*)(std::uint32_t);
	using SetMenuOpenFn = void (*)(bool);
	using VoidMenuFn = void (*)();
	using IsMenuOpenFn = bool (*)();
	using IsReadyFn = bool (*)();
	// v2
	using GetToggleKeyFn = std::uint32_t (*)();
	using SetToggleKeyFn = void (*)(std::uint32_t);

	struct Interface
	{
		std::uint32_t interfaceVersion;
		std::uint32_t structSize;

		SetMenuOpenFn SetMenuOpen;
		VoidMenuFn OpenMenu;
		VoidMenuFn CloseMenu;
		VoidMenuFn ToggleMenu;
		IsMenuOpenFn IsMenuOpen;
		IsReadyFn IsReady;
		GetApiVersionFn GetApiVersion;
		GetInterfaceFn GetInterface;

		// --- v2 additions (keyboard/mouse toggle-key control, applied live) ---
		// GetToggleKeyMkb: current keyboard/mouse toggle scan code (0 = no key bound).
		// SetToggleKeyMkb: set it at runtime; pass 0 to disable dMenu's own keyboard toggle
		//   entirely (e.g. to free the key for other use). Takes effect immediately, no restart,
		//   and is NOT persisted to dmenu.ini — re-apply each session. New members are appended,
		//   so v1 consumers keep working; check interfaceVersion >= 2 (or structSize) before use.
		GetToggleKeyFn GetToggleKeyMkb;
		SetToggleKeyFn SetToggleKeyMkb;
	};
}

extern "C"
{
	DMENU_API std::uint32_t dMenu_GetApiVersion();
	DMENU_API const dmenu_api::Interface* dMenu_GetInterface(std::uint32_t a_requestedVersion);
	DMENU_API void dMenu_SetMenuOpen(bool a_open);
	DMENU_API void dMenu_OpenMenu();
	DMENU_API void dMenu_CloseMenu();
	DMENU_API void dMenu_ToggleMenu();
	DMENU_API bool dMenu_IsMenuOpen();
	DMENU_API bool dMenu_IsReady();
	// v2
	DMENU_API std::uint32_t dMenu_GetToggleKeyMkb();
	DMENU_API void dMenu_SetToggleKeyMkb(std::uint32_t a_key);
}
