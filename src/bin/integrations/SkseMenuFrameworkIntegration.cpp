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

#include "SettingsPresentation.h"

#include "../InputListener.h"
#include "../Utils.h"
#include "../menus/ModSettings.h"
#include "../menus/Settings.h"

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
		"igCollapsingHeader_TreeNodeFlags"
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
	constexpr std::uint32_t kMouseLeftInput = 256;
	enum class FrontendTheme { SteelGold, JetBlack };
	struct ThemePalette
	{
		ImGuiMCP::ImVec4 majorSurface, majorHover, majorActive;
		ImGuiMCP::ImVec4 subsectionSurface, subsectionHover;
		ImGuiMCP::ImVec4 controlSurface, controlHover, sliderTrack, border;
		ImGuiMCP::ImVec4 interaction, interactionActive;
		ImGuiMCP::ImVec4 structuralAccent, subsectionAccent, microAccent;
		ImGuiMCP::ImVec4 primaryText, secondaryText;
	};
	constexpr ThemePalette kSteelGold{
		{ 0.12f, 0.20f, 0.28f, 0.96f }, { 0.17f, 0.29f, 0.40f, 1.0f }, { 0.20f, 0.35f, 0.47f, 1.0f },
		{ 0.12f, 0.24f, 0.32f, 0.52f }, { 0.18f, 0.31f, 0.41f, 0.78f },
		{ 0.09f, 0.16f, 0.22f, 0.94f }, { 0.15f, 0.25f, 0.34f, 1.0f }, { 0.09f, 0.16f, 0.22f, 0.58f },
		{ 0.23f, 0.33f, 0.42f, 0.85f },
		{ 0.29f, 0.61f, 0.89f, 1.0f }, { 0.44f, 0.72f, 0.95f, 1.0f },
		{ 0.85f, 0.66f, 0.36f, 1.0f }, { 0.45f, 0.74f, 0.91f, 1.0f }, { 0.39f, 0.65f, 0.80f, 1.0f },
		{ 0.94f, 0.94f, 0.94f, 1.0f }, { 0.94f, 0.94f, 0.94f, 0.72f }
	};
	constexpr ThemePalette kJetBlack{
		{ 0.063f, 0.071f, 0.082f, 0.98f }, { 0.102f, 0.114f, 0.129f, 1.0f }, { 0.133f, 0.149f, 0.169f, 1.0f },
		{ 0.075f, 0.082f, 0.090f, 0.88f }, { 0.133f, 0.145f, 0.157f, 0.95f },
		{ 0.055f, 0.067f, 0.078f, 0.98f }, { 0.094f, 0.110f, 0.125f, 1.0f }, { 0.055f, 0.067f, 0.078f, 0.90f },
		{ 0.204f, 0.220f, 0.239f, 0.95f },
		{ 0.675f, 0.537f, 0.302f, 1.0f }, { 0.784f, 0.663f, 0.431f, 1.0f },
		{ 0.776f, 0.631f, 0.357f, 1.0f }, { 0.720f, 0.642f, 0.494f, 1.0f }, { 0.647f, 0.623f, 0.565f, 1.0f },
		{ 0.925f, 0.925f, 0.925f, 1.0f }, { 0.624f, 0.639f, 0.659f, 1.0f }
	};
	FrontendTheme CurrentTheme() { return Settings::sksemf_jet_black ? FrontendTheme::JetBlack : FrontendTheme::SteelGold; }
	const ThemePalette& Palette() { return CurrentTheme() == FrontendTheme::JetBlack ? kJetBlack : kSteelGold; }

	// Stable Font Awesome Free glyphs used by the framework's solid font.
	constexpr unsigned int kPageIcon = 0xf009;      // th-large
	constexpr unsigned int kSettingsIcon = 0xf013;  // cog
	constexpr unsigned int kSlidersIcon = 0xf1de;   // sliders
	constexpr unsigned int kToggleIcon = 0xf205;    // toggle-on
	constexpr unsigned int kListIcon = 0xf03a;      // list
	constexpr unsigned int kTextIcon = 0xf031;      // font
	constexpr unsigned int kColorIcon = 0xf1fc;     // paint-brush
	constexpr unsigned int kKeyboardIcon = 0xf11c;  // keyboard
	constexpr unsigned int kMouseIcon = 0xf8cc;     // computer-mouse
	constexpr unsigned int kKeyIcon = 0xf084;       // key
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

	using SettingsPresentation::VisualKind;
	using SettingsPresentation::SemanticRole;
	using SettingsPresentation::GroupStats;
	using SettingsPresentation::TextOrEmpty;
	using SettingsPresentation::IsStructuralText;
	using SettingsPresentation::UpperAscii;
	using SettingsPresentation::SemanticForId;
	using SettingsPresentation::ShouldCollapseSubsection;
	using SettingsPresentation::ShouldCollapseMicro;
	using SettingsPresentation::ShouldCollapseVirtual;
	using SettingsPresentation::RepeatsPageName;
	using SettingsPresentation::CollectGroupStats;

	struct GroupFrame
	{
		bool hasHeading = false;
		float indentWidth = 0.0f;
		float contentIndent = 0.0f;
		bool major = false;
		bool subsection = false;
		bool interactiveSubsection = false;
		bool virtualActive = false;
		bool virtualVisible = true;
		const void* identity = nullptr;
	};

	struct PresentationState
	{
		std::unordered_map<const void*, GroupStats> groupStats;
		std::unordered_map<const void*, GroupStats> virtualStats;
		std::vector<GroupFrame> groupStack;
		std::string_view pageName;
		std::string_view pageDescription;
		std::size_t headingDepth = 0;
		std::size_t interactiveDepth = 0;
	};

	SettingsPresentation::InputDeviceClass ParentInputContext(const PresentationState& a_state)
	{
		for (auto frame = a_state.groupStack.rbegin(); frame != a_state.groupStack.rend(); ++frame) {
			const auto group = a_state.groupStats.find(frame->identity);
			if (group == a_state.groupStats.end()) continue;
			const auto device = SettingsPresentation::InputContext(group->second);
			if (device != SettingsPresentation::InputDeviceClass::Unknown) return device;
		}
		return SettingsPresentation::InputDeviceClass::Unknown;
	}

	unsigned int IconForKeymap(SettingsPresentation::InputDeviceClass a_device)
	{
		switch (a_device) {
		case SettingsPresentation::InputDeviceClass::Keyboard: return kKeyboardIcon;
		case SettingsPresentation::InputDeviceClass::Mouse: return kMouseIcon;
		case SettingsPresentation::InputDeviceClass::Gamepad: return kGamepadIcon;
		default: return kKeyIcon;
		}
	}

	bool g_hasThemeStyles = false;
	bool g_hasStyleVars = false;
	bool g_hasFontAwesome = false;
	bool g_hasIconOverlay = false;
	bool g_hasCompactTree = false;
	bool g_hasInputBridge = false;
	std::int64_t g_inputRegistrationId = -1;
	struct CancelTargetState
	{
		const void* page = nullptr;
		const void* keymap = nullptr;
		bool hovered = false;
		bool passingMouseClick = false;
		bool renderedThisPage = false;
		int renderedFrame = -1;
	};
	CancelTargetState g_cancelTarget;
	bool g_hasFrameCount = false;
	using FrameworkInputCallback = bool(__stdcall*)(RE::InputEvent*);
	using RegisterInputEventFunction = std::int64_t (*)(FrameworkInputCallback);

	struct RowLayout
	{
		bool table = false;
		bool labelHelp = false;
	};

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

	bool DrawMicroDisclosure(const char* a_label, const GroupStats& a_stats)
	{
		const auto label = std::string(g_hasFontAwesome && g_hasIconOverlay ? "      " : "") +
			UpperAscii(TextOrEmpty(a_label)) + "###micro";
		ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, Palette().microAccent);
		ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Header, WithAlpha(Palette().microAccent, 0.0f));
		ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderHovered, WithAlpha(Palette().microAccent, 0.11f));
		ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderActive, WithAlpha(Palette().microAccent, 0.18f));
		if (g_hasStyleVars) {
			ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FramePadding, ImGuiMCP::ImVec2{ 2.0f, 1.0f });
		}
		const bool open = g_hasCompactTree ?
			ImGuiMCP::TreeNodeEx(label.c_str(),
				ImGuiMCP::ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiMCP::ImGuiTreeNodeFlags_SpanAvailWidth) :
			ImGuiMCP::CollapsingHeader(label.c_str());
		DrawHeaderIcon(IconForGroup(a_stats), WithAlpha(Palette().microAccent, 0.85f), true);
		if (g_hasStyleVars) {
			ImGuiMCP::PopStyleVar();
		}
		ImGuiMCP::PopStyleColor(4);
		ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Separator, WithAlpha(Palette().subsectionAccent, 0.25f));
		ImGuiMCP::Separator();
		ImGuiMCP::PopStyleColor();
		return open;
	}

	int PushControlStyle(VisualKind a_kind)
	{
		if (!g_hasThemeStyles) {
			return 0;
		}
		if (a_kind == VisualKind::Button || a_kind == VisualKind::Keymap) {
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Button, Palette().majorSurface);
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, Palette().majorHover);
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive, Palette().majorActive);
			return 3;
		}
		ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_FrameBg,
			a_kind == VisualKind::Slider ? Palette().sliderTrack : Palette().controlSurface);
		ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_FrameBgHovered,
			a_kind == VisualKind::Slider ? WithAlpha(Palette().controlHover, 0.78f) : Palette().controlHover);
		ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_FrameBgActive, Palette().majorHover);
		if (a_kind == VisualKind::Slider) {
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_SliderGrab, Palette().interaction);
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_SliderGrabActive, Palette().interactionActive);
			return 5;
		}
		if (a_kind == VisualKind::Checkbox) {
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_CheckMark, Palette().interactionActive);
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
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text,
				CurrentTheme() == FrontendTheme::SteelGold ? WithAlpha(ThemeColor(ImGuiMCP::ImGuiCol_Text), 0.72f) : Palette().secondaryText);
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
				a_emphasized ? (CurrentTheme() == FrontendTheme::SteelGold ?
					ThemeColor(ImGuiMCP::ImGuiCol_Text) : Palette().primaryText) :
					(CurrentTheme() == FrontendTheme::SteelGold ? WithAlpha(ThemeColor(ImGuiMCP::ImGuiCol_Text), 0.68f) : Palette().secondaryText));
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
			DrawIcon(a_icon ? a_icon : IconForKind(a_kind), WithAlpha(Palette().structuralAccent,
				CurrentTheme() == FrontendTheme::JetBlack ? 0.85f : 0.58f));
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

	void CloseVirtualGroup(PresentationState& a_state)
	{
		if (a_state.groupStack.empty()) {
			return;
		}
		auto& frame = a_state.groupStack.back();
		if (frame.virtualActive && frame.virtualVisible) {
			ImGuiMCP::Unindent(kMicroContentIndent);
		}
		frame.virtualActive = false;
		frame.virtualVisible = true;
	}

	bool VirtualContentVisible(const PresentationState& a_state)
	{
		return a_state.groupStack.empty() || !a_state.groupStack.back().virtualActive ||
			a_state.groupStack.back().virtualVisible;
	}

	bool BeginGroup(PresentationState& a_state, const ModSettings::GroupVisit& a_group)
	{
		CloseVirtualGroup(a_state);
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
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text,
						CurrentTheme() == FrontendTheme::SteelGold ? ThemeColor(ImGuiMCP::ImGuiCol_Text) : Palette().primaryText);
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Header, Palette().majorSurface);
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderHovered, Palette().majorHover);
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderActive, Palette().majorActive);
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Border, Palette().border);
				}
				if (g_hasStyleVars) {
					ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FramePadding, ImGuiMCP::ImVec2{ 8.0f, 5.0f });
					ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameBorderSize, 1.0f);
				}
				showChildren = ImGuiMCP::CollapsingHeader(label.c_str());
				DrawHeaderIcon(IconForGroup(stats), Palette().structuralAccent);
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
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, Palette().subsectionAccent);
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Header, Palette().subsectionSurface);
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderHovered, Palette().subsectionHover);
					ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_HeaderActive, Palette().majorHover);
					if (g_hasStyleVars) {
						ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FramePadding, ImGuiMCP::ImVec2{ 4.0f, 3.0f });
					}
					showChildren = ImGuiMCP::CollapsingHeader(label.c_str());
					DrawHeaderIcon(IconForGroup(stats), Palette().subsectionAccent);
					if (g_hasStyleVars) {
						ImGuiMCP::PopStyleVar();
					}
					ImGuiMCP::PopStyleColor(4);
					frame.interactiveSubsection = true;
					++a_state.interactiveDepth;
				} else {
					if (g_hasFontAwesome) {
						DrawIcon(IconForGroup(stats), Palette().subsectionAccent);
						ImGuiMCP::SameLine();
					}
					ImGuiMCP::TextColored(Palette().subsectionAccent, "%s", UpperAscii(TextOrEmpty(a_group.label)).c_str());
					if (g_hasThemeStyles) {
						ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Separator, WithAlpha(Palette().subsectionAccent, 0.46f));
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
					showChildren = DrawMicroDisclosure(a_group.label, stats);
				} else {
					if (g_hasFontAwesome) {
						DrawIcon(IconForGroup(stats), WithAlpha(Palette().microAccent, 0.85f));
						ImGuiMCP::SameLine();
					}
					ImGuiMCP::TextColored(Palette().microAccent, "%s", UpperAscii(TextOrEmpty(a_group.label)).c_str());
				}
				if (!collapsible) {
					if (g_hasThemeStyles) {
						ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Separator, WithAlpha(Palette().subsectionAccent, 0.25f));
					}
					ImGuiMCP::Separator();
					if (g_hasThemeStyles) {
						ImGuiMCP::PopStyleColor();
					}
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
		CloseVirtualGroup(a_state);
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

	void DrawPresentationText(PresentationState& a_state, const ModSettings::TextVisit& a_text)
	{
		const auto found = a_state.virtualStats.find(a_text.identity);
		if (found == a_state.virtualStats.end()) {
			if (VirtualContentVisible(a_state)) {
				DrawText(a_text);
			}
			return;
		}
		CloseVirtualGroup(a_state);
		if (a_state.headingDepth != 2 || !g_hasThemeStyles || !ShouldCollapseVirtual(found->second)) {
			DrawText(a_text);
			return;
		}
		ImGuiMCP::PushID(a_text.identity);
		const bool open = DrawMicroDisclosure(a_text.label, found->second);
		ImGuiMCP::PopID();
		auto& frame = a_state.groupStack.back();
		frame.virtualActive = true;
		frame.virtualVisible = open;
		if (open) {
			ImGuiMCP::Indent(kMicroContentIndent);
		}
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

	ModSettings::KeymapAction DrawKeymap(const ModSettings::KeymapVisit& a_keymap, const void* a_pageIdentity,
		SettingsPresentation::InputDeviceClass a_parentContext)
	{
		ModSettings::KeymapAction action = ModSettings::KeymapAction::None;
		const RowLayout row = BeginRow(a_keymap.identity, a_keymap.label, VisualKind::Keymap,
			IconForKeymap(SettingsPresentation::ResolveKeymapDevice(a_keymap, a_parentContext)), kKeymapTableWidth);
		if (!a_keymap.enabled) {
			ImGuiMCP::BeginDisabled();
		}
		const int styleColors = PushControlStyle(VisualKind::Keymap);
		bool controlHelp = false;
		const float actionWidth = 96.0f;
		const float unmapWidth = 84.0f;
		const float controlWidth = ImGuiMCP::GetContentRegionAvail().x;
		const auto drawBinding = [&] {
			if (!a_keymap.capturing && g_cancelTarget.keymap == a_keymap.identity) {
				g_cancelTarget = {};
			}
			const auto color = a_keymap.capturing ? Palette().interactionActive :
				a_keymap.mapped ? (CurrentTheme() == FrontendTheme::SteelGold ?
					Palette().structuralAccent : Palette().subsectionAccent) : Palette().secondaryText;
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, color);
			ImGuiMCP::TextWrapped("%s", a_keymap.capturing ? "Press a key..." : TextOrEmpty(a_keymap.bindingLabel));
			ImGuiMCP::PopStyleColor();
			if (!a_keymap.capturing && !g_hasInputBridge) {
				SecondaryText("Capture unavailable");
			}
		};
		const auto drawAction = [&](bool a_fixedWidth) {
			const ImGuiMCP::ImVec2 buttonSize{ a_fixedWidth ? actionWidth - 12.0f : 0.0f, 0.0f };
			if (a_keymap.capturing) {
				if (ImGuiMCP::Button("Cancel", buttonSize)) {
					action = ModSettings::KeymapAction::CancelCapture;
				}
				g_cancelTarget.page = a_pageIdentity;
				g_cancelTarget.keymap = a_keymap.identity;
				g_cancelTarget.hovered = ImGuiMCP::IsItemHovered();
				g_cancelTarget.renderedThisPage = true;
				g_cancelTarget.renderedFrame = g_hasFrameCount ? ImGuiMCP::GetFrameCount() : -1;
				if (action == ModSettings::KeymapAction::CancelCapture) {
					if (g_cancelTarget.passingMouseClick) {
						ModSettings::ObserveKeymapCaptureInput(kMouseLeftInput, false);
					}
					g_cancelTarget = {};
				}
				controlHelp = ItemRequestsHelp();
			} else {
				if (!g_hasInputBridge) {
					ImGuiMCP::BeginDisabled();
				}
				if (ImGuiMCP::Button("Remap", buttonSize)) {
					action = ModSettings::KeymapAction::BeginCapture;
				}
				controlHelp = ItemRequestsHelp();
				if (!g_hasInputBridge) {
					ImGuiMCP::EndDisabled();
				}
			}
		};
		const auto drawUnmap = [&](bool a_fixedWidth) {
			if (!a_keymap.capturing && a_keymap.mapped) {
				const ImGuiMCP::ImVec2 buttonSize{ a_fixedWidth ? unmapWidth - 12.0f : 0.0f, 0.0f };
				if (ImGuiMCP::Button("Unmap", buttonSize)) {
					action = ModSettings::KeymapAction::Unmap;
				}
				controlHelp |= ItemRequestsHelp();
			}
		};
		if (controlWidth >= actionWidth + unmapWidth + 120.0f &&
			ImGuiMCP::BeginTable("##keymap_controls", 3,
				ImGuiMCP::ImGuiTableFlags_NoSavedSettings | ImGuiMCP::ImGuiTableFlags_SizingStretchProp)) {
			ImGuiMCP::TableSetupColumn("Binding", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGuiMCP::TableSetupColumn("Action", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, actionWidth);
			ImGuiMCP::TableSetupColumn("Unmap", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, unmapWidth);
			ImGuiMCP::TableNextRow(0, ImGuiMCP::GetFrameHeight());
			ImGuiMCP::TableSetColumnIndex(0);
			drawBinding();
			ImGuiMCP::TableSetColumnIndex(1);
			drawAction(true);
			ImGuiMCP::TableSetColumnIndex(2);
			drawUnmap(true);
			ImGuiMCP::EndTable();
		} else {
			drawBinding();
			drawAction(false);
			if (!a_keymap.capturing && a_keymap.mapped) {
				if (controlWidth >= actionWidth + unmapWidth + 12.0f) {
					ImGuiMCP::SameLine();
				}
				drawUnmap(false);
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
		const bool ownsCancelTarget = g_cancelTarget.page == a_pageIdentity;
		if (ownsCancelTarget) {
			g_cancelTarget.renderedThisPage = false;
		}
		const bool jetStyle = g_hasThemeStyles && CurrentTheme() == FrontendTheme::JetBlack;
		if (jetStyle) {
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, Palette().primaryText);
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Border, Palette().border);
			if (g_hasStyleVars) {
				ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameBorderSize, 1.0f);
			}
		}
		PresentationState presentation{};
		presentation.groupStats = CollectGroupStats(a_pageIdentity, a_pageName, presentation.pageDescription,
			presentation.virtualStats);
		presentation.pageName = a_pageName;
		if (g_hasFontAwesome) {
			DrawIcon(kPageIcon, Palette().structuralAccent);
			ImGuiMCP::SameLine();
		}
		const auto title = UpperAscii(a_pageName);
		ImGuiMCP::TextUnformatted(title.c_str());
		const int themeButtonColors = PushControlStyle(VisualKind::Button);
		const bool toggleTheme = ImGuiMCP::Button(CurrentTheme() == FrontendTheme::JetBlack ?
			"Theme: Jet Black##frontend_theme" : "Theme: Steel Gold##frontend_theme");
		if (themeButtonColors) {
			ImGuiMCP::PopStyleColor(themeButtonColors);
		}
		SecondaryText(presentation.pageDescription.data());
		ImGuiMCP::Spacing();
		if (g_hasThemeStyles) {
			ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Separator, Palette().border);
		}
		ImGuiMCP::Separator();
		if (g_hasThemeStyles) {
			ImGuiMCP::PopStyleColor();
		}
		ImGuiMCP::Spacing();

		ModSettings::PageSettingsCallbacks callbacks{};
		callbacks.beginGroup = [&](const ModSettings::GroupVisit& group) { return BeginGroup(presentation, group); };
		callbacks.endGroup = [&](const ModSettings::GroupVisit&) { EndGroup(presentation); };
		callbacks.checkbox = [&](const ModSettings::CheckboxVisit& visit) -> std::optional<bool> {
			return VirtualContentVisible(presentation) ? DrawCheckbox(visit) : std::nullopt;
		};
		callbacks.slider = [&](const ModSettings::SliderVisit& visit) {
			return VirtualContentVisible(presentation) ? DrawSlider(visit) : ModSettings::SliderUpdate{};
		};
		callbacks.dropdown = [&](const ModSettings::DropdownVisit& visit) -> std::optional<int> {
			return VirtualContentVisible(presentation) ? DrawDropdown(visit) : std::nullopt;
		};
		callbacks.textbox = [&](const ModSettings::TextboxVisit& visit) {
			return VirtualContentVisible(presentation) ? DrawTextbox(visit) : ModSettings::TextboxUpdate{};
		};
		callbacks.text = [&](const ModSettings::TextVisit& visit) { DrawPresentationText(presentation, visit); };
		callbacks.color = [&](const ModSettings::ColorVisit& visit) {
			return VirtualContentVisible(presentation) ? DrawColor(visit) : ModSettings::ColorUpdate{};
		};
		callbacks.keymap = [&](const ModSettings::KeymapVisit& visit) {
			return VirtualContentVisible(presentation) ? DrawKeymap(visit, a_pageIdentity,
				ParentInputContext(presentation)) : ModSettings::KeymapAction::None;
		};
		callbacks.button = [&](const ModSettings::ButtonVisit& visit) {
			return VirtualContentVisible(presentation) && DrawButton(visit);
		};

		if (ModSettings::VisitPageSettings(a_pageIdentity, callbacks)) {
			ModSettings::CommitIniDirtyPage(a_pageIdentity);
		}
		if (ownsCancelTarget && !g_cancelTarget.renderedThisPage) {
			g_cancelTarget = {};
		}
		if (jetStyle) {
			if (g_hasStyleVars) {
				ImGuiMCP::PopStyleVar();
			}
			ImGuiMCP::PopStyleColor(2);
		}
		if (toggleTheme) {
			Settings::SetSkseMenuFrameworkJetBlack(CurrentTheme() != FrontendTheme::JetBlack);
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

	bool __stdcall ObserveFrameworkInput(RE::InputEvent* a_event)
	{
		const auto* button = a_event ? a_event->AsButtonEvent() : nullptr;
		if (!button) {
			return false;
		}
		const auto inputCode = InputListener::ToInputCode(*button);
		if (!inputCode) {
			return false;
		}
		const bool capturing = ModSettings::IsExternalKeymapCaptureActive();
		if (!capturing || g_cancelTarget.keymap != ModSettings::keyMapListening ||
			(g_hasFrameCount && g_cancelTarget.renderedFrame >= 0 &&
				ImGuiMCP::GetFrameCount() > g_cancelTarget.renderedFrame + 1)) {
			g_cancelTarget = {};
		}
		const bool mouseLeft = button->GetDevice() == RE::INPUT_DEVICE::kMouse && button->GetIDCode() == 0;
		if (button->IsDown()) {
			ModSettings::ObserveKeymapCaptureInput(*inputCode, true);
			if (capturing && mouseLeft && g_cancelTarget.hovered) {
				g_cancelTarget.passingMouseClick = true;
				return false;
			}
			if (capturing) {
				ModSettings::submitInput(*inputCode);
				if (!ModSettings::IsExternalKeymapCaptureActive()) {
					g_cancelTarget = {};
				}
			}
		} else if (!button->IsPressed()) {
			ModSettings::ObserveKeymapCaptureInput(*inputCode, false);
			if (capturing && mouseLeft && g_cancelTarget.passingMouseClick) {
				g_cancelTarget.passingMouseClick = false;
				return false;
			}
		}
		return capturing;
	}

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
		g_hasFrameCount = ::GetProcAddress(module, "igGetFrameCount") != nullptr;

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

		const auto registerInput = reinterpret_cast<RegisterInputEventFunction>(
			::GetProcAddress(module, "RegisterInpoutEvent"));
		if (registerInput) {
			g_inputRegistrationId = registerInput(&ObserveFrameworkInput);
			g_hasInputBridge = g_inputRegistrationId >= 0;
		}
		logger::info("SKSE Menu Framework input bridge {}",
			g_hasInputBridge ? "registered" : "unavailable");

		SKSEMenuFramework::SetSection(Settings::frontend_group_name);
		for (std::size_t index = 0; index < pageCount; ++index) {
			g_pageIdentities[index] = pages[index].identity;
			g_pageNames[index] = pages[index].name;
			SKSEMenuFramework::AddSectionItem(g_pageNames[index], kRenderCallbacks[index]);
		}
		initialized = true;
		logger::info("SKSE Menu Framework frontend registered");
	}
}
