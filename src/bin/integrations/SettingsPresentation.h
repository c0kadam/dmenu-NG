#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../menus/ModSettings.h"

namespace SettingsPresentation
{
	enum class VisualKind : std::size_t
	{
		Checkbox,
		Slider,
		Dropdown,
		Textbox,
		Color,
		Keymap,
		Button,
		Text,
		Count
	};

	enum class SemanticRole : std::size_t
	{
		None,
		Gamepad,
		Keyboard,
		Pointer,
		Indicator,
		Position,
		Size,
		Sound,
		Timing,
		Opacity,
		Image,
		Color,
		Count
	};

	struct GroupStats
	{
		std::size_t directLeaves = 0;
		std::size_t directInteractiveLeaves = 0;
		std::size_t directGroups = 0;
		std::size_t descendantLeaves = 0;
		std::vector<const void*> childGroups;
		std::array<std::size_t, static_cast<std::size_t>(VisualKind::Count)> kinds{};
		std::array<std::size_t, static_cast<std::size_t>(SemanticRole::Count)> semantics{};
		std::string_view soleLeafLabel;
	};

	inline const char* TextOrEmpty(const char* a_text)
	{
		return a_text ? a_text : "";
	}

	inline bool IsStructuralText(const ModSettings::TextVisit& a_text)
	{
		const std::string_view label = TextOrEmpty(a_text.label);
		if ((a_text.description && a_text.description[0] != '\0') || label.empty() || label.size() > 64 ||
			label.find_first_of("\r\n") != std::string_view::npos ||
			label.find_first_not_of(" \t-_=.") == std::string_view::npos) {
			return false;
		}
		const auto last = label.find_last_not_of(" \t");
		return last != std::string_view::npos &&
			std::string_view(".!?;:").find(label[last]) == std::string_view::npos;
	}

	inline std::string UpperAscii(std::string_view a_text)
	{
		std::string result(a_text);
		for (auto& character : result) {
			if (character >= 'a' && character <= 'z') {
				character = static_cast<char>(character - 'a' + 'A');
			}
		}
		return result;
	}

	inline SemanticRole SemanticForId(std::string_view a_id)
	{
		const auto id = UpperAscii(a_id);
		const auto has = [&](std::string_view term) { return id.find(term) != std::string::npos; };
		if (has("GAMEPAD") || has("CONTROLLER") || has("DPAD")) return SemanticRole::Gamepad;
		if (has("CURSOR") || has("POINTER")) return SemanticRole::Pointer;
		if (has("INDICATOR")) return SemanticRole::Indicator;
		if (has("POSITION") || has("OFFSET")) return SemanticRole::Position;
		if (has("KEYBOARD") || has("MOUSE") || has("INPUT") || has("BIND")) return SemanticRole::Keyboard;
		if (has("VOLUME") || has("SOUND") || has("AUDIO")) return SemanticRole::Sound;
		if (has("DELAY") || has("DURATION") || has("COOLDOWN") || has("TIME")) return SemanticRole::Timing;
		if (has("OPACITY") || has("ALPHA")) return SemanticRole::Opacity;
		if (has("RADIUS") || has("DIAMETER") || has("SIZE") || has("SCALE") || has("WIDTH") || has("HEIGHT")) return SemanticRole::Size;
		if (has("COLOR") || has("COLOUR") || has("TINT")) return SemanticRole::Color;
		if (has("SKIN") || has("TEXTURE") || has("IMAGE") || has("RESKIN")) return SemanticRole::Image;
		return SemanticRole::None;
	}

	inline bool ShouldCollapseSubsection(const GroupStats& a_stats)
	{
		const auto interactiveLeaves = a_stats.descendantLeaves - a_stats.kinds[static_cast<std::size_t>(VisualKind::Text)];
		const auto keymaps = a_stats.kinds[static_cast<std::size_t>(VisualKind::Keymap)];
		return interactiveLeaves >= 4 &&
			(a_stats.directInteractiveLeaves >= 3 || a_stats.directGroups == 0 || keymaps >= 5 ||
				(a_stats.directGroups >= 2 && interactiveLeaves >= 8));
	}

	inline bool ShouldCollapseMicro(const GroupStats& a_stats)
	{
		const auto interactiveLeaves = a_stats.descendantLeaves - a_stats.kinds[static_cast<std::size_t>(VisualKind::Text)];
		return a_stats.directInteractiveLeaves >= 3 || interactiveLeaves >= 4 ||
			a_stats.kinds[static_cast<std::size_t>(VisualKind::Keymap)] >= 2 ||
			a_stats.kinds[static_cast<std::size_t>(VisualKind::Slider)] >= 3;
	}

	inline bool ShouldCollapseVirtual(const GroupStats& a_stats)
	{
		return a_stats.directInteractiveLeaves >= 3 ||
			a_stats.kinds[static_cast<std::size_t>(VisualKind::Slider)] >= 2 ||
			a_stats.kinds[static_cast<std::size_t>(VisualKind::Keymap)] >= 2;
	}

	inline bool RepeatsPageName(std::string_view a_groupName, std::string_view a_pageName)
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

	inline std::unordered_map<const void*, GroupStats> CollectGroupStats(
		const void* a_pageIdentity, std::string_view a_pageName, std::string_view& a_pageDescription,
		std::unordered_map<const void*, GroupStats>& a_virtualStats)
	{
		std::unordered_map<const void*, GroupStats> stats;
		std::vector<const void*> groupPath;
		std::vector<const void*> activeMarkers;
		const auto countLeaf = [&](const char* label, VisualKind kind, SemanticRole role = SemanticRole::None) {
			if (!groupPath.empty()) {
				auto& group = stats[groupPath.back()];
				++group.directLeaves;
				if (kind != VisualKind::Text) {
					++group.directInteractiveLeaves;
				}
				group.soleLeafLabel = group.directLeaves == 1 ? TextOrEmpty(label) : std::string_view{};
				for (const auto* identity : groupPath) {
					auto& ancestor = stats[identity];
					++ancestor.descendantLeaves;
					++ancestor.kinds[static_cast<std::size_t>(kind)];
					if (role != SemanticRole::None) {
						++ancestor.semantics[static_cast<std::size_t>(role)];
					}
				}
				if (kind != VisualKind::Text && activeMarkers.back()) {
					auto& run = a_virtualStats[activeMarkers.back()];
					++run.directInteractiveLeaves;
					++run.descendantLeaves;
					++run.kinds[static_cast<std::size_t>(kind)];
					if (role != SemanticRole::None) {
						++run.semantics[static_cast<std::size_t>(role)];
					}
				}
			}
		};
		ModSettings::PageSettingsCallbacks callbacks{};
		callbacks.beginGroup = [&](const ModSettings::GroupVisit& group) {
			if (groupPath.empty() && RepeatsPageName(TextOrEmpty(group.label), a_pageName) &&
				group.description && group.description[0] != '\0') {
				a_pageDescription = group.description;
			}
			if (!groupPath.empty()) {
				++stats[groupPath.back()].directGroups;
				stats[groupPath.back()].childGroups.push_back(group.identity);
				activeMarkers.back() = nullptr;
			}
			groupPath.push_back(group.identity);
			activeMarkers.push_back(nullptr);
			return true;
		};
		callbacks.endGroup = [&](const ModSettings::GroupVisit&) {
			groupPath.pop_back();
			activeMarkers.pop_back();
		};
		callbacks.checkbox = [&](const ModSettings::CheckboxVisit& visit) -> std::optional<bool> {
			countLeaf(visit.label, VisualKind::Checkbox, SemanticForId(visit.semanticId));
			return std::nullopt;
		};
		callbacks.slider = [&](const ModSettings::SliderVisit& visit) {
			countLeaf(visit.label, VisualKind::Slider, SemanticForId(visit.semanticId));
			return ModSettings::SliderUpdate{};
		};
		callbacks.dropdown = [&](const ModSettings::DropdownVisit& visit) -> std::optional<int> { countLeaf(visit.label, VisualKind::Dropdown); return std::nullopt; };
		callbacks.textbox = [&](const ModSettings::TextboxVisit& visit) { countLeaf(visit.label, VisualKind::Textbox); return ModSettings::TextboxUpdate{}; };
		callbacks.text = [&](const ModSettings::TextVisit& visit) {
			if (!activeMarkers.empty() && IsStructuralText(visit)) {
				activeMarkers.back() = visit.identity;
				a_virtualStats.try_emplace(visit.identity);
			}
			countLeaf(visit.label, VisualKind::Text);
		};
		callbacks.color = [&](const ModSettings::ColorVisit& visit) { countLeaf(visit.label, VisualKind::Color); return ModSettings::ColorUpdate{}; };
		callbacks.keymap = [&](const ModSettings::KeymapVisit& visit) {
			countLeaf(visit.label, VisualKind::Keymap, SemanticForId(visit.semanticId));
			return ModSettings::KeymapAction::None;
		};
		callbacks.button = [&](const ModSettings::ButtonVisit& visit) { countLeaf(visit.label, VisualKind::Button); return false; };
		ModSettings::VisitPageSettings(a_pageIdentity, callbacks);
		return stats;
	}

}
