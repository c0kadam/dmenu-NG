#include "imgui.h"
#include "imgui_internal.h"
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <array>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "Settings.h"
#include "ModSettings.h"
#include "bin/Utils.h"
#include "bin/ime/IMEManager.h"
#include "bin/HintMedia.h"
#include "bin/WheelerCooperativeOpening.h"
#include "Translator.h"

namespace
{
	static constexpr uint32_t kGamepadOffset = 266;
	static constexpr uint32_t kGamepadMax = 281;
	static constexpr uint32_t kUnsetCaptureValue = (std::numeric_limits<uint32_t>::max)();
	static constexpr uint32_t kMouseLeftInput = 256;
	static constexpr uint32_t kEnterInput = 28;
	static constexpr uint32_t kSpaceInput = 57;
	static constexpr uint32_t kNumEnterInput = 156;
	static constexpr uint32_t kGamepadAInput = 276;

	static uint32_t* s_pendingKeyCapture = nullptr;
	static std::set<uint32_t> s_captureIgnoredInputs;
	static int s_captureIgnoreUntilFrame = -1;

	bool IsGamepadInputCodeInternal(uint32_t inputCode)
	{
		return inputCode >= kGamepadOffset && inputCode <= kGamepadMax;
	}

	bool IsCaptureTarget(uint32_t* target)
	{
		return s_pendingKeyCapture == target;
	}

	void BeginCapture(uint32_t* target)
	{
		s_pendingKeyCapture = target;
		s_captureIgnoredInputs.clear();

		if (ImGuiContext* ctx = ImGui::GetCurrentContext()) {
			if (ctx->NavInputSource == ImGuiInputSource_Gamepad) {
				s_captureIgnoredInputs.insert(kGamepadAInput);
			} else if (ctx->NavInputSource == ImGuiInputSource_Keyboard) {
				s_captureIgnoredInputs.insert(kEnterInput);
				s_captureIgnoredInputs.insert(kNumEnterInput);
				s_captureIgnoredInputs.insert(kSpaceInput);
			}
		}

		if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			s_captureIgnoredInputs.insert(kMouseLeftInput);
		}
		if (ImGui::IsKeyDown(ImGuiKey_Enter) || ImGui::IsKeyReleased(ImGuiKey_Enter)) {
			s_captureIgnoredInputs.insert(kEnterInput);
		}
		if (ImGui::IsKeyDown(ImGuiKey_KeypadEnter) || ImGui::IsKeyReleased(ImGuiKey_KeypadEnter)) {
			s_captureIgnoredInputs.insert(kNumEnterInput);
		}
		if (ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyReleased(ImGuiKey_Space)) {
			s_captureIgnoredInputs.insert(kSpaceInput);
		}
		if (ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown) || ImGui::IsKeyReleased(ImGuiKey_GamepadFaceDown)) {
			s_captureIgnoredInputs.insert(kGamepadAInput);
		}

		s_captureIgnoreUntilFrame = ImGui::GetFrameCount() + 2;
	}

	void CancelCapture()
	{
		s_pendingKeyCapture = nullptr;
		s_captureIgnoredInputs.clear();
		s_captureIgnoreUntilFrame = -1;
	}

	void RefreshIgnoredCaptureInputs()
	{
		if (s_captureIgnoreUntilFrame >= 0 && ImGui::GetFrameCount() > s_captureIgnoreUntilFrame) {
			s_captureIgnoredInputs.clear();
			s_captureIgnoreUntilFrame = -1;
		}
	}
}

#define SETTINGFILE_PATH "Data\\SKSE\\Plugins\\dmenu\\dmenu.ini"
#define HINT_SETTINGFILE_PATH "Data\\SKSE\\Plugins\\dmenu\\hint_media.ini"

namespace ini
{
	void flush()
	{
		settingsLoader loader(SETTINGFILE_PATH);
		loader.setActiveSection("UI");
		loader.save(Settings::relative_window_size_h, "relative_window_size_h");
		loader.save(Settings::relative_window_size_v, "relative_window_size_v");
		loader.save(Settings::windowPos_x, "windowPos_x");
		loader.save(Settings::windowPos_y, "windowPos_y");
		loader.save(Settings::lockWindowSize, "lockWindowSize");
		loader.save(Settings::lockWindowPos, "lockWindowPos");
		loader.save(Settings::key_toggle_dmenu_mkb, "key_toggle_dmenu_mkb");
		loader.save(Settings::key_toggle_modifier_mkb, "key_toggle_modifier_mkb");
		loader.save(Settings::key_toggle_dmenu_gamepad, "key_toggle_dmenu_gamepad");
		loader.save(Settings::key_toggle_modifier_gamepad, "key_toggle_modifier_gamepad");
		loader.save(Settings::key_toggle_hints_gamepad, "key_toggle_hints_gamepad");
		// Backward compatibility for older versions that only read shared keys.
		loader.save(Settings::key_toggle_dmenu_mkb, "key_toggle_dmenu");
		loader.save(Settings::key_toggle_modifier_mkb, "key_toggle_modifier");
		loader.save(Settings::fontScale, "fontScale");
		loader.setActiveSection("IME");
		loader.save(Settings::enable_ime_support, "EnableIMESupport");
		loader.save(Settings::show_ime_composition_overlay, "ShowCompositionOverlay");
		loader.save(Settings::ime_debug_log, "DebugLog");
		loader.flush();

		settingsLoader hintLoader(HINT_SETTINGFILE_PATH);
		hintLoader.setActiveSection("Hints");
		hintLoader.save(Settings::enable_hint_media, "EnableHintMedia");
		hintLoader.save(Settings::hint_media_cache_mb, "HintMediaCacheMB");
		hintLoader.save(Settings::hint_media_max_decode_pixels, "HintMediaMaxDecodePixels");
		hintLoader.save(Settings::hint_media_log_level, "HintMediaLogLevel");
		hintLoader.save(Settings::hint_media_preview_scale, "HintMediaPreviewScale");
		hintLoader.save(Settings::hint_media_click_to_open, "HintMediaClickToOpen");
		hintLoader.save(Settings::hint_media_click_hover_radius, "HintMediaClickHoverRadius");
		hintLoader.save(Settings::hint_media_click_close_grace_ms, "HintMediaClickCloseGraceMs");
		hintLoader.save(Settings::hint_media_click_follow_mouse, "HintMediaClickFollowMouse");
		hintLoader.save(Settings::hint_media_click_follow_offset_x, "HintMediaClickFollowOffsetX");
		hintLoader.save(Settings::hint_media_click_follow_offset_y, "HintMediaClickFollowOffsetY");
		hintLoader.save(Settings::hint_media_webm_max_buffered_frames, "HintMediaWebmMaxBufferedFrames");
		hintLoader.save(Settings::hint_media_webm_max_clip_seconds, "HintMediaWebmMaxClipSeconds");
		hintLoader.save(Settings::hint_media_webm_decode_budget_ms, "HintMediaWebmDecodeBudgetMs");
		hintLoader.save(Settings::hint_media_webm_decode_max_frames_per_tick, "HintMediaWebmDecodeMaxFramesPerTick");
		hintLoader.flush();
	}

	void load()
	{
		settingsLoader loader(SETTINGFILE_PATH);
		loader.setActiveSection("UI");
		Settings::key_toggle_dmenu_mkb = 199;
		Settings::key_toggle_modifier_mkb = 0;
		Settings::key_toggle_dmenu_gamepad = 0;
		Settings::key_toggle_modifier_gamepad = 0;
		Settings::key_toggle_hints_gamepad = 279;

		loader.load(Settings::relative_window_size_h, "relative_window_size_h");
		loader.load(Settings::relative_window_size_v, "relative_window_size_v");
		loader.load(Settings::windowPos_x, "windowPos_x");
		loader.load(Settings::windowPos_y, "windowPos_y");
		loader.load(Settings::lockWindowSize, "lockWindowSize");
		loader.load(Settings::lockWindowPos, "lockWindowPos");
		loader.load(Settings::fontScale, "fontScale");

		uint32_t legacyToggle = kUnsetCaptureValue;
		uint32_t legacyModifier = kUnsetCaptureValue;
		loader.load(legacyToggle, "key_toggle_dmenu");
		loader.load(legacyModifier, "key_toggle_modifier");

		uint32_t mkbToggle = kUnsetCaptureValue;
		uint32_t mkbModifier = kUnsetCaptureValue;
		uint32_t gamepadToggle = kUnsetCaptureValue;
		uint32_t gamepadModifier = kUnsetCaptureValue;
		uint32_t gamepadHintToggle = kUnsetCaptureValue;
		loader.load(mkbToggle, "key_toggle_dmenu_mkb");
		loader.load(mkbModifier, "key_toggle_modifier_mkb");
		loader.load(gamepadToggle, "key_toggle_dmenu_gamepad");
		loader.load(gamepadModifier, "key_toggle_modifier_gamepad");
		loader.load(gamepadHintToggle, "key_toggle_hints_gamepad");

		if (mkbToggle != kUnsetCaptureValue) {
			Settings::key_toggle_dmenu_mkb = mkbToggle;
		}
		if (mkbModifier != kUnsetCaptureValue) {
			Settings::key_toggle_modifier_mkb = mkbModifier;
		}
		if (gamepadToggle != kUnsetCaptureValue) {
			Settings::key_toggle_dmenu_gamepad = gamepadToggle;
		}
		if (gamepadModifier != kUnsetCaptureValue) {
			Settings::key_toggle_modifier_gamepad = gamepadModifier;
		}
		if (gamepadHintToggle != kUnsetCaptureValue) {
			Settings::key_toggle_hints_gamepad = gamepadHintToggle;
		}

		// Legacy migration: if split keys were not present, infer target domain from input range.
		if (legacyToggle != kUnsetCaptureValue) {
			if (IsGamepadInputCodeInternal(legacyToggle)) {
				if (gamepadToggle == kUnsetCaptureValue) {
					Settings::key_toggle_dmenu_gamepad = legacyToggle;
				}
			} else if (mkbToggle == kUnsetCaptureValue) {
				Settings::key_toggle_dmenu_mkb = legacyToggle;
			}
		}
		if (legacyModifier != kUnsetCaptureValue) {
			if (IsGamepadInputCodeInternal(legacyModifier)) {
				if (gamepadModifier == kUnsetCaptureValue) {
					Settings::key_toggle_modifier_gamepad = legacyModifier;
				}
			} else if (mkbModifier == kUnsetCaptureValue) {
				Settings::key_toggle_modifier_mkb = legacyModifier;
			}
		}

		loader.setActiveSection("IME");
		loader.load(Settings::enable_ime_support, "EnableIMESupport");
		loader.load(Settings::show_ime_composition_overlay, "ShowCompositionOverlay");
		loader.load(Settings::ime_debug_log, "DebugLog");

		settingsLoader hintLoader(HINT_SETTINGFILE_PATH);
		hintLoader.setActiveSection("Hints");
		hintLoader.load(Settings::enable_hint_media, "EnableHintMedia");
		hintLoader.load(Settings::hint_media_cache_mb, "HintMediaCacheMB");
		hintLoader.load(Settings::hint_media_max_decode_pixels, "HintMediaMaxDecodePixels");
		hintLoader.load(Settings::hint_media_log_level, "HintMediaLogLevel");
		hintLoader.load(Settings::hint_media_preview_scale, "HintMediaPreviewScale");
		hintLoader.load(Settings::hint_media_click_to_open, "HintMediaClickToOpen");
		hintLoader.load(Settings::hint_media_click_hover_radius, "HintMediaClickHoverRadius");
		hintLoader.load(Settings::hint_media_click_close_grace_ms, "HintMediaClickCloseGraceMs");
		hintLoader.load(Settings::hint_media_click_follow_mouse, "HintMediaClickFollowMouse");
		hintLoader.load(Settings::hint_media_click_follow_offset_x, "HintMediaClickFollowOffsetX");
		hintLoader.load(Settings::hint_media_click_follow_offset_y, "HintMediaClickFollowOffsetY");
		hintLoader.load(Settings::hint_media_webm_max_buffered_frames, "HintMediaWebmMaxBufferedFrames");
		hintLoader.load(Settings::hint_media_webm_max_clip_seconds, "HintMediaWebmMaxClipSeconds");
		hintLoader.load(Settings::hint_media_webm_decode_budget_ms, "HintMediaWebmDecodeBudgetMs");
		hintLoader.load(Settings::hint_media_webm_decode_max_frames_per_tick, "HintMediaWebmDecodeMaxFramesPerTick");
	}

	void init()
	{
		load();
	}
}

namespace UI
{
	void init()
	{
	}

	void show()
	{
		RefreshIgnoredCaptureInputs();

		ImVec2 parentSize = ImGui::GetMainViewport()->Size;

		// Calculate the relative sizes
		Settings::relative_window_size_h = ImGui::GetWindowWidth() / parentSize.x;
		Settings::relative_window_size_v = ImGui::GetWindowHeight() / parentSize.y;

		auto windowPos = ImGui::GetWindowPos();
		Settings::windowPos_x = windowPos.x;
		Settings::windowPos_y = windowPos.y;

		// Display the relative sizes in real-time
		ImGui::Text("%s: %.2f%%", TR("settings_width_label", "Width"), Settings::relative_window_size_h * 100.0f);
		ImGui::SameLine();
		ImGui::Text("%s: %.2f%%", TR("settings_height_label", "Height"), Settings::relative_window_size_v * 100.0f);

		ImGui::Checkbox(TR("settings_lock_size", "Lock Size"), &Settings::lockWindowSize);

		// Display the relative positions in real-time
		ImGui::Text("%s: %f", TR("settings_pos_x", "X position"), Settings::windowPos_x);
		ImGui::SameLine();
		ImGui::Text("%s: %f", TR("settings_pos_y", "Y position"), Settings::windowPos_y);

		ImGui::Checkbox(TR("settings_lock_pos", "Lock Position"), &Settings::lockWindowPos);

		if (ImGui::SliderFloat(TR("settings_font_scale", "Font Scale"), &Settings::fontScale, 0.5f, 2.f)) {
			ImGui::GetIO().FontGlobalScale = Settings::fontScale;
		}

		ImGui::Separator();
		ImGui::Text("%s", TR("settings_hotkeys", "Hotkeys"));

		auto showRebindRow = [](const char* label, uint32_t* value, bool allowClear) {
			ImGui::PushID(value);
			const bool isCapturing = IsCaptureTarget(value);
			const bool capturingOther = s_pendingKeyCapture != nullptr && !isCapturing;

			ImGui::BeginDisabled(capturingOther);
			if (ImGui::Button(isCapturing ? TR("settings_press_key", "Press a key or button...") : TR("settings_rebind", "Rebind"))) {
				if (isCapturing) {
					CancelCapture();
				} else {
					BeginCapture(value);
				}
			}
			ImGui::EndDisabled();

			if (allowClear) {
				ImGui::SameLine();
				if (ImGui::Button(TR("settings_clear", "Clear"))) {
					*value = 0;
					if (value == &Settings::key_toggle_dmenu_mkb ||
					    value == &Settings::key_toggle_modifier_mkb ||
					    value == &Settings::key_toggle_dmenu_gamepad ||
					    value == &Settings::key_toggle_modifier_gamepad) {
						WheelerCooperativeOpening::PublishCurrentBindings();
					}
				}
			}

			ImGui::SameLine();
			ImGui::Text("%s: %s",
				label,
				*value ? ModSettings::setting_keymap::keyid_to_str(*value) : TR("settings_none", "None"));
			ImGui::PopID();
		};

		ImGui::Text("%s", TR("settings_hotkeys_mkb", "Keyboard/Mouse"));
		showRebindRow(TR("settings_toggle_key_mkb", "Toggle (MKB)"), &Settings::key_toggle_dmenu_mkb, true);
		showRebindRow(TR("settings_modifier_mkb", "Modifier (MKB)"), &Settings::key_toggle_modifier_mkb, true);

		ImGui::Separator();
		ImGui::Text("%s", TR("settings_hotkeys_gamepad", "Gamepad"));
		showRebindRow(TR("settings_toggle_key_gamepad", "Toggle (Gamepad)"), &Settings::key_toggle_dmenu_gamepad, true);
		showRebindRow(TR("settings_modifier_gamepad", "Modifier (Gamepad)"), &Settings::key_toggle_modifier_gamepad, true);
		showRebindRow(TR("settings_hint_toggle_gamepad", "Hints Show/Hide (Gamepad)"), &Settings::key_toggle_hints_gamepad, true);

		struct BindingDef
		{
			const char* label;
			uint32_t value;
		};

		const std::array<BindingDef, 5> bindingDefs = {
			BindingDef{ TR("settings_toggle_key_mkb", "Toggle (MKB)"), Settings::key_toggle_dmenu_mkb },
			BindingDef{ TR("settings_modifier_mkb", "Modifier (MKB)"), Settings::key_toggle_modifier_mkb },
			BindingDef{ TR("settings_toggle_key_gamepad", "Toggle (Gamepad)"), Settings::key_toggle_dmenu_gamepad },
			BindingDef{ TR("settings_modifier_gamepad", "Modifier (Gamepad)"), Settings::key_toggle_modifier_gamepad },
			BindingDef{ TR("settings_hint_toggle_gamepad", "Hints Show/Hide (Gamepad)"), Settings::key_toggle_hints_gamepad }
		};

		std::unordered_map<uint32_t, std::vector<const char*>> groupedByKey;
		for (const auto& binding : bindingDefs) {
			if (binding.value != 0) {
				groupedByKey[binding.value].push_back(binding.label);
			}
		}

		bool hasConflict = false;
		for (const auto& [key, labels] : groupedByKey) {
			if (labels.size() > 1) {
				hasConflict = true;
				break;
			}
		}

		if (hasConflict) {
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.30f, 1.0f));
			ImGui::TextUnformatted("Hotkey conflicts detected:");
			for (const auto& [key, labels] : groupedByKey) {
				if (labels.size() <= 1) {
					continue;
				}

				std::string joinedLabels;
				for (std::size_t i = 0; i < labels.size(); ++i) {
					if (i > 0) {
						joinedLabels += ", ";
					}
					joinedLabels += labels[i];
				}

				ImGui::BulletText(
					"%s -> %s",
					ModSettings::setting_keymap::keyid_to_str(static_cast<int>(key)),
					joinedLabels.c_str());
			}
			ImGui::PopStyleColor();
		}

		ImGui::Separator();
		ImGui::Text("%s", TR("settings_ime_section", "IME"));
		if (ImGui::Checkbox(TR("settings_enable_ime", "Enable IME Support"), &Settings::enable_ime_support)) {
			if (Settings::ime_debug_log) {
				INFO("IME: {}", Settings::enable_ime_support ? "enabled" : "disabled");
			}
			if (!Settings::enable_ime_support) {
				IME::Manager::Get().Reset("settings disabled");
			}
		}

		ImGui::BeginDisabled(!Settings::enable_ime_support);
		ImGui::Checkbox(
			TR("settings_ime_overlay", "Show IME Composition Overlay"),
			&Settings::show_ime_composition_overlay);
		ImGui::EndDisabled();
	}
}

void Settings::show()
{
	// Use consistent padding and alignment
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 5));

	// Indent the contents of the window
	ImGui::Indent();
	ImGui::Spacing();

	// Display size controls
	ImGui::Text("%s", TR("settings_ui_section", "UI"));
	UI::show();
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Unindent the contents of the window
	ImGui::Unindent();

	// Position the "Save" button at the bottom-right corner of the window
	ImGui::SameLine(ImGui::GetWindowWidth() - 100);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);

	// Set the button background and text colors
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

	const bool focusPrimaryAction = request_primary_action_focus;
	request_primary_action_focus = false;
	if (focusPrimaryAction) {
		ImGui::SetKeyboardFocusHere();
	}
	if (ImGui::Button(TR("settings_save_button", "Save"))) {
		ini::flush();
	}
	if (focusPrimaryAction) {
		ImGui::SetItemDefaultFocus();
	}

	// Restore the previous style colors
	ImGui::PopStyleColor(4);

	// Use consistent padding and alignment
	ImGui::PopStyleVar(2);
}

void Settings::init()
{
	ini::init();  // load all settings first
	UI::init();
	HintMediaManager::Get().SetEnabled(Settings::enable_hint_media);
	HintMediaManager::Get().SetCacheBudgetMB(Settings::hint_media_cache_mb);
	HintMediaManager::Get().SetMaxDecodePixels(Settings::hint_media_max_decode_pixels);
	HintMediaManager::Get().SetLogLevel(static_cast<int>(Settings::hint_media_log_level));
	HintMediaManager::Get().SetPreviewScale(Settings::hint_media_preview_scale);
	HintMediaManager::Get().SetWebmMaxBufferedFrames(Settings::hint_media_webm_max_buffered_frames);
	HintMediaManager::Get().SetWebmMaxClipSeconds(Settings::hint_media_webm_max_clip_seconds);
	HintMediaManager::Get().SetWebmDecodeBudgetMs(Settings::hint_media_webm_decode_budget_ms);
	HintMediaManager::Get().SetWebmDecodeMaxFramesPerTick(Settings::hint_media_webm_decode_max_frames_per_tick);

	INFO("Settings initialized.");
}

void Settings::submitKeyCapture(uint32_t inputCode)
{
	if (s_pendingKeyCapture == nullptr) {
		return;
	}

	if (s_captureIgnoredInputs.contains(inputCode)) {
		return;
	}

	auto* captureTarget = s_pendingKeyCapture;
	*captureTarget = inputCode;
	const bool cooperativeOpeningBindingChanged =
		captureTarget == &Settings::key_toggle_dmenu_mkb ||
		captureTarget == &Settings::key_toggle_modifier_mkb ||
		captureTarget == &Settings::key_toggle_dmenu_gamepad ||
		captureTarget == &Settings::key_toggle_modifier_gamepad;
	CancelCapture();
	if (cooperativeOpeningBindingChanged) {
		WheelerCooperativeOpening::PublishCurrentBindings();
	}
}

bool Settings::IsCapturingInput()
{
	return s_pendingKeyCapture != nullptr;
}

void Settings::RequestPrimaryActionFocus()
{
	request_primary_action_focus = true;
}
