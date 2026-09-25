#include "PCH.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "FlickIntegration.h"
#include "SettingsPresentation.h"
#include "../InputListener.h"
#include "../Utils.h"
#include "../menus/ModSettings.h"

// API 4 uses ImFont::LegacySize; dMenu's pinned ImGui names it FontSize.
// Win32 min/max macros also collide with API 4's std::max helpers.
// Keep these compatibility aliases local so the vendored header remains exact.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#define LegacySize FontSize
#include "include/lib/FUCK_API.h"
#undef LegacySize
#pragma pop_macro("max")
#pragma pop_macro("min")

namespace
{
	using SettingsPresentation::GroupStats;
	using SettingsPresentation::SemanticRole;
	using SettingsPresentation::VisualKind;
	using SettingsPresentation::TextOrEmpty;
	using SettingsPresentation::UpperAscii;
	using SettingsPresentation::SemanticForId;
	using SettingsPresentation::RepeatsPageName;
	using SettingsPresentation::CollectGroupStats;

	constexpr std::uint32_t kMouseLeftInput = 256;
	constexpr float kWideRowWidth = 560.0f;
	constexpr float kMajorIndent = 14.0f;
	constexpr float kSubsectionIndent = 12.0f;
	constexpr float kMicroIndent = 9.0f;

	struct Palette
	{
		ImVec4 major, majorHover, majorActive;
		ImVec4 control, controlHover, border;
		ImVec4 primary, secondary;
		ImVec4 gold, goldBright, interaction, interactionActive;
	};
	constexpr Palette kVanilla{
		{ 0.063f, 0.067f, 0.075f, 0.96f }, { 0.110f, 0.114f, 0.125f, 1.0f }, { 0.145f, 0.149f, 0.165f, 1.0f },
		{ 0.067f, 0.071f, 0.078f, 0.96f }, { 0.118f, 0.122f, 0.133f, 1.0f }, { 0.224f, 0.220f, 0.227f, 0.90f },
		{ 0.898f, 0.882f, 0.847f, 1.0f }, { 0.600f, 0.584f, 0.553f, 1.0f },
		{ 0.722f, 0.604f, 0.380f, 1.0f }, { 0.788f, 0.678f, 0.451f, 1.0f },
		{ 0.733f, 0.718f, 0.675f, 1.0f }, { 0.824f, 0.741f, 0.580f, 1.0f }
	};

	// Codepoints verified in the pinned FLICK Font Awesome 6 header.
	constexpr unsigned int kGearIcon = 0xf013;
	constexpr unsigned int kLayerIcon = 0xf5fd;
	constexpr unsigned int kKeyboardIcon = 0xf11c;
	constexpr unsigned int kGamepadIcon = 0xf11b;
	constexpr unsigned int kMouseIcon = 0xf8cc;
	constexpr unsigned int kKeyIcon = 0xf084;
	constexpr unsigned int kColorIcon = 0xf53f;
	constexpr unsigned int kTimingIcon = 0xf017;
	constexpr unsigned int kSoundIcon = 0xf028;
	constexpr unsigned int kPositionIcon = 0xf047;
	constexpr unsigned int kSizeIcon = 0xf065;
	constexpr unsigned int kFilterIcon = 0xf0b0;
	constexpr unsigned int kSlidersIcon = 0xf1de;

	std::string Glyph(unsigned int a_codepoint)
	{
		if (!a_codepoint) return {};
		if (a_codepoint < 0x800) {
			return { static_cast<char>(0xc0 | (a_codepoint >> 6)), static_cast<char>(0x80 | (a_codepoint & 0x3f)) };
		}
		return { static_cast<char>(0xe0 | (a_codepoint >> 12)),
			static_cast<char>(0x80 | ((a_codepoint >> 6) & 0x3f)), static_cast<char>(0x80 | (a_codepoint & 0x3f)) };
	}

	unsigned int IconForRole(SemanticRole a_role)
	{
		switch (a_role) {
		case SemanticRole::Gamepad: return kGamepadIcon;
		case SemanticRole::Keyboard: return kKeyboardIcon;
		case SemanticRole::Pointer: return kMouseIcon;
		case SemanticRole::Position: return kPositionIcon;
		case SemanticRole::Size: return kSizeIcon;
		case SemanticRole::Sound: return kSoundIcon;
		case SemanticRole::Timing: return kTimingIcon;
		case SemanticRole::Image:
		case SemanticRole::Color: return kColorIcon;
		case SemanticRole::Indicator: return kFilterIcon;
		default: return 0;
		}
	}

	unsigned int IconForKeymap(std::string_view a_semanticId)
	{
		const auto id = UpperAscii(a_semanticId);
		const auto has = [&](std::string_view a_term) { return id.find(a_term) != std::string::npos; };
		const bool gamepad = has("GAMEPAD") || has("CONTROLLER") || has("DPAD");
		const bool mouse = has("MOUSE");
		const bool keyboard = has("KEYBOARD");
		if (static_cast<int>(gamepad) + static_cast<int>(mouse) + static_cast<int>(keyboard) != 1) return kKeyIcon;
		return gamepad ? kGamepadIcon : mouse ? kMouseIcon : kKeyboardIcon;
	}

	unsigned int IconForGroup(const GroupStats& a_stats, bool a_fallback = false)
	{
		std::size_t best = 0;
		unsigned int icon = 0;
		for (std::size_t index = 1; index < static_cast<std::size_t>(SemanticRole::Count); ++index) {
			if (a_stats.semantics[index] > best) {
				best = a_stats.semantics[index];
				icon = IconForRole(static_cast<SemanticRole>(index));
			}
		}
		if (best >= 2 && icon) return icon;
		if (a_stats.kinds[static_cast<std::size_t>(VisualKind::Keymap)] >= 2) return kKeyboardIcon;
		if (a_stats.kinds[static_cast<std::size_t>(VisualKind::Color)] >= 2) return kColorIcon;
		if (a_stats.kinds[static_cast<std::size_t>(VisualKind::Slider)] >= 3) return kSlidersIcon;
		return a_fallback ? (a_stats.directGroups > 1 ? kLayerIcon : kGearIcon) : 0;
	}

	void PushPageStyle()
	{
		FUCK::PushStyleColor(ImGuiCol_Text, kVanilla.primary);
		FUCK::PushStyleColor(ImGuiCol_Border, kVanilla.border);
		FUCK::PushStyleColor(ImGuiCol_Separator, kVanilla.border);
		FUCK::PushStyleColor(ImGuiCol_FrameBg, kVanilla.control);
		FUCK::PushStyleColor(ImGuiCol_FrameBgHovered, kVanilla.controlHover);
		FUCK::PushStyleColor(ImGuiCol_FrameBgActive, kVanilla.majorHover);
		FUCK::PushStyleColor(ImGuiCol_Button, kVanilla.control);
		FUCK::PushStyleColor(ImGuiCol_ButtonHovered, kVanilla.majorHover);
		FUCK::PushStyleColor(ImGuiCol_ButtonActive, kVanilla.majorActive);
		FUCK::PushStyleColor(ImGuiCol_CheckMark, kVanilla.interactionActive);
		FUCK::PushStyleColor(ImGuiCol_SliderGrab, kVanilla.interaction);
		FUCK::PushStyleColor(ImGuiCol_SliderGrabActive, kVanilla.interactionActive);
		FUCK::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
	}

	void PopPageStyle()
	{
		FUCK::PopStyleVar();
		FUCK::PopStyleColor(12);
	}

	void SecondaryText(const char* a_text)
	{
		if (!a_text || !*a_text) return;
		FUCK::PushStyleColor(ImGuiCol_Text, kVanilla.secondary);
		FUCK::TextWrapped("%s", a_text);
		FUCK::PopStyleColor();
	}

	void Help(const char* a_description)
	{
		if (!a_description || !*a_description) return;
		FUCK::PushStyleColor(ImGuiCol_Text, kVanilla.secondary);
		FUCK::HelpMarker(a_description);
		FUCK::PopStyleColor();
	}

	void PushRelativeFont(float a_baseFontSize, float a_scale)
	{
		FUCK::PushFont(FUCK::GetFont(FUCK::Font::kRegular), a_baseFontSize * a_scale);
	}

	void Heading(std::string_view a_label, float a_baseFontSize, float a_scale, ImVec4 a_color,
		unsigned int a_icon = 0)
	{
		PushRelativeFont(a_baseFontSize, a_scale);
		if (a_icon) {
			const auto icon = Glyph(a_icon);
			FUCK::TextColored(kVanilla.gold, "%s", icon.c_str());
			FUCK::SameLine();
		}
		FUCK::TextColored(a_color, "%s", UpperAscii(a_label).c_str());
		FUCK::PopFont();
	}

	struct Row { bool table = false; };

	Row BeginRow(const void* a_identity, const char* a_label, unsigned int a_icon = 0)
	{
		FUCK::PushID(a_identity);
		Row row{};
		if (FUCK::GetContentRegionAvail().x >= FUCK::UIScale(kWideRowWidth) &&
			FUCK::BeginTable("##setting_row", 3, FUCK::TableFlags::kNoSavedSettings | FUCK::TableFlags::kSizingStretchProp)) {
			row.table = true;
			FUCK::TableSetupColumn("Setting", FUCK::TableColumnFlags::kWidthStretch, 0.46f);
			FUCK::TableSetupColumn("Control", FUCK::TableColumnFlags::kWidthStretch, 0.49f);
			FUCK::TableSetupColumn("Help", FUCK::TableColumnFlags::kWidthFixed, FUCK::GetFrameHeight());
			FUCK::TableNextRow(0, FUCK::GetFrameHeight());
			FUCK::TableSetColumnIndex(0);
		}
		FUCK::AlignTextToFramePadding();
		if (a_icon) {
			const auto icon = Glyph(a_icon);
			FUCK::TextColored(kVanilla.gold, "%s", icon.c_str());
			FUCK::SameLine();
		}
		FUCK::TextWrapped("%s", TextOrEmpty(a_label));
		if (row.table) FUCK::TableSetColumnIndex(1);
		return row;
	}

	void SetControlWidth(const Row& a_row)
	{
		const float reserve = a_row.table ? FUCK::UIScale(8.0f) : FUCK::GetFrameHeight() * 2.3f;
		FUCK::SetNextItemWidth((std::max)(1.0f, FUCK::GetContentRegionAvail().x - reserve));
	}

	void EndRow(const Row& a_row, const char* a_description)
	{
		if (a_row.table) {
			FUCK::TableSetColumnIndex(2);
			Help(a_description);
			FUCK::EndTable();
		} else if (a_description && *a_description) {
			FUCK::SameLine();
			Help(a_description);
		}
		FUCK::PopID();
	}

	struct GroupFrame
	{
		const void* identity = nullptr;
		bool heading = false;
		bool major = false;
		bool subsection = false;
		bool interactiveSubsection = false;
		bool virtualActive = false;
		bool virtualVisible = true;
		float indent = 0.0f;
	};

	struct RenderState
	{
		std::unordered_map<const void*, GroupStats> groups;
		std::unordered_map<const void*, GroupStats> virtualGroups;
		std::vector<GroupFrame> stack;
		std::string_view pageName;
		std::string_view description;
		float baseFontSize = 0.0f;
		std::size_t depth = 0;
		std::size_t majorCount = 0;
		std::size_t subsectionCount = 0;
		std::size_t interactiveDepth = 0;
	};

	void CloseVirtual(RenderState& a_state)
	{
		if (a_state.stack.empty()) return;
		auto& frame = a_state.stack.back();
		if (frame.virtualActive && frame.virtualVisible) FUCK::Unindent(FUCK::UIScale(kMicroIndent));
		frame.virtualActive = false;
		frame.virtualVisible = true;
	}

	bool ContentVisible(const RenderState& a_state)
	{
		return a_state.stack.empty() || !a_state.stack.back().virtualActive || a_state.stack.back().virtualVisible;
	}

	bool Disclosure(const char* a_label, float a_baseFontSize, float a_scale, ImVec4 a_text,
		ImVec4 a_hover, unsigned int a_icon, bool a_defaultOpen)
	{
		std::string label = a_icon ? Glyph(a_icon) + "  " : std::string{};
		label += UpperAscii(TextOrEmpty(a_label));
		PushRelativeFont(a_baseFontSize, a_scale);
		FUCK::PushStyleColor(ImGuiCol_Text, a_text);
		FUCK::PushStyleColor(ImGuiCol_Header, a_scale >= 1.0f ? kVanilla.major : ImVec4{ 0, 0, 0, 0 });
		FUCK::PushStyleColor(ImGuiCol_HeaderHovered, a_hover);
		FUCK::PushStyleColor(ImGuiCol_HeaderActive, kVanilla.majorActive);
		const int flags = a_defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0;
		const bool open = FUCK::CollapsingHeader(label.c_str(), flags);
		FUCK::PopStyleColor(4);
		FUCK::PopFont();
		return open;
	}

	bool BeginGroup(RenderState& a_state, const ModSettings::GroupVisit& a_group)
	{
		CloseVirtual(a_state);
		FUCK::PushID(a_group.identity);
		const auto found = a_state.groups.find(a_group.identity);
		const GroupStats empty{};
		const auto& stats = found == a_state.groups.end() ? empty : found->second;
		const bool repeat = a_state.depth == 0 && RepeatsPageName(TextOrEmpty(a_group.label), a_state.pageName);
		const bool singleLeaf = a_state.depth == 0 && stats.descendantLeaves == 1 &&
			stats.directGroups == 0 && stats.soleLeafLabel == TextOrEmpty(a_group.label);
		GroupFrame frame{};
		frame.identity = a_group.identity;
		bool open = true;
		if (!repeat && stats.descendantLeaves != 0) {
			if (singleLeaf) {
				SecondaryText(a_group.description);
			} else if (a_state.depth == 0) {
				FUCK::Spacing();
				open = Disclosure(a_group.label, a_state.baseFontSize, 1.0f, kVanilla.goldBright,
					kVanilla.majorHover, IconForGroup(stats, true), a_state.majorCount++ == 0);
				FUCK::Separator();
				frame.heading = frame.major = true;
				if (open) frame.indent = kMajorIndent;
			} else if (a_state.depth == 1) {
				const bool collapsible = a_state.interactiveDepth == 0 && SettingsPresentation::ShouldCollapseSubsection(stats);
				if (collapsible) {
					open = Disclosure(a_group.label, a_state.baseFontSize, 0.94f, kVanilla.gold,
						kVanilla.controlHover, IconForGroup(stats), a_state.subsectionCount++ == 0);
					frame.interactiveSubsection = true;
					++a_state.interactiveDepth;
				} else {
					Heading(a_group.label, a_state.baseFontSize, 0.94f, kVanilla.gold, IconForGroup(stats));
				}
				FUCK::Separator();
				frame.heading = frame.subsection = true;
				if (open) frame.indent = kSubsectionIndent;
			} else {
				const bool collapsible = a_state.depth == 2 && SettingsPresentation::ShouldCollapseMicro(stats);
				const unsigned int icon = IconForGroup(stats);
				if (collapsible) {
					open = Disclosure(a_group.label, a_state.baseFontSize, 0.87f, kVanilla.secondary,
						kVanilla.controlHover, icon, false);
				} else {
					Heading(a_group.label, a_state.baseFontSize, 0.87f, kVanilla.secondary, icon);
				}
				frame.heading = true;
				if (open) frame.indent = kMicroIndent;
			}
			if (frame.heading) {
				++a_state.depth;
				if (open && frame.indent > 0) FUCK::Indent(FUCK::UIScale(frame.indent));
				if (open) SecondaryText(a_group.description);
			}
		}
		a_state.stack.push_back(frame);
		return open;
	}

	void EndGroup(RenderState& a_state)
	{
		CloseVirtual(a_state);
		const auto frame = a_state.stack.back();
		a_state.stack.pop_back();
		if (frame.heading) --a_state.depth;
		if (frame.interactiveSubsection) --a_state.interactiveDepth;
		if (frame.indent > 0) FUCK::Unindent(FUCK::UIScale(frame.indent));
		if (frame.major || frame.subsection) FUCK::Spacing();
		FUCK::PopID();
	}

	void DrawText(const ModSettings::TextVisit& a_text)
	{
		FUCK::PushID(a_text.identity);
		const std::string_view value = TextOrEmpty(a_text.label);
		if ((!a_text.description || !*a_text.description) && !value.empty() &&
			value.find_first_not_of("-_= ") == std::string_view::npos) {
			FUCK::Separator();
			FUCK::PopID();
			return;
		}
		FUCK::Spacing();
		if (!a_text.enabled) FUCK::BeginDisabled();
		FUCK::TextColored({ a_text.color.red, a_text.color.green, a_text.color.blue, a_text.color.alpha },
			"%s", TextOrEmpty(a_text.label));
		if (!a_text.enabled) FUCK::EndDisabled();
		if (a_text.description && *a_text.description) {
			FUCK::SameLine();
			Help(a_text.description);
		}
		FUCK::PopID();
	}

	void DrawPresentationText(RenderState& a_state, const ModSettings::TextVisit& a_text)
	{
		const auto found = a_state.virtualGroups.find(a_text.identity);
		if (found == a_state.virtualGroups.end()) {
			if (ContentVisible(a_state)) DrawText(a_text);
			return;
		}
		CloseVirtual(a_state);
		if (a_state.depth < 1 || !SettingsPresentation::ShouldCollapseVirtual(found->second)) {
			DrawText(a_text);
			return;
		}
		FUCK::PushID(a_text.identity);
		const bool open = Disclosure(a_text.label, a_state.baseFontSize, 0.87f, kVanilla.secondary,
			kVanilla.controlHover, IconForGroup(found->second), false);
		FUCK::PopID();
		auto& frame = a_state.stack.back();
		frame.virtualActive = true;
		frame.virtualVisible = open;
		if (open) FUCK::Indent(FUCK::UIScale(kMicroIndent));
	}

	class DMenuPageTool;
	DMenuPageTool* g_activeTool = nullptr;

	class DMenuPageTool final : public FUCK::ITool
	{
	public:
		DMenuPageTool(const void* a_page, std::string a_sourceName, std::string a_name) :
			page_(a_page), sourceName_(std::move(a_sourceName)), name_(std::move(a_name)) {}
		const char* Name() const override { return name_.c_str(); }
		const char* Group() const override { return "dMenu"; }
		void OnOpen() override { open_ = true; g_activeTool = this; }
		void OnClose() override;
		bool OnAsyncInput(const void* a_event) override;
		void Draw() override;
		bool IsCancelMouseTarget() const
		{
			return open_ && cancelKeymap_ && cancelKeymap_ == captureKeymap_ &&
				ModSettings::IsExternalKeymapCaptureActive() && ModSettings::keyMapListening == captureKeymap_ &&
				(cancelHovered_ || cancelMouseClick_);
		}

	private:
		ModSettings::KeymapAction DrawKeymap(const ModSettings::KeymapVisit& a_keymap);
		const void* page_;
		std::string sourceName_;
		std::string name_;
		const void* captureKeymap_ = nullptr;
		const void* cancelKeymap_ = nullptr;
		bool open_ = false;
		bool cancelHovered_ = false;
		bool cancelMouseClick_ = false;
		bool cancelRendered_ = false;
	};

	void DMenuPageTool::OnClose()
	{
		open_ = false;
		if (g_activeTool == this) g_activeTool = nullptr;
		ModSettings::CancelExternalKeymapCapture(captureKeymap_);
		captureKeymap_ = cancelKeymap_ = nullptr;
		cancelHovered_ = cancelMouseClick_ = false;
	}

	bool DMenuPageTool::OnAsyncInput(const void* a_event)
	{
		if (!open_) return false;
		const auto* const* events = static_cast<const RE::InputEvent* const*>(a_event);
		bool consumed = false;
		for (auto* event = events ? *events : nullptr; event; event = event->next) {
			const auto* button = event->AsButtonEvent();
			if (!button) continue;
			const auto input = InputListener::ToInputCode(*button);
			if (!input) continue;
			const bool capture = captureKeymap_ && ModSettings::IsExternalKeymapCaptureActive() &&
				ModSettings::keyMapListening == captureKeymap_;
			const bool left = button->GetDevice() == RE::INPUT_DEVICE::kMouse && button->GetIDCode() == 0;
			if (button->IsDown()) {
				ModSettings::ObserveKeymapCaptureInput(*input, true);
				if (!capture) continue;
				if (left && cancelKeymap_ == captureKeymap_ && (cancelHovered_ || cancelMouseClick_)) {
					cancelMouseClick_ = true;
					consumed = true;
					continue;
				}
				ModSettings::submitInput(*input);
				consumed = true;
				if (!ModSettings::IsExternalKeymapCaptureActive()) {
					captureKeymap_ = cancelKeymap_ = nullptr;
					cancelHovered_ = cancelMouseClick_ = false;
				}
			} else if (!button->IsPressed()) {
				ModSettings::ObserveKeymapCaptureInput(*input, false);
				if (capture && left && cancelMouseClick_) {
					cancelMouseClick_ = false;
					consumed = true;
					continue;
				}
				consumed |= capture;
			}
		}
		return consumed;
	}

	ModSettings::KeymapAction DMenuPageTool::DrawKeymap(const ModSettings::KeymapVisit& a_keymap)
	{
		const Row row = BeginRow(a_keymap.identity, a_keymap.label, IconForKeymap(a_keymap.semanticId));
		if (!a_keymap.enabled) FUCK::BeginDisabled();
		ModSettings::KeymapAction action = ModSettings::KeymapAction::None;
		if (a_keymap.capturing) captureKeymap_ = a_keymap.identity;
		const auto drawBinding = [&] {
			const std::string label = a_keymap.capturing ? "Press a key..." :
				a_keymap.mapped ? UpperAscii(TextOrEmpty(a_keymap.bindingLabel)) : TextOrEmpty(a_keymap.bindingLabel);
			FUCK::PushStyleColor(ImGuiCol_Text, a_keymap.mapped || a_keymap.capturing ? kVanilla.goldBright : kVanilla.secondary);
			FUCK::TextWrapped("%s", label.c_str());
			FUCK::PopStyleColor();
		};
		const auto drawAction = [&] {
			if (a_keymap.capturing) {
				if (FUCK::Button("Cancel")) action = ModSettings::KeymapAction::CancelCapture;
				cancelKeymap_ = a_keymap.identity;
				cancelHovered_ = FUCK::IsItemHovered();
				cancelRendered_ = true;
			} else if (FUCK::Button("Remap")) {
				captureKeymap_ = a_keymap.identity;
				action = ModSettings::KeymapAction::BeginCapture;
			}
		};
		const auto drawClear = [&] {
			if (!a_keymap.capturing && a_keymap.mapped && FUCK::Button("Clear")) {
				action = ModSettings::KeymapAction::Unmap;
			}
		};
		const float actionWidth = (std::max)(FUCK::CalcTextSize("Remap").x,
			FUCK::CalcTextSize("Cancel").x) + FUCK::UIScale(20.0f);
		const float clearWidth = FUCK::CalcTextSize("Clear").x + FUCK::UIScale(20.0f);
		const float tableMinWidth = actionWidth + clearWidth + FUCK::UIScale(112.0f);
		if (FUCK::GetContentRegionAvail().x >= tableMinWidth &&
			FUCK::BeginTable("##keymap_controls", 3, FUCK::TableFlags::kNoSavedSettings | FUCK::TableFlags::kSizingStretchProp)) {
			FUCK::TableSetupColumn("Binding", FUCK::TableColumnFlags::kWidthStretch, 1.0f);
			FUCK::TableSetupColumn("Action", FUCK::TableColumnFlags::kWidthFixed, actionWidth);
			FUCK::TableSetupColumn("Clear", FUCK::TableColumnFlags::kWidthFixed, clearWidth);
			FUCK::TableNextRow(0, FUCK::GetFrameHeight());
			FUCK::TableSetColumnIndex(0);
			drawBinding();
			FUCK::TableSetColumnIndex(1);
			drawAction();
			FUCK::TableSetColumnIndex(2);
			drawClear();
			FUCK::EndTable();
		} else {
			drawBinding();
			drawAction();
			if (!a_keymap.capturing && a_keymap.mapped) {
				FUCK::SameLine();
				drawClear();
			}
		}
		if (action == ModSettings::KeymapAction::CancelCapture) {
			if (cancelMouseClick_) ModSettings::ObserveKeymapCaptureInput(kMouseLeftInput, false);
			captureKeymap_ = cancelKeymap_ = nullptr;
			cancelHovered_ = cancelMouseClick_ = false;
		}
		if (!a_keymap.enabled) FUCK::EndDisabled();
		EndRow(row, a_keymap.description);
		return action;
	}

	void DMenuPageTool::Draw()
	{
		cancelRendered_ = false;
		PushPageStyle();
		RenderState state{};
		state.baseFontSize = FUCK::GetTextLineHeight();
		state.pageName = sourceName_;
		state.groups = CollectGroupStats(page_, sourceName_, state.description, state.virtualGroups);
		Heading(name_, state.baseFontSize, 1.10f, kVanilla.primary, kGearIcon);
		SecondaryText(state.description.data());
		FUCK::Separator();
		FUCK::Spacing();

		ModSettings::PageSettingsCallbacks callbacks{};
		callbacks.beginGroup = [&](const ModSettings::GroupVisit& group) { return BeginGroup(state, group); };
		callbacks.endGroup = [&](const ModSettings::GroupVisit&) { EndGroup(state); };
		callbacks.checkbox = [&](const ModSettings::CheckboxVisit& visit) -> std::optional<bool> {
			if (!ContentVisible(state)) return std::nullopt;
			bool value = visit.value;
			const Row row = BeginRow(visit.identity, visit.label);
			if (!visit.enabled) FUCK::BeginDisabled();
			const bool changed = FUCK::Checkbox("##value", &value);
			if (!visit.enabled) FUCK::EndDisabled();
			EndRow(row, visit.description);
			return changed ? std::optional<bool>{ value } : std::nullopt;
		};
		callbacks.slider = [&](const ModSettings::SliderVisit& visit) {
			if (!ContentVisible(state)) return ModSettings::SliderUpdate{};
			const int count = Utils::SliderStepIndex(visit.max, visit.min, visit.step);
			const int initial = Utils::SliderStepIndex(visit.value, visit.min, visit.step);
			int index = initial;
			char display[64]{};
			std::snprintf(display, sizeof(display), "%g", visit.value);
			const Row row = BeginRow(visit.identity, visit.label, IconForRole(SemanticForId(visit.semanticId)));
			if (!visit.enabled) FUCK::BeginDisabled();
			SetControlWidth(row);
			FUCK::SliderInt("##value", &index, 0, count, display);
			const bool finished = FUCK::IsItemDeactivatedAfterEdit();
			if (!visit.enabled) FUCK::EndDisabled();
			EndRow(row, visit.description);
			return ModSettings::SliderUpdate{ index != initial ? std::optional<int>{ index } : std::nullopt, finished };
		};
		callbacks.dropdown = [&](const ModSettings::DropdownVisit& visit) -> std::optional<int> {
			if (!ContentVisible(state)) return std::nullopt;
			int selected = visit.selectedIndex;
			std::vector<const char*> options;
			options.reserve(visit.options.size());
			for (const auto& option : visit.options) options.push_back(option.c_str());
			const Row row = BeginRow(visit.identity, visit.label);
			if (!visit.enabled) FUCK::BeginDisabled();
			SetControlWidth(row);
			const bool changed = FUCK::Combo("##value", &selected, options.data(), static_cast<int>(options.size()));
			if (!visit.enabled) FUCK::EndDisabled();
			EndRow(row, visit.description);
			return changed ? std::optional<int>{ selected } : std::nullopt;
		};
		callbacks.textbox = [&](const ModSettings::TextboxVisit& visit) {
			if (!ContentVisible(state)) return ModSettings::TextboxUpdate{};
			// The API's std::string helper has a fixed 2048-byte buffer. Keep
			// existing longer values intact and leave room for this frame's input.
			std::vector<char> buffer(visit.value.size() + 2048, '\0');
			std::copy(visit.value.begin(), visit.value.end(), buffer.begin());
			const Row row = BeginRow(visit.identity, visit.label);
			if (!visit.enabled) FUCK::BeginDisabled();
			SetControlWidth(row);
			const bool changed = FUCK::InputText("##value", buffer.data(), buffer.size());
			const bool finished = FUCK::IsItemDeactivatedAfterEdit();
			if (!visit.enabled) FUCK::EndDisabled();
			EndRow(row, visit.description);
			return ModSettings::TextboxUpdate{
				changed ? std::optional<std::string>{ buffer.data() } : std::nullopt, finished };
		};
		callbacks.text = [&](const ModSettings::TextVisit& visit) { DrawPresentationText(state, visit); };
		callbacks.color = [&](const ModSettings::ColorVisit& visit) {
			if (!ContentVisible(state)) return ModSettings::ColorUpdate{};
			float color[4]{ visit.value.red, visit.value.green, visit.value.blue, visit.value.alpha };
			const Row row = BeginRow(visit.identity, visit.label, kColorIcon);
			if (!visit.enabled) FUCK::BeginDisabled();
			FUCK::SetNextItemWidth(FUCK::GetFrameHeight() * 1.85f);
			const bool changed = FUCK::ColorEdit4("##value", color,
				ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_DisplayRGB |
				ImGuiColorEditFlags_AlphaBar);
			const bool finished = FUCK::IsItemDeactivatedAfterEdit();
			if (!visit.enabled) FUCK::EndDisabled();
			EndRow(row, visit.description);
			return ModSettings::ColorUpdate{
				changed ? std::optional<ModSettings::Rgba>{ { color[0], color[1], color[2], color[3] } } : std::nullopt,
				finished };
		};
		callbacks.keymap = [&](const ModSettings::KeymapVisit& visit) {
			return ContentVisible(state) ? DrawKeymap(visit) : ModSettings::KeymapAction::None;
		};
		callbacks.button = [&](const ModSettings::ButtonVisit& visit) {
			if (!ContentVisible(state)) return false;
			FUCK::PushID(visit.identity);
			FUCK::Spacing();
			if (!visit.enabled) FUCK::BeginDisabled();
			const bool pressed = FUCK::Button(TextOrEmpty(visit.label));
			if (!visit.enabled) FUCK::EndDisabled();
			if (visit.description && *visit.description) {
				FUCK::SameLine();
				Help(visit.description);
			}
			FUCK::Spacing();
			FUCK::PopID();
			return pressed;
		};
		if (ModSettings::VisitPageSettings(page_, callbacks)) ModSettings::CommitIniDirtyPage(page_);
		if (!cancelRendered_ || !ModSettings::IsExternalKeymapCaptureActive() ||
			ModSettings::keyMapListening != captureKeymap_) {
			cancelHovered_ = cancelMouseClick_ = false;
			cancelKeymap_ = nullptr;
		}
		PopPageStyle();
	}
}

namespace FlickIntegration
{
	bool ShouldPassCancelMouseLeft()
	{
		return g_activeTool && g_activeTool->IsCancelMouseTarget();
	}

	void InitializeAfterPluginsLoaded()
	{
		static bool initialized = false;
		if (initialized) return;
		initialized = true;
		if (!FUCK::Connect(Plugin::NAME.data())) {
			logger::info("FLICK API 4 frontend unavailable");
			return;
		}

		static std::vector<std::unique_ptr<DMenuPageTool>> tools;
		std::unordered_set<std::string> names;
		ModSettings::ForEachLoadedPage([&](const ModSettings::PageVisit& page) {
			std::string base(page.name);
			if (base.empty()) base = "Unnamed Page";
			std::string display = base;
			for (unsigned int suffix = 2; names.contains(display); ++suffix) {
				display = base + " (" + std::to_string(suffix) + ")";
			}
			names.insert(display);
			auto tool = std::make_unique<DMenuPageTool>(page.identity, std::string(page.name), std::move(display));
			FUCK::RegisterTool(tool.get());
			tools.push_back(std::move(tool));
		});
		logger::info("FLICK API 4 frontend registered {} dMenu pages", tools.size());
	}
}
