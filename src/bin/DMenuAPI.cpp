// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 C0kadam

#define DMENU_API __declspec(dllexport)

#include "DMenuAPI.h"

#include "Renderer.h"

extern "C" DMENU_API std::uint32_t dMenu_GetApiVersion()
{
	return dmenu_api::kInterfaceVersion;
}

extern "C" DMENU_API const dmenu_api::Interface* dMenu_GetInterface(std::uint32_t a_requestedVersion)
{
	static const dmenu_api::Interface api{
		dmenu_api::kInterfaceVersion,
		sizeof(dmenu_api::Interface),
		dMenu_SetMenuOpen,
		dMenu_OpenMenu,
		dMenu_CloseMenu,
		dMenu_ToggleMenu,
		dMenu_IsMenuOpen,
		dMenu_IsReady,
		dMenu_GetApiVersion,
		dMenu_GetInterface
	};

	if (a_requestedVersion != 0 && a_requestedVersion > dmenu_api::kInterfaceVersion) {
		return nullptr;
	}

	return &api;
}

extern "C" DMENU_API void dMenu_SetMenuOpen(bool a_open)
{
	Renderer::SetEnabled(a_open);
}

extern "C" DMENU_API void dMenu_OpenMenu()
{
	Renderer::Open();
}

extern "C" DMENU_API void dMenu_CloseMenu()
{
	Renderer::Close();
}

extern "C" DMENU_API void dMenu_ToggleMenu()
{
	Renderer::Toggle();
}

extern "C" DMENU_API bool dMenu_IsMenuOpen()
{
	return Renderer::IsEnabled();
}

extern "C" DMENU_API bool dMenu_IsReady()
{
	return Renderer::IsReady();
}

void DMenuAPI::DispatchInterface()
{
	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging) {
		return;
	}

	auto* api = const_cast<dmenu_api::Interface*>(dMenu_GetInterface(dmenu_api::kInterfaceVersion));
	if (!api) {
		return;
	}

	messaging->Dispatch(dmenu_api::kMessageInterface, api, api->structSize, nullptr);
}
