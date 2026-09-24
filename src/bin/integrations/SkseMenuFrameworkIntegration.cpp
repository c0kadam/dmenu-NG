#include "PCH.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <utility>
#include <vector>

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
		"igBeginCombo",
		"igEndCombo",
		"igSelectable_Bool",
		"igInputText",
		"igTextColoredV",
		"igColorEdit4",
		"igButton",
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
		"igPushID_Int",
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

	std::optional<int> DrawDropdown(const ModSettings::DropdownVisit& a_dropdown)
	{
		int selectedIndex = a_dropdown.selectedIndex;
		const char* previewValue = "";
		if (selectedIndex >= 0 && static_cast<std::size_t>(selectedIndex) < a_dropdown.options.size()) {
			previewValue = a_dropdown.options[static_cast<std::size_t>(selectedIndex)].c_str();
		}

		ImGuiMCP::PushID(a_dropdown.identity);
		if (!a_dropdown.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		if (ImGuiMCP::BeginCombo(TextOrEmpty(a_dropdown.label), previewValue)) {
			for (std::size_t index = 0; index < a_dropdown.options.size(); ++index) {
				ImGuiMCP::PushID(static_cast<int>(index));
				if (ImGuiMCP::Selectable(a_dropdown.options[index].c_str(), selectedIndex == static_cast<int>(index))) {
					selectedIndex = static_cast<int>(index);
				}
				ImGuiMCP::PopID();
			}
			ImGuiMCP::EndCombo();
		}
		if (!a_dropdown.enabled) {
			ImGuiMCP::EndDisabled();
		}
		DrawDescription(a_dropdown.description);
		ImGuiMCP::PopID();

		return selectedIndex != a_dropdown.selectedIndex ? std::optional<int>{ selectedIndex } : std::nullopt;
	}

	struct TextboxBuffer
	{
		std::vector<char> characters;
	};

	int TextboxResizeCallback(ImGuiMCP::ImGuiInputTextCallbackData* a_data)
	{
		if (!a_data || a_data->EventFlag != ImGuiMCP::ImGuiInputTextFlags_CallbackResize) {
			return 0;
		}

		auto* buffer = static_cast<TextboxBuffer*>(a_data->UserData);
		if (!buffer) {
			return 0;
		}

		buffer->characters.resize(static_cast<std::size_t>(a_data->BufTextLen) + 1);
		a_data->Buf = buffer->characters.data();
		return 0;
	}

	ModSettings::TextboxUpdate DrawTextbox(const ModSettings::TextboxVisit& a_textbox)
	{
		TextboxBuffer buffer{};
		buffer.characters.assign(a_textbox.value.begin(), a_textbox.value.end());
		buffer.characters.push_back('\0');

		ImGuiMCP::PushID(a_textbox.identity);
		if (!a_textbox.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		const bool changed = ImGuiMCP::InputText(
			TextOrEmpty(a_textbox.label),
			buffer.characters.data(),
			buffer.characters.size(),
			ImGuiMCP::ImGuiInputTextFlags_CallbackResize,
			TextboxResizeCallback,
			&buffer);
		const bool editCompleted = ImGuiMCP::IsItemDeactivatedAfterEdit();
		if (!a_textbox.enabled) {
			ImGuiMCP::EndDisabled();
		}
		DrawDescription(a_textbox.description);
		ImGuiMCP::PopID();

		return {
			changed ? std::optional<std::string>{ buffer.characters.data() } : std::nullopt,
			editCompleted
		};
	}

	void DrawText(const ModSettings::TextVisit& a_text)
	{
		ImGuiMCP::PushID(a_text.identity);
		if (!a_text.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		ImGuiMCP::TextColored(
			ImGuiMCP::ImVec4{ a_text.color.red, a_text.color.green, a_text.color.blue, a_text.color.alpha },
			"%s",
			TextOrEmpty(a_text.label));
		if (!a_text.enabled) {
			ImGuiMCP::EndDisabled();
		}
		DrawDescription(a_text.description);
		ImGuiMCP::PopID();
	}

	ModSettings::ColorUpdate DrawColor(const ModSettings::ColorVisit& a_color)
	{
		float value[4] = {
			a_color.value.red,
			a_color.value.green,
			a_color.value.blue,
			a_color.value.alpha
		};

		ImGuiMCP::PushID(a_color.identity);
		if (!a_color.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		const bool changed = ImGuiMCP::ColorEdit4(
			TextOrEmpty(a_color.label),
			value,
			ImGuiMCP::ImGuiColorEditFlags_DisplayRGB | ImGuiMCP::ImGuiColorEditFlags_AlphaBar);
		const bool editCompleted = ImGuiMCP::IsItemDeactivatedAfterEdit();
		if (!a_color.enabled) {
			ImGuiMCP::EndDisabled();
		}
		DrawDescription(a_color.description);
		ImGuiMCP::PopID();

		return {
			changed ? std::optional<ModSettings::Rgba>{ ModSettings::Rgba{ value[0], value[1], value[2], value[3] } } : std::nullopt,
			editCompleted
		};
	}

	ModSettings::KeymapAction DrawKeymap(const ModSettings::KeymapVisit& a_keymap)
	{
		ModSettings::KeymapAction action = ModSettings::KeymapAction::None;

		ImGuiMCP::PushID(a_keymap.identity);
		if (!a_keymap.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		if (ImGuiMCP::Button("Remap")) {
			action = ModSettings::KeymapAction::BeginCapture;
		}
		ImGuiMCP::SameLine();
		if (ImGuiMCP::Button("Unmap")) {
			action = ModSettings::KeymapAction::Unmap;
		}
		ImGuiMCP::SameLine();
		ImGuiMCP::TextUnformatted(TextOrEmpty(a_keymap.label));
		ImGuiMCP::SameLine();
		ImGuiMCP::TextUnformatted(a_keymap.capturing ? "Press a key..." : TextOrEmpty(a_keymap.bindingLabel));
		if (!a_keymap.enabled) {
			ImGuiMCP::EndDisabled();
		}
		DrawDescription(a_keymap.description);
		ImGuiMCP::PopID();

		return action;
	}

	bool DrawButton(const ModSettings::ButtonVisit& a_button)
	{
		ImGuiMCP::PushID(a_button.identity);
		if (!a_button.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		const bool activated = ImGuiMCP::Button(TextOrEmpty(a_button.label));
		if (!a_button.enabled) {
			ImGuiMCP::EndDisabled();
		}
		DrawDescription(a_button.description);
		ImGuiMCP::PopID();
		return activated;
	}

	void RenderPage(const void* a_pageIdentity)
	{
		ModSettings::PageSettingsCallbacks callbacks{};
		callbacks.beginGroup = BeginGroup;
		callbacks.endGroup = EndGroup;
		callbacks.checkbox = DrawCheckbox;
		callbacks.slider = DrawSlider;
		callbacks.dropdown = DrawDropdown;
		callbacks.textbox = DrawTextbox;
		callbacks.text = DrawText;
		callbacks.color = DrawColor;
		callbacks.keymap = DrawKeymap;
		callbacks.button = DrawButton;

		if (ModSettings::VisitPageSettings(a_pageIdentity, callbacks)) {
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
