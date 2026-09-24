#include "PCH.h"

#include <atomic>
#include <cstdarg>
#include <filesystem>

#include "SkseMenuFrameworkIntegration.h"

#include "include/lib/SKSEMenuFramework.h"

namespace
{
	constexpr auto kFrameworkModuleName = L"SKSEMenuFramework";
	constexpr auto kAddSectionItemExportName = "AddSectionItem";
	constexpr auto kTextUnformattedExportName = "igTextUnformatted";

	void __stdcall DrawDMenuSettings()
	{
		ImGuiMCP::TextUnformatted("SKSE Menu Framework frontend test");
	}

	bool HasRequiredExports(HMODULE a_module)
	{
		return ::GetProcAddress(a_module, kAddSectionItemExportName) != nullptr &&
		       ::GetProcAddress(a_module, kTextUnformattedExportName) != nullptr;
	}
}

namespace SkseMenuFrameworkIntegration
{
	void InitializeAfterPluginsLoaded()
	{
		static bool initialized = false;
		if (initialized) {
			return;
		}

		auto module = ::GetModuleHandleW(kFrameworkModuleName);
		if (!module || !HasRequiredExports(module)) {
			return;
		}

		SKSEMenuFramework::SetSection("dMenu");
		SKSEMenuFramework::AddSectionItem("dMenu Settings", DrawDMenuSettings);
		initialized = true;
		logger::info("SKSE Menu Framework frontend registered");
	}
}
