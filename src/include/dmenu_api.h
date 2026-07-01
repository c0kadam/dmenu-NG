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
	inline constexpr std::uint32_t kInterfaceVersion = 1;

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
}
