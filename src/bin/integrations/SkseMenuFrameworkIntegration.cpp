#include "PCH.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
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
		"igTextUnformatted",
		"igTextWrappedV",
		"igSeparator",
		"igSpacing",
		"igIndent",
		"igUnindent",
		"igAlignTextToFramePadding",
		"igGetContentRegionAvail",
		"igGetFrameHeight",
		"igSetNextItemWidth",
		"igBeginTable",
		"igEndTable",
		"igTableSetupColumn",
		"igTableNextRow",
		"igTableSetColumnIndex",
		"igPushTextWrapPos",
		"igPopTextWrapPos",
		"igSameLine",
		"igBeginDisabled",
		"igEndDisabled",
		"igIsItemHovered",
		"igIsItemFocused",
		"igBeginTooltip",
		"igEndTooltip",
		"igIsItemDeactivatedAfterEdit",
		"igPushID_Ptr",
		"igPushID_Int",
		"igPopID",
		"igCollapsingHeader_TreeNodeFlags",
		"igSetNextItemOpen"
	};

	constexpr std::size_t kMaximumRegisteredPages = 128;
	using RenderFunction = SKSEMenuFramework::Model::RenderFunction;
	std::array<const void*, kMaximumRegisteredPages> g_pageIdentities = {};
	std::array<std::string, kMaximumRegisteredPages> g_pageNames = {};
	constexpr float kStackedRowWidth = 500.0f;
	constexpr float kKeymapTableWidth = 680.0f;
	constexpr float kLabelWeight = 0.46f;
	constexpr float kControlWeight = 0.54f;
	constexpr float kSubsectionIndent = 16.0f;
	constexpr unsigned int kSettingsIcon = 0xf013;  // Font Awesome cog
	constexpr unsigned int kSlidersIcon = 0xf1de;   // Font Awesome sliders

	struct GroupStats
	{
		std::size_t directLeaves = 0;
		std::size_t descendantLeaves = 0;
		std::size_t sliders = 0;
		std::string_view soleLeafLabel;
	};

	struct GroupFrame
	{
		bool hasHeading = false;
		bool indented = false;
		bool major = false;
	};

	struct PresentationState
	{
		std::unordered_map<const void*, GroupStats> groupStats;
		std::vector<GroupFrame> groupStack;
		std::string_view pageName;
		std::string_view pageDescription;
		std::size_t headingDepth = 0;
		std::size_t majorCount = 0;
		bool subsectionIndented = false;
	};

	bool g_hasThemeStyles = false;
	bool g_hasFontAwesome = false;

	struct RowLayout
	{
		bool table = false;
		bool labelHelp = false;
	};

	const char* TextOrEmpty(const char* a_text)
	{
		return a_text ? a_text : "";
	}

	bool ItemRequestsHelp()
	{
		return ImGuiMCP::IsItemHovered() || ImGuiMCP::IsItemFocused();
	}

	ImGuiMCP::ImVec4 ThemeColor(ImGuiMCP::ImGuiCol a_color)
	{
		return *ImGuiMCP::GetStyleColorVec4(a_color);
	}

	ImGuiMCP::ImVec4 WithAlpha(ImGuiMCP::ImVec4 a_color, float a_factor)
	{
		a_color.w *= a_factor;
		return a_color;
	}

	ImGuiMCP::ImVec4 Blend(ImGuiMCP::ImVec4 a_base, ImGuiMCP::ImVec4 a_accent, float a_amount)
	{
		return {
			a_base.x + (a_accent.x - a_base.x) * a_amount,
			a_base.y + (a_accent.y - a_base.y) * a_amount,
			a_base.z + (a_accent.z - a_base.z) * a_amount,
			a_base.w
		};
	}

	ImGuiMCP::ImVec4 HeaderColor(ImGuiMCP::ImGuiCol a_color, ImGuiMCP::ImVec4 a_accent, float a_amount)
	{
		auto color = Blend(ThemeColor(a_color), a_accent, a_amount);
		color.w = (std::max)(color.w, 0.28f);
		return color;
	}

	void SecondaryText(const char* a_text)
	{
		if (!a_text || a_text[0] == '\0') {
			return;
		}
		if (g_hasThemeStyles) {
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, WithAlpha(ThemeColor(ImGuiMCP::ImGuiCol_Text), 0.76f));
		}
		ImGuiMCP::TextWrapped("%s", a_text);
		if (g_hasThemeStyles) {
			ImGuiMCP::PopStyleColor();
		}
	}

	void HelpMarker(bool a_emphasized)
	{
		if (g_hasThemeStyles) {
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text,
				WithAlpha(ThemeColor(ImGuiMCP::ImGuiCol_Text), a_emphasized ? 1.0f : 0.68f));
		}
		ImGuiMCP::TextUnformatted("?");
		if (g_hasThemeStyles) {
			ImGuiMCP::PopStyleColor();
		}
	}

	void DrawDescription(const char* a_description, bool a_targetHelp)
	{
		if (!a_description || a_description[0] == '\0') {
			return;
		}
		ImGuiMCP::SameLine();
		HelpMarker(a_targetHelp);
		if ((a_targetHelp || ItemRequestsHelp()) && ImGuiMCP::BeginTooltip()) {
			ImGuiMCP::TextUnformatted(a_description);
			ImGuiMCP::EndTooltip();
		}
	}

	bool RepeatsPageName(std::string_view a_groupName, std::string_view a_pageName)
	{
		if (a_groupName == a_pageName) {
			return true;
		}
		if (a_groupName.size() > a_pageName.size() &&
		    a_groupName.substr(a_groupName.size() - a_pageName.size()) == a_pageName) {
			return a_groupName.substr(0, a_groupName.size() - a_pageName.size()).ends_with(": ");
		}
		return false;
	}

	RowLayout BeginRow(const void* a_identity, const char* a_label, float a_tableWidth = kStackedRowWidth)
	{
		ImGuiMCP::PushID(a_identity);
		RowLayout row{};
		if (ImGuiMCP::GetContentRegionAvail().x >= a_tableWidth &&
		    ImGuiMCP::BeginTable("##setting_row", 3,
			    ImGuiMCP::ImGuiTableFlags_NoSavedSettings | ImGuiMCP::ImGuiTableFlags_SizingStretchProp)) {
			row.table = true;
			ImGuiMCP::TableSetupColumn("Setting", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, kLabelWeight);
			ImGuiMCP::TableSetupColumn("Control", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, kControlWeight);
			ImGuiMCP::TableSetupColumn("Help", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, ImGuiMCP::GetFrameHeight());
			ImGuiMCP::TableNextRow(0, ImGuiMCP::GetFrameHeight());
			ImGuiMCP::TableSetColumnIndex(0);
		}
		ImGuiMCP::AlignTextToFramePadding();
		ImGuiMCP::TextWrapped("%s", TextOrEmpty(a_label));
		row.labelHelp = ItemRequestsHelp();
		if (row.table) {
			ImGuiMCP::TableSetColumnIndex(1);
		}
		return row;
	}

	void SetControlWidth(const RowLayout& a_row)
	{
		const float available = ImGuiMCP::GetContentRegionAvail().x;
		ImGuiMCP::SetNextItemWidth((std::max)(1.0f, available - (a_row.table ? 12.0f : ImGuiMCP::GetFrameHeight() * 2.0f)));
	}

	void EndRow(const RowLayout& a_row, const char* a_description, bool a_controlHelp)
	{
		if (a_row.table) {
			ImGuiMCP::TableSetColumnIndex(2);
			if (a_description && a_description[0] != '\0') {
				HelpMarker(a_row.labelHelp || a_controlHelp);
				if ((a_row.labelHelp || a_controlHelp || ItemRequestsHelp()) && ImGuiMCP::BeginTooltip()) {
					ImGuiMCP::TextUnformatted(a_description);
					ImGuiMCP::EndTooltip();
				}
			}
			ImGuiMCP::EndTable();
		} else {
			DrawDescription(a_description, a_row.labelHelp || a_controlHelp);
		}
		ImGuiMCP::PopID();
	}

	bool BeginGroup(PresentationState& a_state, const ModSettings::GroupVisit& a_group)
	{
		ImGuiMCP::PushID(a_group.identity);
		const auto found = a_state.groupStats.find(a_group.identity);
		const GroupStats stats = found == a_state.groupStats.end() ? GroupStats{} : found->second;
		const bool repeatsPage = a_state.headingDepth == 0 && RepeatsPageName(TextOrEmpty(a_group.label), a_state.pageName);
		const bool singleLeafWrapper = stats.descendantLeaves == 1;
		GroupFrame frame{};
		bool showChildren = true;
		if (!repeatsPage && stats.descendantLeaves != 0) {
			ImGuiMCP::Spacing();
			if (singleLeafWrapper) {
				if (stats.soleLeafLabel != TextOrEmpty(a_group.label)) {
					SecondaryText(TextOrEmpty(a_group.label));
				}
			} else if (a_state.headingDepth == 0) {
				const auto label = (g_hasFontAwesome ?
					FontAwesome::UnicodeToUtf8(stats.sliders >= 2 && stats.sliders * 2 >= stats.descendantLeaves ? kSlidersIcon : kSettingsIcon) + "  " + TextOrEmpty(a_group.label) :
					std::string(TextOrEmpty(a_group.label))) + "###section";
				if (g_hasThemeStyles) {
					const auto accent = ThemeColor(ImGuiMCP::ImGuiCol_CheckMark);
					const auto normal = ThemeColor(ImGuiMCP::ImGuiCol_Text);
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, Blend(normal, accent, 0.64f));
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Header, HeaderColor(ImGuiMCP::ImGuiCol_Header, accent, 0.16f));
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderHovered, HeaderColor(ImGuiMCP::ImGuiCol_HeaderHovered, accent, 0.20f));
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderActive, HeaderColor(ImGuiMCP::ImGuiCol_HeaderActive, accent, 0.26f));
				}
				ImGuiMCP::SetNextItemOpen(a_state.majorCount++ == 0, ImGuiMCP::ImGuiCond_FirstUseEver);
				if (g_hasFontAwesome) {
					FontAwesome::PushSolid();
				}
				showChildren = ImGuiMCP::CollapsingHeader(label.c_str());
				if (g_hasFontAwesome) {
					FontAwesome::Pop();
				}
				if (g_hasThemeStyles) {
					ImGuiMCP::PopStyleColor(4);
				}
				frame.hasHeading = true;
				frame.major = true;
			} else {
				if (!a_state.subsectionIndented) {
					ImGuiMCP::Indent(kSubsectionIndent);
					frame.indented = true;
					a_state.subsectionIndented = true;
				}
				if (g_hasThemeStyles) {
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text,
						Blend(ThemeColor(ImGuiMCP::ImGuiCol_Text), ThemeColor(ImGuiMCP::ImGuiCol_CheckMark), 0.34f));
				}
				ImGuiMCP::TextUnformatted(TextOrEmpty(a_group.label));
				if (g_hasThemeStyles) {
					ImGuiMCP::PopStyleColor();
				}
				frame.hasHeading = true;
			}
			if (showChildren && a_group.description && a_group.description[0] != '\0') {
				SecondaryText(a_group.description);
			}
			if (frame.hasHeading) {
				++a_state.headingDepth;
			}
		}
		a_state.groupStack.push_back(frame);
		return showChildren;
	}

	void EndGroup(PresentationState& a_state)
	{
		const GroupFrame frame = a_state.groupStack.back();
		a_state.groupStack.pop_back();
		if (frame.hasHeading) {
			--a_state.headingDepth;
		}
		if (frame.indented) {
			ImGuiMCP::Unindent(kSubsectionIndent);
			a_state.subsectionIndented = false;
		}
		if (frame.major) {
			ImGuiMCP::Spacing();
		}
		ImGuiMCP::PopID();
	}

	std::unordered_map<const void*, GroupStats> CollectGroupStats(
		const void* a_pageIdentity, std::string_view a_pageName, std::string_view& a_pageDescription)
	{
		std::unordered_map<const void*, GroupStats> stats;
		std::vector<const void*> groupPath;
		const auto countLeaf = [&](const char* label) {
			if (!groupPath.empty()) {
				auto& group = stats[groupPath.back()];
				++group.directLeaves;
				group.soleLeafLabel = group.directLeaves == 1 ? TextOrEmpty(label) : std::string_view{};
				for (const auto* identity : groupPath) {
					++stats[identity].descendantLeaves;
				}
			}
		};
		ModSettings::PageSettingsCallbacks callbacks{};
		callbacks.beginGroup = [&](const ModSettings::GroupVisit& group) {
			if (groupPath.empty() && RepeatsPageName(TextOrEmpty(group.label), a_pageName) &&
				group.description && group.description[0] != '\0') {
				a_pageDescription = group.description;
			}
			groupPath.push_back(group.identity);
			return true;
		};
		callbacks.endGroup = [&](const ModSettings::GroupVisit&) { groupPath.pop_back(); };
		callbacks.checkbox = [&](const ModSettings::CheckboxVisit& visit) -> std::optional<bool> { countLeaf(visit.label); return std::nullopt; };
		callbacks.slider = [&](const ModSettings::SliderVisit& visit) {
			countLeaf(visit.label);
			for (const auto* identity : groupPath) {
				++stats[identity].sliders;
			}
			return ModSettings::SliderUpdate{};
		};
		callbacks.dropdown = [&](const ModSettings::DropdownVisit& visit) -> std::optional<int> { countLeaf(visit.label); return std::nullopt; };
		callbacks.textbox = [&](const ModSettings::TextboxVisit& visit) { countLeaf(visit.label); return ModSettings::TextboxUpdate{}; };
		callbacks.text = [&](const ModSettings::TextVisit& visit) { countLeaf(visit.label); };
		callbacks.color = [&](const ModSettings::ColorVisit& visit) { countLeaf(visit.label); return ModSettings::ColorUpdate{}; };
		callbacks.keymap = [&](const ModSettings::KeymapVisit& visit) { countLeaf(visit.label); return ModSettings::KeymapAction::None; };
		callbacks.button = [&](const ModSettings::ButtonVisit& visit) { countLeaf(visit.label); return false; };
		ModSettings::VisitPageSettings(a_pageIdentity, callbacks);
		return stats;
	}

	std::optional<bool> DrawCheckbox(const ModSettings::CheckboxVisit& a_checkbox)
	{
		bool value = a_checkbox.value;
		const RowLayout row = BeginRow(a_checkbox.identity, a_checkbox.label);
		if (!a_checkbox.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		const bool changed = ImGuiMCP::Checkbox("##value", &value);
		const bool controlHelp = ItemRequestsHelp();
		if (!a_checkbox.enabled) {
			ImGuiMCP::EndDisabled();
		}
		EndRow(row, a_checkbox.description, controlHelp);
		return changed ? std::optional<bool>{ value } : std::nullopt;
	}

	ModSettings::SliderUpdate DrawSlider(const ModSettings::SliderVisit& a_slider)
	{
		const int stepCount = Utils::SliderStepIndex(a_slider.max, a_slider.min, a_slider.step);
		const int initialStepIndex = Utils::SliderStepIndex(a_slider.value, a_slider.min, a_slider.step);
		int stepIndex = initialStepIndex;
		char displayValue[64] = {};
		std::snprintf(displayValue, sizeof(displayValue), "%g", a_slider.value);

		const RowLayout row = BeginRow(a_slider.identity, a_slider.label);
		if (!a_slider.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		SetControlWidth(row);
		ImGuiMCP::SliderInt("##value", &stepIndex, 0, stepCount, displayValue);
		const bool editCompleted = ImGuiMCP::IsItemDeactivatedAfterEdit();
		const bool controlHelp = ItemRequestsHelp();
		if (!a_slider.enabled) {
			ImGuiMCP::EndDisabled();
		}
		EndRow(row, a_slider.description, controlHelp);

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

		const RowLayout row = BeginRow(a_dropdown.identity, a_dropdown.label);
		if (!a_dropdown.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		SetControlWidth(row);
		const bool comboOpen = ImGuiMCP::BeginCombo("##value", previewValue);
		const bool controlHelp = ItemRequestsHelp();
		if (comboOpen) {
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
		EndRow(row, a_dropdown.description, controlHelp);

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

		const RowLayout row = BeginRow(a_textbox.identity, a_textbox.label);
		if (!a_textbox.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		SetControlWidth(row);
		const bool changed = ImGuiMCP::InputText(
			"##value",
			buffer.characters.data(),
			buffer.characters.size(),
			ImGuiMCP::ImGuiInputTextFlags_CallbackResize,
			TextboxResizeCallback,
			&buffer);
		const bool editCompleted = ImGuiMCP::IsItemDeactivatedAfterEdit();
		const bool controlHelp = ItemRequestsHelp();
		if (!a_textbox.enabled) {
			ImGuiMCP::EndDisabled();
		}
		EndRow(row, a_textbox.description, controlHelp);

		return {
			changed ? std::optional<std::string>{ buffer.characters.data() } : std::nullopt,
			editCompleted
		};
	}

	void DrawText(const ModSettings::TextVisit& a_text)
	{
		ImGuiMCP::PushID(a_text.identity);
		const std::string_view content = TextOrEmpty(a_text.label);
		const bool isSeparator = (!a_text.description || a_text.description[0] == '\0') &&
			!content.empty() &&
			content.find_first_not_of("-_= ") == std::string_view::npos;
		if (isSeparator) {
			ImGuiMCP::Separator();
			ImGuiMCP::PopID();
			return;
		}
		ImGuiMCP::Spacing();
		if (!a_text.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		ImGuiMCP::PushTextWrapPos();
		ImGuiMCP::TextColored(
			ImGuiMCP::ImVec4{ a_text.color.red, a_text.color.green, a_text.color.blue, a_text.color.alpha },
			"%s",
			TextOrEmpty(a_text.label));
		ImGuiMCP::PopTextWrapPos();
		const bool textHelp = ItemRequestsHelp();
		if (!a_text.enabled) {
			ImGuiMCP::EndDisabled();
		}
		DrawDescription(a_text.description, textHelp);
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

		const RowLayout row = BeginRow(a_color.identity, a_color.label);
		if (!a_color.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		ImGuiMCP::SetNextItemWidth((std::min)(ImGuiMCP::GetFrameHeight() * 2.5f, ImGuiMCP::GetContentRegionAvail().x));
		const bool changed = ImGuiMCP::ColorEdit4(
			"##value",
			value,
			ImGuiMCP::ImGuiColorEditFlags_DisplayRGB |
			ImGuiMCP::ImGuiColorEditFlags_AlphaBar |
			ImGuiMCP::ImGuiColorEditFlags_NoInputs |
			ImGuiMCP::ImGuiColorEditFlags_NoLabel);
		const bool editCompleted = ImGuiMCP::IsItemDeactivatedAfterEdit();
		const bool controlHelp = ItemRequestsHelp();
		if (!a_color.enabled) {
			ImGuiMCP::EndDisabled();
		}
		EndRow(row, a_color.description, controlHelp);

		return {
			changed ? std::optional<ModSettings::Rgba>{ ModSettings::Rgba{ value[0], value[1], value[2], value[3] } } : std::nullopt,
			editCompleted
		};
	}

	ModSettings::KeymapAction DrawKeymap(const ModSettings::KeymapVisit& a_keymap)
	{
		ModSettings::KeymapAction action = ModSettings::KeymapAction::None;
		const RowLayout row = BeginRow(a_keymap.identity, a_keymap.label, kKeymapTableWidth);
		if (!a_keymap.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		bool controlHelp = false;
		if (a_keymap.capturing) {
			SecondaryText("Press a key...");
			if (row.table) {
				ImGuiMCP::SameLine();
			}
			if (ImGuiMCP::Button("Cancel")) {
				action = ModSettings::KeymapAction::CancelCapture;
			}
			controlHelp = ItemRequestsHelp();
		} else {
			ImGuiMCP::TextUnformatted(TextOrEmpty(a_keymap.bindingLabel));
			if (row.table) {
				ImGuiMCP::SameLine();
			}
			if (ImGuiMCP::Button("Remap")) {
				action = ModSettings::KeymapAction::BeginCapture;
			}
			controlHelp = ItemRequestsHelp();
			if (a_keymap.mapped) {
				ImGuiMCP::SameLine();
				if (ImGuiMCP::Button("Unmap")) {
					action = ModSettings::KeymapAction::Unmap;
				}
				controlHelp |= ItemRequestsHelp();
			}
		}
		if (!a_keymap.enabled) {
			ImGuiMCP::EndDisabled();
		}
		EndRow(row, a_keymap.description, controlHelp);

		return action;
	}

	bool DrawButton(const ModSettings::ButtonVisit& a_button)
	{
		ImGuiMCP::PushID(a_button.identity);
		ImGuiMCP::Spacing();
		if (!a_button.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		const float width = (std::min)(280.0f, ImGuiMCP::GetContentRegionAvail().x);
		const bool activated = ImGuiMCP::Button(TextOrEmpty(a_button.label), ImGuiMCP::ImVec2{ width, 0.0f });
		const bool controlHelp = ItemRequestsHelp();
		if (!a_button.enabled) {
			ImGuiMCP::EndDisabled();
		}
		DrawDescription(a_button.description, controlHelp);
		ImGuiMCP::Spacing();
		ImGuiMCP::PopID();
		return activated;
	}

	void RenderPage(const void* a_pageIdentity, std::string_view a_pageName)
	{
		PresentationState presentation{};
		presentation.groupStats = CollectGroupStats(a_pageIdentity, a_pageName, presentation.pageDescription);
		presentation.pageName = a_pageName;
		if (g_hasFontAwesome) {
			FontAwesome::PushSolid();
			if (g_hasThemeStyles) {
				ImGuiMCP::TextColored(ThemeColor(ImGuiMCP::ImGuiCol_CheckMark), "%s", FontAwesome::UnicodeToUtf8(kSettingsIcon).c_str());
			} else {
				ImGuiMCP::TextUnformatted(FontAwesome::UnicodeToUtf8(kSettingsIcon).c_str());
			}
			FontAwesome::Pop();
			ImGuiMCP::SameLine();
		}
		ImGuiMCP::TextUnformatted(a_pageName.data(), a_pageName.data() + a_pageName.size());
		SecondaryText(presentation.pageDescription.data());
		ImGuiMCP::Separator();
		ImGuiMCP::Spacing();

		ModSettings::PageSettingsCallbacks callbacks{};
		callbacks.beginGroup = [&](const ModSettings::GroupVisit& group) { return BeginGroup(presentation, group); };
		callbacks.endGroup = [&](const ModSettings::GroupVisit&) { EndGroup(presentation); };
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
		RenderPage(g_pageIdentities[Index], g_pageNames[Index]);
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
		g_hasThemeStyles = ::GetProcAddress(module, "igGetStyleColorVec4") &&
			::GetProcAddress(module, "igPushStyleColor_Vec4") &&
			::GetProcAddress(module, "igPopStyleColor");
		g_hasFontAwesome = ::GetProcAddress(module, "PushSolid") && ::GetProcAddress(module, "Pop");

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
			g_pageNames[index] = pages[index].name;
			SKSEMenuFramework::AddSectionItem(g_pageNames[index], kRenderCallbacks[index]);
		}
		initialized = true;
		logger::info("SKSE Menu Framework frontend registered");
	}
}
