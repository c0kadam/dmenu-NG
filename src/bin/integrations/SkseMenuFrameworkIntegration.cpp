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
	constexpr float kSubsectionIndent = 10.0f;
	constexpr float kSubsectionContentIndent = 6.0f;
	constexpr float kMicroContentIndent = 4.0f;
	constexpr ImGuiMCP::ImVec4 kSteel{ 0.12f, 0.20f, 0.28f, 0.96f };
	constexpr ImGuiMCP::ImVec4 kSteelHover{ 0.17f, 0.29f, 0.40f, 1.0f };
	constexpr ImGuiMCP::ImVec4 kSteelActive{ 0.20f, 0.35f, 0.47f, 1.0f };
	constexpr ImGuiMCP::ImVec4 kControlSurface{ 0.09f, 0.16f, 0.22f, 0.94f };
	constexpr ImGuiMCP::ImVec4 kControlHover{ 0.15f, 0.25f, 0.34f, 1.0f };
	constexpr ImGuiMCP::ImVec4 kBlue{ 0.29f, 0.61f, 0.89f, 1.0f };
	constexpr ImGuiMCP::ImVec4 kBlueActive{ 0.44f, 0.72f, 0.95f, 1.0f };
	constexpr ImGuiMCP::ImVec4 kGold{ 0.85f, 0.66f, 0.36f, 1.0f };
	constexpr ImGuiMCP::ImVec4 kCyan{ 0.45f, 0.74f, 0.91f, 1.0f };
	constexpr ImGuiMCP::ImVec4 kMicroCyan{ 0.39f, 0.65f, 0.80f, 1.0f };
	constexpr ImGuiMCP::ImVec4 kBorder{ 0.23f, 0.33f, 0.42f, 0.85f };
	constexpr ImGuiMCP::ImVec4 kSubsectionSurface{ 0.12f, 0.24f, 0.32f, 0.52f };
	constexpr ImGuiMCP::ImVec4 kSubsectionHover{ 0.18f, 0.31f, 0.41f, 0.78f };
	constexpr ImGuiMCP::ImVec4 kSliderTrack{ 0.09f, 0.16f, 0.22f, 0.58f };

	// Stable Font Awesome Free glyphs used by the framework's solid font.
	constexpr unsigned int kPageIcon = 0xf009;      // th-large
	constexpr unsigned int kSettingsIcon = 0xf013;  // cog
	constexpr unsigned int kSlidersIcon = 0xf1de;   // sliders
	constexpr unsigned int kToggleIcon = 0xf205;    // toggle-on
	constexpr unsigned int kListIcon = 0xf03a;      // list
	constexpr unsigned int kTextIcon = 0xf031;      // font
	constexpr unsigned int kColorIcon = 0xf1fc;     // paint-brush
	constexpr unsigned int kKeyboardIcon = 0xf11c;  // keyboard
	constexpr unsigned int kActionIcon = 0xf0e7;    // bolt
	constexpr unsigned int kGroupsIcon = 0xf0e8;    // sitemap
	constexpr unsigned int kGamepadIcon = 0xf11b;   // gamepad
	constexpr unsigned int kPointerIcon = 0xf05b;   // crosshairs
	constexpr unsigned int kSizeIcon = 0xf065;      // expand
	constexpr unsigned int kSoundIcon = 0xf028;     // volume-up
	constexpr unsigned int kTimingIcon = 0xf017;    // clock
	constexpr unsigned int kOpacityIcon = 0xf042;   // adjust
	constexpr unsigned int kIndicatorIcon = 0xf06e; // eye
	constexpr unsigned int kPositionIcon = 0xf047;  // arrows

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

	struct GroupFrame
	{
		bool hasHeading = false;
		float indentWidth = 0.0f;
		float contentIndent = 0.0f;
		bool major = false;
		bool subsection = false;
		bool interactiveSubsection = false;
		const void* identity = nullptr;
	};

	struct PresentationState
	{
		std::unordered_map<const void*, GroupStats> groupStats;
		std::vector<GroupFrame> groupStack;
		std::string_view pageName;
		std::string_view pageDescription;
		std::size_t headingDepth = 0;
		std::size_t majorCount = 0;
		std::size_t subsectionCount = 0;
		std::size_t interactiveDepth = 0;
	};

	bool g_hasThemeStyles = false;
	bool g_hasStyleVars = false;
	bool g_hasFontAwesome = false;
	bool g_hasIconOverlay = false;
	bool g_hasCompactTree = false;

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

	std::string UpperAscii(std::string_view a_text)
	{
		std::string result(a_text);
		for (auto& character : result) {
			if (character >= 'a' && character <= 'z') {
				character = static_cast<char>(character - 'a' + 'A');
			}
		}
		return result;
	}

	SemanticRole SemanticForId(std::string_view a_id)
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

	unsigned int IconForSemantic(SemanticRole a_role)
	{
		switch (a_role) {
		case SemanticRole::Gamepad: return kGamepadIcon;
		case SemanticRole::Keyboard: return kKeyboardIcon;
		case SemanticRole::Pointer: return kPointerIcon;
		case SemanticRole::Indicator: return kIndicatorIcon;
		case SemanticRole::Position: return kPositionIcon;
		case SemanticRole::Size: return kSizeIcon;
		case SemanticRole::Sound: return kSoundIcon;
		case SemanticRole::Timing: return kTimingIcon;
		case SemanticRole::Opacity: return kOpacityIcon;
		case SemanticRole::Image: return kColorIcon;
		case SemanticRole::Color: return kColorIcon;
		default: return 0;
		}
	}

	unsigned int IconForKind(VisualKind a_kind)
	{
		switch (a_kind) {
		case VisualKind::Checkbox: return kToggleIcon;
		case VisualKind::Slider: return kSlidersIcon;
		case VisualKind::Dropdown: return kListIcon;
		case VisualKind::Textbox: return kTextIcon;
		case VisualKind::Color: return kColorIcon;
		case VisualKind::Keymap: return kKeyboardIcon;
		case VisualKind::Button: return kActionIcon;
		default: return kSettingsIcon;
		}
	}

	unsigned int IconForGroup(const GroupStats& a_stats)
	{
		std::size_t bestSemantic = 0;
		unsigned int semanticIcon = 0;
		for (std::size_t index = 1; index < static_cast<std::size_t>(SemanticRole::Count); ++index) {
			if (a_stats.semantics[index] > bestSemantic) {
				bestSemantic = a_stats.semantics[index];
				semanticIcon = IconForSemantic(static_cast<SemanticRole>(index));
			}
		}
		const auto interactiveLeaves = a_stats.descendantLeaves - a_stats.kinds[static_cast<std::size_t>(VisualKind::Text)];
		if (bestSemantic >= 2 && bestSemantic * 3 >= interactiveLeaves) {
			return semanticIcon;
		}
		constexpr std::array priority{
			VisualKind::Keymap, VisualKind::Color, VisualKind::Slider,
			VisualKind::Button, VisualKind::Textbox, VisualKind::Dropdown, VisualKind::Checkbox
		};
		std::size_t best = 0;
		unsigned int icon = a_stats.directGroups > 1 ? kGroupsIcon : kSettingsIcon;
		for (const auto kind : priority) {
			const auto count = a_stats.kinds[static_cast<std::size_t>(kind)];
			if (count > best) {
				best = count;
				icon = IconForKind(kind);
			}
		}
		return best >= 2 && (best * 2 >= interactiveLeaves || (best >= 3 && best * 3 >= interactiveLeaves)) ?
			icon : (a_stats.directGroups > 1 ? kGroupsIcon : kSettingsIcon);
	}

	bool ShouldCollapseSubsection(const GroupStats& a_stats)
	{
		const auto interactiveLeaves = a_stats.descendantLeaves - a_stats.kinds[static_cast<std::size_t>(VisualKind::Text)];
		const auto keymaps = a_stats.kinds[static_cast<std::size_t>(VisualKind::Keymap)];
		return interactiveLeaves >= 4 &&
			(a_stats.directInteractiveLeaves >= 3 || a_stats.directGroups == 0 || keymaps >= 5 ||
				(a_stats.directGroups >= 2 && interactiveLeaves >= 8));
	}

	bool ShouldCollapseMicro(const GroupStats& a_stats)
	{
		const auto interactiveLeaves = a_stats.descendantLeaves - a_stats.kinds[static_cast<std::size_t>(VisualKind::Text)];
		return a_stats.directInteractiveLeaves >= 3 || interactiveLeaves >= 4 ||
			a_stats.kinds[static_cast<std::size_t>(VisualKind::Keymap)] >= 2 ||
			a_stats.kinds[static_cast<std::size_t>(VisualKind::Slider)] >= 3;
	}

	bool OnlyMeaningfulChild(const PresentationState& a_state)
	{
		if (a_state.groupStack.empty()) {
			return false;
		}
		const auto parent = a_state.groupStats.find(a_state.groupStack.back().identity);
		if (parent == a_state.groupStats.end() || parent->second.directInteractiveLeaves != 0) {
			return false;
		}
		std::size_t meaningfulChildren = 0;
		for (const auto* child : parent->second.childGroups) {
			const auto found = a_state.groupStats.find(child);
			if (found != a_state.groupStats.end() &&
				found->second.descendantLeaves > found->second.kinds[static_cast<std::size_t>(VisualKind::Text)]) {
				++meaningfulChildren;
			}
		}
		return meaningfulChildren == 1;
	}

	void DrawIcon(unsigned int a_codepoint, ImGuiMCP::ImVec4 a_color)
	{
		if (!g_hasFontAwesome) {
			return;
		}
		const auto glyph = FontAwesome::UnicodeToUtf8(a_codepoint);
		FontAwesome::PushSolid();
		ImGuiMCP::TextColored(a_color, "%s", glyph.c_str());
		FontAwesome::Pop();
	}

	void DrawHeaderIcon(unsigned int a_codepoint, ImGuiMCP::ImVec4 a_color, bool a_compact = false)
	{
		if (!g_hasFontAwesome || !g_hasIconOverlay) {
			return;
		}
		const auto afterHeader = ImGuiMCP::GetCursorScreenPos();
		const auto header = ImGuiMCP::GetItemRectMin();
		ImGuiMCP::SetCursorScreenPos({ header.x + ImGuiMCP::GetFrameHeight() * 0.94f,
			header.y + (a_compact ? 1.0f : 4.0f) });
		DrawIcon(a_codepoint, a_color);
		ImGuiMCP::SetCursorScreenPos(afterHeader);
	}

	int PushControlStyle(VisualKind a_kind)
	{
		if (!g_hasThemeStyles) {
			return 0;
		}
		if (a_kind == VisualKind::Button || a_kind == VisualKind::Keymap) {
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Button, kSteel);
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, kSteelHover);
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive, kSteelActive);
			return 3;
		}
		ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_FrameBg,
			a_kind == VisualKind::Slider ? kSliderTrack : kControlSurface);
		ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_FrameBgHovered,
			a_kind == VisualKind::Slider ? WithAlpha(kControlHover, 0.78f) : kControlHover);
		ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_FrameBgActive, kSteelHover);
		if (a_kind == VisualKind::Slider) {
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_SliderGrab, kBlue);
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_SliderGrabActive, kBlueActive);
			return 5;
		}
		if (a_kind == VisualKind::Checkbox) {
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_CheckMark, kBlueActive);
			return 4;
		}
		return 3;
	}

	void SecondaryText(const char* a_text)
	{
		if (!a_text || a_text[0] == '\0') {
			return;
		}
		if (g_hasThemeStyles) {
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, WithAlpha(ThemeColor(ImGuiMCP::ImGuiCol_Text), 0.72f));
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

	RowLayout BeginRow(const void* a_identity, const char* a_label, VisualKind a_kind,
		unsigned int a_icon = 0, float a_tableWidth = kStackedRowWidth)
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
		if (g_hasFontAwesome) {
			DrawIcon(a_icon ? a_icon : IconForKind(a_kind), WithAlpha(kGold, 0.58f));
			ImGuiMCP::SameLine();
		}
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
		const GroupStats emptyStats{};
		const GroupStats& stats = found == a_state.groupStats.end() ? emptyStats : found->second;
		const bool repeatsPage = a_state.headingDepth == 0 && RepeatsPageName(TextOrEmpty(a_group.label), a_state.pageName);
		const bool singleLeafWrapper = a_state.headingDepth == 0 && stats.descendantLeaves == 1 &&
			stats.directGroups == 0 && stats.soleLeafLabel == TextOrEmpty(a_group.label);
		GroupFrame frame{};
		frame.identity = a_group.identity;
		bool showChildren = true;
		if (!repeatsPage && stats.descendantLeaves != 0) {
			if (a_state.headingDepth <= 1) {
				ImGuiMCP::Spacing();
			}
			if (singleLeafWrapper) {
				SecondaryText(a_group.description);
			} else if (a_state.headingDepth == 0) {
				const auto label = std::string(g_hasFontAwesome && g_hasIconOverlay ? "      " : "") +
					UpperAscii(TextOrEmpty(a_group.label)) + "###section";
				if (g_hasThemeStyles) {
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, ThemeColor(ImGuiMCP::ImGuiCol_Text));
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Header, kSteel);
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderHovered, kSteelHover);
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderActive, kSteelActive);
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Border, kBorder);
				}
				if (g_hasStyleVars) {
					ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FramePadding, ImGuiMCP::ImVec2{ 8.0f, 5.0f });
					ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameBorderSize, 1.0f);
				}
				a_state.subsectionCount = 0;
				ImGuiMCP::SetNextItemOpen(a_state.majorCount++ == 0, ImGuiMCP::ImGuiCond_FirstUseEver);
				showChildren = ImGuiMCP::CollapsingHeader(label.c_str());
				DrawHeaderIcon(IconForGroup(stats), kGold);
				if (g_hasStyleVars) {
					ImGuiMCP::PopStyleVar(2);
				}
				if (g_hasThemeStyles) {
					ImGuiMCP::PopStyleColor(5);
				}
				frame.hasHeading = true;
				frame.major = true;
				if (showChildren) {
					ImGuiMCP::Indent(kSubsectionIndent);
					frame.indentWidth = kSubsectionIndent;
				}
			} else if (a_state.headingDepth == 1) {
				ImGuiMCP::Indent(kSubsectionIndent);
				frame.indentWidth = kSubsectionIndent;
				const bool collapsible = g_hasThemeStyles && a_state.interactiveDepth == 0 && ShouldCollapseSubsection(stats);
				if (collapsible) {
					const auto label = std::string(g_hasFontAwesome && g_hasIconOverlay ? "      " : "") +
						UpperAscii(TextOrEmpty(a_group.label)) + "###subsection";
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, kCyan);
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Header, kSubsectionSurface);
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderHovered, kSubsectionHover);
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderActive, kSteelHover);
					if (g_hasStyleVars) {
						ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FramePadding, ImGuiMCP::ImVec2{ 4.0f, 3.0f });
					}
					ImGuiMCP::SetNextItemOpen(a_state.subsectionCount++ == 0, ImGuiMCP::ImGuiCond_FirstUseEver);
					showChildren = ImGuiMCP::CollapsingHeader(label.c_str());
					DrawHeaderIcon(IconForGroup(stats), kCyan);
					if (g_hasStyleVars) {
						ImGuiMCP::PopStyleVar();
					}
					ImGuiMCP::PopStyleColor(4);
					frame.interactiveSubsection = true;
					++a_state.interactiveDepth;
				} else {
					if (g_hasFontAwesome) {
						DrawIcon(IconForGroup(stats), kCyan);
						ImGuiMCP::SameLine();
					}
					ImGuiMCP::TextColored(kCyan, "%s", UpperAscii(TextOrEmpty(a_group.label)).c_str());
					if (g_hasThemeStyles) {
						ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Separator, WithAlpha(kCyan, 0.46f));
					}
					ImGuiMCP::Separator();
					if (g_hasThemeStyles) {
						ImGuiMCP::PopStyleColor();
					}
				}
				frame.hasHeading = true;
				frame.subsection = true;
				if (showChildren) {
					ImGuiMCP::Indent(kSubsectionContentIndent);
					frame.contentIndent = kSubsectionContentIndent;
				}
			} else {
				ImGuiMCP::Indent(kSubsectionContentIndent);
				frame.indentWidth = kSubsectionContentIndent;
				const bool collapsible = a_state.headingDepth == 2 && g_hasThemeStyles && ShouldCollapseMicro(stats);
				if (collapsible) {
					const auto label = std::string(g_hasFontAwesome && g_hasIconOverlay ? "      " : "") +
						UpperAscii(TextOrEmpty(a_group.label)) + "###micro";
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, kMicroCyan);
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Header, WithAlpha(kMicroCyan, 0.0f));
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderHovered, WithAlpha(kMicroCyan, 0.11f));
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderActive, WithAlpha(kMicroCyan, 0.18f));
					if (g_hasStyleVars) {
						ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FramePadding, ImGuiMCP::ImVec2{ 2.0f, 1.0f });
					}
					ImGuiMCP::SetNextItemOpen(OnlyMeaningfulChild(a_state), ImGuiMCP::ImGuiCond_FirstUseEver);
					showChildren = g_hasCompactTree ?
						ImGuiMCP::TreeNodeEx(label.c_str(),
							ImGuiMCP::ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiMCP::ImGuiTreeNodeFlags_SpanAvailWidth) :
						ImGuiMCP::CollapsingHeader(label.c_str());
					DrawHeaderIcon(IconForGroup(stats), WithAlpha(kMicroCyan, 0.85f), true);
					if (g_hasStyleVars) {
						ImGuiMCP::PopStyleVar();
					}
					ImGuiMCP::PopStyleColor(4);
				} else {
					if (g_hasFontAwesome) {
						DrawIcon(IconForGroup(stats), WithAlpha(kMicroCyan, 0.85f));
						ImGuiMCP::SameLine();
					}
					ImGuiMCP::TextColored(kMicroCyan, "%s", UpperAscii(TextOrEmpty(a_group.label)).c_str());
				}
				if (g_hasThemeStyles) {
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Separator, WithAlpha(kCyan, 0.25f));
				}
				ImGuiMCP::Separator();
				if (g_hasThemeStyles) {
					ImGuiMCP::PopStyleColor();
				}
				frame.hasHeading = true;
				if (showChildren) {
					ImGuiMCP::Indent(kMicroContentIndent);
					frame.contentIndent = kMicroContentIndent;
				}
			}
			if (frame.hasHeading && showChildren && a_group.description && a_group.description[0] != '\0') {
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
		if (frame.interactiveSubsection) {
			--a_state.interactiveDepth;
		}
		if (frame.contentIndent > 0.0f) {
			ImGuiMCP::Unindent(frame.contentIndent);
		}
		if (frame.indentWidth > 0.0f) {
			ImGuiMCP::Unindent(frame.indentWidth);
		}
		if (frame.major || frame.subsection) {
			ImGuiMCP::Spacing();
		}
		ImGuiMCP::PopID();
	}

	std::unordered_map<const void*, GroupStats> CollectGroupStats(
		const void* a_pageIdentity, std::string_view a_pageName, std::string_view& a_pageDescription)
	{
		std::unordered_map<const void*, GroupStats> stats;
		std::vector<const void*> groupPath;
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
			}
			groupPath.push_back(group.identity);
			return true;
		};
		callbacks.endGroup = [&](const ModSettings::GroupVisit&) { groupPath.pop_back(); };
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
		callbacks.text = [&](const ModSettings::TextVisit& visit) { countLeaf(visit.label, VisualKind::Text); };
		callbacks.color = [&](const ModSettings::ColorVisit& visit) { countLeaf(visit.label, VisualKind::Color); return ModSettings::ColorUpdate{}; };
		callbacks.keymap = [&](const ModSettings::KeymapVisit& visit) {
			countLeaf(visit.label, VisualKind::Keymap, SemanticForId(visit.semanticId));
			return ModSettings::KeymapAction::None;
		};
		callbacks.button = [&](const ModSettings::ButtonVisit& visit) { countLeaf(visit.label, VisualKind::Button); return false; };
		ModSettings::VisitPageSettings(a_pageIdentity, callbacks);
		return stats;
	}

	std::optional<bool> DrawCheckbox(const ModSettings::CheckboxVisit& a_checkbox)
	{
		bool value = a_checkbox.value;
		const RowLayout row = BeginRow(a_checkbox.identity, a_checkbox.label, VisualKind::Checkbox,
			IconForSemantic(SemanticForId(a_checkbox.semanticId)));
		if (!a_checkbox.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		const int styleColors = PushControlStyle(VisualKind::Checkbox);
		const bool changed = ImGuiMCP::Checkbox("##value", &value);
		if (styleColors) {
			ImGuiMCP::PopStyleColor(styleColors);
		}
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

		const RowLayout row = BeginRow(a_slider.identity, a_slider.label, VisualKind::Slider,
			IconForSemantic(SemanticForId(a_slider.semanticId)));
		if (!a_slider.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		SetControlWidth(row);
		const int styleColors = PushControlStyle(VisualKind::Slider);
		if (g_hasStyleVars) {
			ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameRounding, 4.0f);
			ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_GrabRounding, 4.0f);
		}
		ImGuiMCP::SliderInt("##value", &stepIndex, 0, stepCount, displayValue);
		if (g_hasStyleVars) {
			ImGuiMCP::PopStyleVar(2);
		}
		if (styleColors) {
			ImGuiMCP::PopStyleColor(styleColors);
		}
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

		const RowLayout row = BeginRow(a_dropdown.identity, a_dropdown.label, VisualKind::Dropdown);
		if (!a_dropdown.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		SetControlWidth(row);
		const int styleColors = PushControlStyle(VisualKind::Dropdown);
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
		if (styleColors) {
			ImGuiMCP::PopStyleColor(styleColors);
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

		const RowLayout row = BeginRow(a_textbox.identity, a_textbox.label, VisualKind::Textbox);
		if (!a_textbox.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		SetControlWidth(row);
		const int styleColors = PushControlStyle(VisualKind::Textbox);
		const bool changed = ImGuiMCP::InputText(
			"##value",
			buffer.characters.data(),
			buffer.characters.size(),
			ImGuiMCP::ImGuiInputTextFlags_CallbackResize,
			TextboxResizeCallback,
			&buffer);
		if (styleColors) {
			ImGuiMCP::PopStyleColor(styleColors);
		}
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

		const RowLayout row = BeginRow(a_color.identity, a_color.label, VisualKind::Color);
		if (!a_color.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		ImGuiMCP::SetNextItemWidth((std::min)(ImGuiMCP::GetFrameHeight() * 2.5f, ImGuiMCP::GetContentRegionAvail().x));
		const int styleColors = PushControlStyle(VisualKind::Color);
		const bool changed = ImGuiMCP::ColorEdit4(
			"##value",
			value,
			ImGuiMCP::ImGuiColorEditFlags_DisplayRGB |
			ImGuiMCP::ImGuiColorEditFlags_AlphaBar |
			ImGuiMCP::ImGuiColorEditFlags_NoInputs |
			ImGuiMCP::ImGuiColorEditFlags_NoLabel);
		if (styleColors) {
			ImGuiMCP::PopStyleColor(styleColors);
		}
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
		const RowLayout row = BeginRow(a_keymap.identity, a_keymap.label, VisualKind::Keymap,
			IconForSemantic(SemanticForId(a_keymap.semanticId)), kKeymapTableWidth);
		if (!a_keymap.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		const int styleColors = PushControlStyle(VisualKind::Keymap);
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
		if (styleColors) {
			ImGuiMCP::PopStyleColor(styleColors);
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
		const int styleColors = PushControlStyle(VisualKind::Button);
		const auto label = g_hasFontAwesome ? FontAwesome::UnicodeToUtf8(kActionIcon) + "  " + TextOrEmpty(a_button.label) :
			std::string(TextOrEmpty(a_button.label));
		if (g_hasFontAwesome) {
			FontAwesome::PushSolid();
		}
		const bool activated = ImGuiMCP::Button(label.c_str(), ImGuiMCP::ImVec2{ width, 0.0f });
		if (g_hasFontAwesome) {
			FontAwesome::Pop();
		}
		if (styleColors) {
			ImGuiMCP::PopStyleColor(styleColors);
		}
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
			DrawIcon(kPageIcon, kGold);
			ImGuiMCP::SameLine();
		}
		const auto title = UpperAscii(a_pageName);
		ImGuiMCP::TextUnformatted(title.c_str());
		SecondaryText(presentation.pageDescription.data());
		ImGuiMCP::Spacing();
		if (g_hasThemeStyles) {
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Separator, kBorder);
		}
		ImGuiMCP::Separator();
		if (g_hasThemeStyles) {
			ImGuiMCP::PopStyleColor();
		}
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
		g_hasStyleVars = ::GetProcAddress(module, "igPushStyleVar_Float") &&
			::GetProcAddress(module, "igPushStyleVar_Vec2") &&
			::GetProcAddress(module, "igPopStyleVar");
		g_hasFontAwesome = ::GetProcAddress(module, "PushSolid") && ::GetProcAddress(module, "Pop");
		g_hasIconOverlay = ::GetProcAddress(module, "igGetCursorScreenPos") &&
			::GetProcAddress(module, "igSetCursorScreenPos") &&
			::GetProcAddress(module, "igGetItemRectMin");
		g_hasCompactTree = ::GetProcAddress(module, "igTreeNodeEx_Str") != nullptr;

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
