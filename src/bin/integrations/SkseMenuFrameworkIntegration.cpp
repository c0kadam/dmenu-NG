#include "PCH.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <utility>

#include "SkseMenuFrameworkIntegration.h"

#include "include/lib/SKSEMenuFramework.h"

#include "../Utils.h"
#include "../menus/ModSettings.h"

namespace
{
	constexpr auto kFrameworkModuleName = L"SKSEMenuFramework";
	constexpr std::array kRequiredExportNames{
		"AddSectionItem",
		"igCheckbox",
		"igSliderInt",
		"igCollapsingHeader_TreeNodeFlags",
		"igTextUnformatted",
		"igSameLine",
		"igBeginDisabled",
		"igEndDisabled",
		"igIsItemHovered",
		"igBeginTooltip",
		"igEndTooltip",
		"igIsItemDeactivatedAfterEdit",
		"igPushID_Ptr",
		"igPopID"
	};

	constexpr std::size_t kMaximumRegisteredPages = 128;
	using RenderFunction = SKSEMenuFramework::Model::RenderFunction;
	std::array<const void*, kMaximumRegisteredPages> g_pageIdentities = {};

	const char* TextOrEmpty(const char* a_text)
	{
		return a_text ? a_text : "";
	}

	void DrawDescription(const char* a_description)
	{
		if (!a_description || a_description[0] == '\0') {
			return;
		}

		ImGuiMCP::SameLine();
		ImGuiMCP::TextUnformatted("(?)");
		if (ImGuiMCP::IsItemHovered() && ImGuiMCP::BeginTooltip()) {
			ImGuiMCP::TextUnformatted(a_description);
			ImGuiMCP::EndTooltip();
		}
	}

	bool BeginGroup(const ModSettings::GroupVisit& a_group)
	{
		ImGuiMCP::PushID(a_group.identity);
		if (!a_group.enabled) {
			ImGuiMCP::BeginDisabled();
		}

		const bool isOpen = ImGuiMCP::CollapsingHeader(TextOrEmpty(a_group.label));
		if (!a_group.enabled) {
			ImGuiMCP::EndDisabled();
		}
		DrawDescription(a_group.description);
		return isOpen;
	}

	void EndGroup(const ModSettings::GroupVisit&)
	{
		ImGuiMCP::PopID();
	}

	std::optional<bool> DrawCheckbox(const ModSettings::CheckboxVisit& a_checkbox)
	{
		bool value = a_checkbox.value;
		ImGuiMCP::PushID(a_checkbox.identity);
		if (!a_checkbox.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		const bool changed = ImGuiMCP::Checkbox(TextOrEmpty(a_checkbox.label), &value);
		if (!a_checkbox.enabled) {
			ImGuiMCP::EndDisabled();
		}
		DrawDescription(a_checkbox.description);
		ImGuiMCP::PopID();
		return changed ? std::optional<bool>{ value } : std::nullopt;
	}

	ModSettings::SliderUpdate DrawSlider(const ModSettings::SliderVisit& a_slider)
	{
		const int stepCount = Utils::SliderStepIndex(a_slider.max, a_slider.min, a_slider.step);
		const int initialStepIndex = Utils::SliderStepIndex(a_slider.value, a_slider.min, a_slider.step);
		int stepIndex = initialStepIndex;
		char displayValue[64] = {};
		std::snprintf(displayValue, sizeof(displayValue), "%g", a_slider.value);

		ImGuiMCP::PushID(a_slider.identity);
		if (!a_slider.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		ImGuiMCP::SliderInt(TextOrEmpty(a_slider.label), &stepIndex, 0, stepCount, displayValue);
		const bool editCompleted = ImGuiMCP::IsItemDeactivatedAfterEdit();
		if (!a_slider.enabled) {
			ImGuiMCP::EndDisabled();
		}
		DrawDescription(a_slider.description);
		ImGuiMCP::PopID();

		return {
			stepIndex != initialStepIndex ? std::optional<int>{ stepIndex } : std::nullopt,
			editCompleted
		};
	}

	void RenderPage(const void* a_pageIdentity)
	{
		if (ModSettings::VisitPageSettings(a_pageIdentity, BeginGroup, EndGroup, DrawCheckbox, DrawSlider)) {
			ModSettings::CommitIniDirtyPage(a_pageIdentity);
		}
	}

	template <std::size_t Index>
	void __stdcall RenderPageSlot()
	{
		RenderPage(g_pageIdentities[Index]);
	}

	template <std::size_t... Indices>
	constexpr std::array<RenderFunction, sizeof...(Indices)> MakeRenderCallbacks(std::index_sequence<Indices...>)
	{
		return { &RenderPageSlot<Indices>... };
	}

	const auto kRenderCallbacks = MakeRenderCallbacks(std::make_index_sequence<kMaximumRegisteredPages>{});

	bool HasRequiredExports(HMODULE a_module)
	{
		for (const auto* exportName : kRequiredExportNames) {
			if (::GetProcAddress(a_module, exportName) == nullptr) {
				return false;
			}
		}
		return true;
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

		std::array<ModSettings::PageVisit, kMaximumRegisteredPages> pages = {};
		std::size_t pageCount = 0;
		bool pageLimitExceeded = false;
		ModSettings::ForEachLoadedPage([&](const ModSettings::PageVisit& page) {
			if (pageCount == pages.size()) {
				pageLimitExceeded = true;
				return;
			}
			pages[pageCount++] = page;
		});
		if (pageLimitExceeded) {
			logger::error("SKSE Menu Framework frontend supports at most {} settings pages", kMaximumRegisteredPages);
			return;
		}

		SKSEMenuFramework::SetSection("dMenu");
		for (std::size_t index = 0; index < pageCount; ++index) {
			g_pageIdentities[index] = pages[index].identity;
			SKSEMenuFramework::AddSectionItem(std::string(pages[index].name), kRenderCallbacks[index]);
		}
		initialized = true;
		logger::info("SKSE Menu Framework frontend registered");
	}
}
