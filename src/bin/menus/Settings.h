#pragma once
#include "Translator.h"
class Settings
{
public:
	static inline float relative_window_size_h = 1.f;
	static inline float relative_window_size_v = 1.f;
	static inline float windowPos_x = 0.f;
	static inline float windowPos_y = 0.f;

	static inline bool lockWindowPos = false;
	static inline bool lockWindowSize = false;

	static inline float fontScale = 1.f;
	static inline bool sksemf_jet_black = false;
	static void SetSkseMenuFrameworkJetBlack(bool a_enabled);

	static inline bool enable_ime_support = false;
	static inline bool show_ime_composition_overlay = true;
	static inline bool ime_debug_log = false;

	// Hint media runtime controls (Data\SKSE\Plugins\dmenu\hint_media.ini [Hints]).
	static inline bool enable_hint_media = true;
	static inline uint32_t hint_media_cache_mb = 128;
	static inline uint32_t hint_media_max_decode_pixels = 2048 * 2048;
	static inline uint32_t hint_media_log_level = 0;
	static inline float hint_media_preview_scale = 1.0f;
	static inline bool hint_media_click_to_open = true;
	static inline float hint_media_click_hover_radius = 96.0f;
	static inline uint32_t hint_media_click_close_grace_ms = 180;
	static inline bool hint_media_click_follow_mouse = true;
	static inline float hint_media_click_follow_offset_x = 16.0f;
	static inline float hint_media_click_follow_offset_y = 18.0f;
	static inline uint32_t hint_media_webm_max_buffered_frames = 120;
	static inline uint32_t hint_media_webm_max_clip_seconds = 30;
	static inline uint32_t hint_media_webm_decode_budget_ms = 2;
	static inline uint32_t hint_media_webm_decode_max_frames_per_tick = 1;

	static inline uint32_t key_toggle_dmenu_mkb = 199;
	static inline uint32_t key_toggle_modifier_mkb = 0;  // 0 = no modifier required
	static inline uint32_t key_toggle_dmenu_gamepad = 0;
	static inline uint32_t key_toggle_modifier_gamepad = 0;  // 0 = no modifier required
	static inline uint32_t key_toggle_hints_gamepad = 279;  // Y

	static void show();

	static void init();

	// Submit input id for key capture (keyboard/mouse/gamepad, called from InputListener)
	static void submitKeyCapture(uint32_t inputCode);

	static bool IsCapturingInput();
	static void RequestPrimaryActionFocus();

private:
	static inline bool request_primary_action_focus = false;
};
