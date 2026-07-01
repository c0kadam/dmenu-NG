#include "ScreenKeyboardBridge.h"

#include "imgui.h"
#include "imgui_internal.h"

#include "Renderer.h"
#include "ime/IMEWidgets.h"
#include "menus/Settings.h"
#include "menus/Translator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string_view>

namespace
{
	constexpr const char* kInternalKeyboardPopup = "##dmenu_internal_keyboard";
	constexpr float kPopupWidth = 660.0f;
	constexpr float kKeyWidth = 44.0f;
	constexpr float kWideKeyWidth = 86.0f;
	constexpr float kSpaceKeyWidth = 150.0f;

	struct Utf8Codepoint
	{
		char32_t codepoint;
		std::size_t startIndex;
	};

	std::optional<Utf8Codepoint> GetLastUtf8Codepoint(std::string_view text)
	{
		if (text.empty()) {
			return std::nullopt;
		}

		std::size_t start = text.size() - 1;
		while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) {
			--start;
		}

		const unsigned char lead = static_cast<unsigned char>(text[start]);
		std::size_t byteCount = 1;
		char32_t codepoint = 0;
		if ((lead & 0x80) == 0) {
			codepoint = lead;
		} else if ((lead & 0xE0) == 0xC0) {
			byteCount = 2;
			codepoint = lead & 0x1F;
		} else if ((lead & 0xF0) == 0xE0) {
			byteCount = 3;
			codepoint = lead & 0x0F;
		} else if ((lead & 0xF8) == 0xF0) {
			byteCount = 4;
			codepoint = lead & 0x07;
		} else {
			return std::nullopt;
		}

		if (start + byteCount > text.size()) {
			return std::nullopt;
		}

		for (std::size_t i = 1; i < byteCount; ++i) {
			const unsigned char continuation = static_cast<unsigned char>(text[start + i]);
			if ((continuation & 0xC0) != 0x80) {
				return std::nullopt;
			}
			codepoint = (codepoint << 6) | (continuation & 0x3F);
		}

		return Utf8Codepoint{ codepoint, start };
	}

	std::optional<char32_t> DecodeSingleUtf8Codepoint(std::string_view text)
	{
		const auto codepoint = GetLastUtf8Codepoint(text);
		if (!codepoint || codepoint->startIndex != 0) {
			return std::nullopt;
		}

		return codepoint->codepoint;
	}

	void AppendUtf8(std::string& text, char32_t codepoint)
	{
		if (codepoint <= 0x7F) {
			text.push_back(static_cast<char>(codepoint));
		} else if (codepoint <= 0x7FF) {
			text.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
			text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
		} else if (codepoint <= 0xFFFF) {
			text.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
			text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
			text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
		} else {
			text.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
			text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
			text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
			text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
		}
	}

	void PopLastUtf8Codepoint(std::string& text)
	{
		if (const auto last = GetLastUtf8Codepoint(text)) {
			text.erase(last->startIndex);
		}
	}

	void KeepOnlyDigits(std::string& text)
	{
		text.erase(
			std::remove_if(
				text.begin(),
				text.end(),
				[](unsigned char ch) { return !std::isdigit(ch); }),
			text.end());
	}

	ScreenKeyboardBridge::Layout CycleLayout(ScreenKeyboardBridge::Layout current, int direction)
	{
		constexpr std::array<ScreenKeyboardBridge::Layout, 4> kLayouts = {
			ScreenKeyboardBridge::Layout::Latin,
			ScreenKeyboardBridge::Layout::Hiragana,
			ScreenKeyboardBridge::Layout::Katakana,
			ScreenKeyboardBridge::Layout::Hangul
		};

		std::size_t currentIndex = 0;
		for (std::size_t i = 0; i < kLayouts.size(); ++i) {
			if (kLayouts[i] == current) {
				currentIndex = i;
				break;
			}
		}

		const int count = static_cast<int>(kLayouts.size());
		int nextIndex = static_cast<int>(currentIndex) + direction;
		while (nextIndex < 0) {
			nextIndex += count;
		}
		nextIndex %= count;
		return kLayouts[static_cast<std::size_t>(nextIndex)];
	}

	void CenterNextRow(float totalWidth)
	{
		const float availableWidth = ImGui::GetContentRegionAvail().x;
		if (availableWidth <= totalWidth) {
			return;
		}

		const float offset = (availableWidth - totalWidth) * 0.5f;
		if (offset > 0.0f) {
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
		}
	}

	template <class Fn>
	void DrawLatinKeyRow(std::string_view keys, bool uppercase, bool& focusFirst, Fn&& onPress)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float totalWidth = (kKeyWidth * static_cast<float>(keys.size())) +
			(style.ItemSpacing.x * static_cast<float>((std::max)(std::size_t{ 0 }, keys.size() - 1)));
		CenterNextRow(totalWidth);

		for (std::size_t i = 0; i < keys.size(); ++i) {
			const unsigned char key = static_cast<unsigned char>(keys[i]);
			const char display = uppercase ? static_cast<char>(std::toupper(key)) : static_cast<char>(key);
			char label[2]{ display, '\0' };
			if (ImGui::Button(label, ImVec2(kKeyWidth, 0.0f))) {
				onPress(display);
			}
			if (focusFirst) {
				ImGui::SetItemDefaultFocus();
				focusFirst = false;
			}
			if (i + 1 < keys.size()) {
				ImGui::SameLine();
			}
		}
	}

	template <class KeyT, std::size_t N, class Fn>
	void DrawKeyRow(const std::array<KeyT, N>& keys, bool& focusFirst, Fn&& onPress)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float totalWidth = (kKeyWidth * static_cast<float>(keys.size())) +
			(style.ItemSpacing.x * static_cast<float>((std::max)(std::size_t{ 0 }, keys.size() - 1)));
		CenterNextRow(totalWidth);

		for (std::size_t i = 0; i < keys.size(); ++i) {
			const char* label = reinterpret_cast<const char*>(keys[i]);
			if (ImGui::Button(label, ImVec2(kKeyWidth, 0.0f))) {
				onPress(label);
			}
			if (focusFirst) {
				ImGui::SetItemDefaultFocus();
				focusFirst = false;
			}
			if (i + 1 < keys.size()) {
				ImGui::SameLine();
			}
		}
	}
}

ScreenKeyboardBridge& ScreenKeyboardBridge::GetSingleton()
{
	static ScreenKeyboardBridge singleton;
	return singleton;
}

bool ScreenKeyboardBridge::IsAwaitingResult() const
{
	return awaitingResult_.load();
}

bool ScreenKeyboardBridge::RequestTextBox(const std::string& initialValue, ResultCallback callback, RequestOptions options)
{
	if (!callback || !Renderer::IsEnabled()) {
		return false;
	}

	std::lock_guard lock(stateLock_);
	if (awaitingResult_.load()) {
		return false;
	}

	pendingCallback_ = std::move(callback);
	internalKeyboardOpen_ = true;
	internalPopupRequested_ = true;
	openedWithGamepad_ = ImGui::GetCurrentContext() != nullptr &&
		ImGui::GetCurrentContext()->NavInputSource == ImGuiInputSource_Gamepad;
	internalFocusTextField_ = !openedWithGamepad_;
	internalFocusFirstKey_ = openedWithGamepad_;
	deferredDefaultFocusFrames_ = openedWithGamepad_ ? 1 : 0;
	uppercase_ = false;
	currentLayout_ = Layout::Latin;
	activeOptions_ = options;
	internalBuffer_ = initialValue;
	if (activeOptions_.digitsOnly) {
		KeepOnlyDigits(internalBuffer_);
	}
	awaitingResult_.store(true);
	INFO("ScreenKeyboard: opened internal keyboard");
	return true;
}

void ScreenKeyboardBridge::CancelActiveRequest()
{
	std::lock_guard lock(stateLock_);
	if (!awaitingResult_.load()) {
		return;
	}

	pendingCallback_ = nullptr;
	internalKeyboardOpen_ = false;
	internalPopupRequested_ = false;
	internalFocusTextField_ = false;
	internalFocusFirstKey_ = false;
	openedWithGamepad_ = false;
	deferredDefaultFocusFrames_ = 0;
	uppercase_ = false;
	currentLayout_ = Layout::Latin;
	activeOptions_ = {};
	internalBuffer_.clear();
	awaitingResult_.store(false);
	INFO("ScreenKeyboard: canceled active keyboard request");
}

void ScreenKeyboardBridge::Draw()
{
	static constexpr std::array<const char8_t*, 5> kHiraganaRow1{ u8"\u3042", u8"\u3044", u8"\u3046", u8"\u3048", u8"\u304A" };
	static constexpr std::array<const char8_t*, 5> kHiraganaRow2{ u8"\u304B", u8"\u304D", u8"\u304F", u8"\u3051", u8"\u3053" };
	static constexpr std::array<const char8_t*, 5> kHiraganaRow3{ u8"\u3055", u8"\u3057", u8"\u3059", u8"\u305B", u8"\u305D" };
	static constexpr std::array<const char8_t*, 5> kHiraganaRow4{ u8"\u305F", u8"\u3061", u8"\u3064", u8"\u3066", u8"\u3068" };
	static constexpr std::array<const char8_t*, 5> kHiraganaRow5{ u8"\u306A", u8"\u306B", u8"\u306C", u8"\u306D", u8"\u306E" };
	static constexpr std::array<const char8_t*, 5> kHiraganaRow6{ u8"\u306F", u8"\u3072", u8"\u3075", u8"\u3078", u8"\u307B" };
	static constexpr std::array<const char8_t*, 5> kHiraganaRow7{ u8"\u307E", u8"\u307F", u8"\u3080", u8"\u3081", u8"\u3082" };
	static constexpr std::array<const char8_t*, 6> kHiraganaRow8{ u8"\u3084", u8"\u3086", u8"\u3088", u8"\u308F", u8"\u3092", u8"\u3093" };
	static constexpr std::array<const char8_t*, 10> kHiraganaRow9{
		u8"\u304C", u8"\u304E", u8"\u3050", u8"\u3052", u8"\u3054",
		u8"\u3056", u8"\u3058", u8"\u305A", u8"\u305C", u8"\u305E"
	};
	static constexpr std::array<const char8_t*, 10> kHiraganaRow10{
		u8"\u3060", u8"\u3062", u8"\u3065", u8"\u3067", u8"\u3069",
		u8"\u3070", u8"\u3073", u8"\u3076", u8"\u3079", u8"\u307C"
	};
	static constexpr std::array<const char8_t*, 10> kHiraganaRow11{
		u8"\u3071", u8"\u3074", u8"\u3077", u8"\u307A", u8"\u307D",
		u8"\u3041", u8"\u3043", u8"\u3045", u8"\u3047", u8"\u3049"
	};
	static constexpr std::array<const char8_t*, 10> kHiraganaRow12{
		u8"\u3083", u8"\u3085", u8"\u3087", u8"\u3063", u8"\u30FC",
		u8"\u3001", u8"\u3002", u8"\u300C", u8"\u300D", u8"\u30FB"
	};
	static constexpr std::array<const char8_t*, 5> kKatakanaRow1{ u8"\u30A2", u8"\u30A4", u8"\u30A6", u8"\u30A8", u8"\u30AA" };
	static constexpr std::array<const char8_t*, 5> kKatakanaRow2{ u8"\u30AB", u8"\u30AD", u8"\u30AF", u8"\u30B1", u8"\u30B3" };
	static constexpr std::array<const char8_t*, 5> kKatakanaRow3{ u8"\u30B5", u8"\u30B7", u8"\u30B9", u8"\u30BB", u8"\u30BD" };
	static constexpr std::array<const char8_t*, 5> kKatakanaRow4{ u8"\u30BF", u8"\u30C1", u8"\u30C4", u8"\u30C6", u8"\u30C8" };
	static constexpr std::array<const char8_t*, 5> kKatakanaRow5{ u8"\u30CA", u8"\u30CB", u8"\u30CC", u8"\u30CD", u8"\u30CE" };
	static constexpr std::array<const char8_t*, 5> kKatakanaRow6{ u8"\u30CF", u8"\u30D2", u8"\u30D5", u8"\u30D8", u8"\u30DB" };
	static constexpr std::array<const char8_t*, 5> kKatakanaRow7{ u8"\u30DE", u8"\u30DF", u8"\u30E0", u8"\u30E1", u8"\u30E2" };
	static constexpr std::array<const char8_t*, 6> kKatakanaRow8{ u8"\u30E4", u8"\u30E6", u8"\u30E8", u8"\u30EF", u8"\u30F2", u8"\u30F3" };
	static constexpr std::array<const char8_t*, 10> kKatakanaRow9{
		u8"\u30AC", u8"\u30AE", u8"\u30B0", u8"\u30B2", u8"\u30B4",
		u8"\u30B6", u8"\u30B8", u8"\u30BA", u8"\u30BC", u8"\u30BE"
	};
	static constexpr std::array<const char8_t*, 10> kKatakanaRow10{
		u8"\u30C0", u8"\u30C2", u8"\u30C5", u8"\u30C7", u8"\u30C9",
		u8"\u30D0", u8"\u30D3", u8"\u30D6", u8"\u30D9", u8"\u30DC"
	};
	static constexpr std::array<const char8_t*, 10> kKatakanaRow11{
		u8"\u30D1", u8"\u30D4", u8"\u30D7", u8"\u30DA", u8"\u30DD",
		u8"\u30A1", u8"\u30A3", u8"\u30A5", u8"\u30A7", u8"\u30A9"
	};
	static constexpr std::array<const char8_t*, 10> kKatakanaRow12{
		u8"\u30E3", u8"\u30E5", u8"\u30E7", u8"\u30C3", u8"\u30FC",
		u8"\u3001", u8"\u3002", u8"\u300C", u8"\u300D", u8"\u30FB"
	};
	static constexpr std::array<const char8_t*, 10> kHangulRow1{
		u8"\u3131", u8"\u3132", u8"\u3134", u8"\u3137", u8"\u3138",
		u8"\u3139", u8"\u3141", u8"\u3142", u8"\u3143", u8"\u3145"
	};
	static constexpr std::array<const char8_t*, 9> kHangulRow2{
		u8"\u3146", u8"\u3147", u8"\u3148", u8"\u3149", u8"\u314A",
		u8"\u314B", u8"\u314C", u8"\u314D", u8"\u314E"
	};
	static constexpr std::array<const char8_t*, 10> kHangulRow3{
		u8"\u314F", u8"\u3150", u8"\u3151", u8"\u3152", u8"\u3153",
		u8"\u3154", u8"\u3155", u8"\u3156", u8"\u3157", u8"\u315B"
	};
	static constexpr std::array<const char8_t*, 10> kHangulRow4{
		u8"\u315C", u8"\u3160", u8"\u3161", u8"\u3163", u8"\u3158",
		u8"\u3159", u8"\u315A", u8"\u315D", u8"\u315E", u8"\u315F"
	};
	static constexpr std::array<const char8_t*, 6> kHangulRow5{
		u8"\u3162", u8"\u30FC", u8"\u3001", u8"\u3002", u8"\uFF01", u8"\uFF1F"
	};

	ResultCallback callback;
	std::string resultValue;
	bool confirm = false;
	bool cancel = false;

	std::unique_lock lock(stateLock_);
	if (!internalKeyboardOpen_) {
		return;
	}

	if (internalPopupRequested_) {
		internalPopupRequested_ = false;
		ImGui::SetNextWindowFocus();
	}

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float viewportWidth = viewport ? viewport->Size.x : kPopupWidth;
	const float viewportHeight = viewport ? viewport->Size.y : 720.0f;
	const float windowWidth = (std::min)(kPopupWidth, viewportWidth * 0.82f);
	const float windowHeight = (std::clamp)(viewportHeight * 0.66f, 400.0f, 620.0f);
	if (viewport) {
		ImGui::SetNextWindowPos(
			ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f),
			ImGuiCond_Always,
			ImVec2(0.5f, 0.5f));
	}
	ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_Always);
	if (!ImGui::Begin(
			kInternalKeyboardPopup,
			nullptr,
			ImGuiWindowFlags_NoCollapse |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	auto appendLayoutText = [this](std::string_view value) {
		if (activeOptions_.digitsOnly) {
			internalBuffer_.append(value);
			KeepOnlyDigits(internalBuffer_);
			return;
		}

		if (currentLayout_ == Layout::Hangul) {
			if (const auto codepoint = DecodeSingleUtf8Codepoint(value)) {
				AppendUtf8(internalBuffer_, *codepoint);
				return;
			}
		}

		internalBuffer_.append(value);
	};

	auto setLayout = [this](Layout layout) {
		currentLayout_ = layout;
		uppercase_ = false;
	};

	auto confirmKeyboard = [&]() {
		confirm = true;
		resultValue = internalBuffer_;
	};

	auto drawActionRow = [&]() {
		const ImGuiStyle& style = ImGui::GetStyle();

		if (activeOptions_.digitsOnly) {
			const float totalWidth =
				(kWideKeyWidth * 4.0f) + (style.ItemSpacing.x * 3.0f);
			CenterNextRow(totalWidth);

			if (ImGui::Button(TR("aim_gamepad_keyboard_backspace", "Back"), ImVec2(kWideKeyWidth, 0.0f))) {
				PopLastUtf8Codepoint(internalBuffer_);
			}
			ImGui::SameLine();
			if (ImGui::Button(TR("aim_gamepad_keyboard_clear", "Clear"), ImVec2(kWideKeyWidth, 0.0f))) {
				internalBuffer_.clear();
			}
			ImGui::SameLine();
			if (ImGui::Button(TR("aim_gamepad_keyboard_ok", "OK"), ImVec2(kWideKeyWidth, 0.0f))) {
				confirmKeyboard();
			}
			ImGui::SameLine();
			if (ImGui::Button(TR("aim_gamepad_keyboard_cancel", "Cancel"), ImVec2(kWideKeyWidth, 0.0f))) {
				cancel = true;
			}
			return;
		}

		float firstRowWidth = (kKeyWidth * 5.0f) + kSpaceKeyWidth + kWideKeyWidth + (style.ItemSpacing.x * 6.0f);
		if (currentLayout_ == Layout::Latin) {
			firstRowWidth += kWideKeyWidth + style.ItemSpacing.x;
		}
		CenterNextRow(firstRowWidth);

		if (currentLayout_ == Layout::Latin) {
			if (ImGui::Button(
					uppercase_ ?
                        TR("aim_gamepad_keyboard_shift_on", "Shift: ON") :
                        TR("aim_gamepad_keyboard_shift_off", "Shift"),
					ImVec2(kWideKeyWidth, 0.0f))) {
				uppercase_ = !uppercase_;
			}
			ImGui::SameLine();
		}

		if (ImGui::Button("-", ImVec2(kKeyWidth, 0.0f))) {
			internalBuffer_.push_back('-');
		}
		ImGui::SameLine();
		if (ImGui::Button("_", ImVec2(kKeyWidth, 0.0f))) {
			internalBuffer_.push_back('_');
		}
		ImGui::SameLine();
		if (ImGui::Button(".", ImVec2(kKeyWidth, 0.0f))) {
			internalBuffer_.push_back('.');
		}
		ImGui::SameLine();
		if (ImGui::Button("'", ImVec2(kKeyWidth, 0.0f))) {
			internalBuffer_.push_back('\'');
		}
		ImGui::SameLine();
		if (ImGui::Button("/", ImVec2(kKeyWidth, 0.0f))) {
			internalBuffer_.push_back('/');
		}
		ImGui::SameLine();
		if (ImGui::Button(TR("aim_gamepad_keyboard_space", "Space"), ImVec2(kSpaceKeyWidth, 0.0f))) {
			if (activeOptions_.allowSpace) {
				internalBuffer_.push_back(' ');
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(TR("aim_gamepad_keyboard_backspace", "Back"), ImVec2(kWideKeyWidth, 0.0f))) {
			PopLastUtf8Codepoint(internalBuffer_);
		}

		ImGui::Spacing();
		const float secondRowWidth = (kWideKeyWidth * 3.0f) + (style.ItemSpacing.x * 2.0f);
		CenterNextRow(secondRowWidth);
		if (ImGui::Button(TR("aim_gamepad_keyboard_clear", "Clear"), ImVec2(kWideKeyWidth, 0.0f))) {
			internalBuffer_.clear();
		}
		ImGui::SameLine();
		if (ImGui::Button(TR("aim_gamepad_keyboard_ok", "OK"), ImVec2(kWideKeyWidth, 0.0f))) {
			confirmKeyboard();
		}
		ImGui::SameLine();
		if (ImGui::Button(TR("aim_gamepad_keyboard_cancel", "Cancel"), ImVec2(kWideKeyWidth, 0.0f))) {
			cancel = true;
		}
	};

	ImGui::TextUnformatted(TR("aim_gamepad_keyboard_title", "On-Screen Keyboard"));
	ImGui::Separator();
	ImGui::TextDisabled(
		"%s",
		Settings::enable_ime_support ?
            TR("aim_gamepad_keyboard_ime_hint_enabled", "The field above accepts IME input for CN/JP/KR.") :
            TR("aim_gamepad_keyboard_ime_hint_disabled", "Enable dMenu IME in Settings for full CN/JP/KR input."));
	if (openedWithGamepad_) {
		ImGui::TextDisabled(
			"%s",
			activeOptions_.digitsOnly ?
				TR("aim_gamepad_keyboard_controls_numeric", "A Select  X Backspace  View Clear") :
				TR("aim_gamepad_keyboard_controls_text", "A Select  X Backspace  LB/RB Layout  View Clear"));
	}
	ImGui::SetNextItemWidth(-1.0f);
	IMEWidgets::InputTextWithHint(
		TR("aim_gamepad_keyboard_input", "Input"),
		activeOptions_.digitsOnly ?
			TR("aim_gamepad_keyboard_input_hint_numeric", "Enter digits directly or use the keypad below.") :
			TR("aim_gamepad_keyboard_input_hint", "Type here or use the layout tabs below."),
		&internalBuffer_,
		activeOptions_.digitsOnly ? ImGuiInputTextFlags_CharsDecimal : 0);
	if (activeOptions_.digitsOnly) {
		KeepOnlyDigits(internalBuffer_);
	}
	if (internalFocusTextField_) {
		ImGui::SetItemDefaultFocus();
		internalFocusTextField_ = false;
	}

	ImGui::Spacing();
	if (internalBuffer_.empty()) {
		ImGui::TextDisabled("%s", TR("aim_gamepad_keyboard_empty", "<empty>"));
	} else {
		ImGui::BeginChild("##keyboard_value", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 1.8f), true);
		ImGui::TextWrapped("%s", internalBuffer_.c_str());
		ImGui::EndChild();
	}

	ImGui::Spacing();
	if (!activeOptions_.digitsOnly) {
		ImGui::TextDisabled("%s", TR("aim_gamepad_keyboard_layout_hint", "Choose a layout below or use LB/RB."));
		const ImGuiStyle& style = ImGui::GetStyle();
		const float layoutButtonWidth = (ImGui::GetContentRegionAvail().x - (style.ItemSpacing.x * 3.0f)) / 4.0f;
		auto drawLayoutButton = [&](Layout layout, const char* label, bool sameLine) {
			const bool selected = currentLayout_ == layout;
			if (sameLine) {
				ImGui::SameLine();
			}

			if (selected) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			}
			if (ImGui::Button(label, ImVec2(layoutButtonWidth, 0.0f))) {
				setLayout(layout);
			}
			if (selected) {
				ImGui::PopStyleColor(2);
			}
		};

		drawLayoutButton(Layout::Latin, TR("aim_gamepad_keyboard_layout_latin", "Latin"), false);
		drawLayoutButton(Layout::Hiragana, TR("aim_gamepad_keyboard_layout_hiragana", "Hiragana"), true);
		drawLayoutButton(Layout::Katakana, TR("aim_gamepad_keyboard_layout_katakana", "Katakana"), true);
		drawLayoutButton(Layout::Hangul, TR("aim_gamepad_keyboard_layout_hangul", "Hangul"), true);
	}

	ImGui::Spacing();
	const float keyAreaHeight = (std::max)(180.0f, ImGui::GetContentRegionAvail().y);
	ImGui::BeginChild("##keyboard_keys", ImVec2(0.0f, keyAreaHeight), true);
	bool focusFirstKey = internalFocusFirstKey_ && deferredDefaultFocusFrames_ == 0;
	if (activeOptions_.digitsOnly) {
		DrawLatinKeyRow("1234567890", false, focusFirstKey, [this](char ch) { internalBuffer_.push_back(ch); });
	} else {
		switch (currentLayout_) {
		case Layout::Latin:
			DrawLatinKeyRow("1234567890", uppercase_, focusFirstKey, [this](char ch) { internalBuffer_.push_back(ch); });
			DrawLatinKeyRow("qwertyuiop", uppercase_, focusFirstKey, [this](char ch) { internalBuffer_.push_back(ch); });
			DrawLatinKeyRow("asdfghjkl", uppercase_, focusFirstKey, [this](char ch) { internalBuffer_.push_back(ch); });
			DrawLatinKeyRow("zxcvbnm", uppercase_, focusFirstKey, [this](char ch) { internalBuffer_.push_back(ch); });
			break;
		case Layout::Hiragana:
			DrawKeyRow(kHiraganaRow1, focusFirstKey, appendLayoutText);
			DrawKeyRow(kHiraganaRow2, focusFirstKey, appendLayoutText);
			DrawKeyRow(kHiraganaRow3, focusFirstKey, appendLayoutText);
			DrawKeyRow(kHiraganaRow4, focusFirstKey, appendLayoutText);
			DrawKeyRow(kHiraganaRow5, focusFirstKey, appendLayoutText);
			DrawKeyRow(kHiraganaRow6, focusFirstKey, appendLayoutText);
			DrawKeyRow(kHiraganaRow7, focusFirstKey, appendLayoutText);
			DrawKeyRow(kHiraganaRow8, focusFirstKey, appendLayoutText);
			DrawKeyRow(kHiraganaRow9, focusFirstKey, appendLayoutText);
			DrawKeyRow(kHiraganaRow10, focusFirstKey, appendLayoutText);
			DrawKeyRow(kHiraganaRow11, focusFirstKey, appendLayoutText);
			DrawKeyRow(kHiraganaRow12, focusFirstKey, appendLayoutText);
			break;
		case Layout::Katakana:
			DrawKeyRow(kKatakanaRow1, focusFirstKey, appendLayoutText);
			DrawKeyRow(kKatakanaRow2, focusFirstKey, appendLayoutText);
			DrawKeyRow(kKatakanaRow3, focusFirstKey, appendLayoutText);
			DrawKeyRow(kKatakanaRow4, focusFirstKey, appendLayoutText);
			DrawKeyRow(kKatakanaRow5, focusFirstKey, appendLayoutText);
			DrawKeyRow(kKatakanaRow6, focusFirstKey, appendLayoutText);
			DrawKeyRow(kKatakanaRow7, focusFirstKey, appendLayoutText);
			DrawKeyRow(kKatakanaRow8, focusFirstKey, appendLayoutText);
			DrawKeyRow(kKatakanaRow9, focusFirstKey, appendLayoutText);
			DrawKeyRow(kKatakanaRow10, focusFirstKey, appendLayoutText);
			DrawKeyRow(kKatakanaRow11, focusFirstKey, appendLayoutText);
			DrawKeyRow(kKatakanaRow12, focusFirstKey, appendLayoutText);
			break;
		case Layout::Hangul:
			ImGui::TextDisabled("%s", TR("aim_gamepad_keyboard_hangul_hint", "Use the input field above with IME for full Korean syllables."));
			DrawKeyRow(kHangulRow1, focusFirstKey, appendLayoutText);
			DrawKeyRow(kHangulRow2, focusFirstKey, appendLayoutText);
			DrawKeyRow(kHangulRow3, focusFirstKey, appendLayoutText);
			DrawKeyRow(kHangulRow4, focusFirstKey, appendLayoutText);
			DrawKeyRow(kHangulRow5, focusFirstKey, appendLayoutText);
			break;
		}
	}
	ImGui::Spacing();
	drawActionRow();
	if (!focusFirstKey && internalFocusFirstKey_ && deferredDefaultFocusFrames_ == 0) {
		internalFocusFirstKey_ = false;
	}
	ImGui::EndChild();

	if (!activeOptions_.digitsOnly) {
		if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) {
			setLayout(CycleLayout(currentLayout_, -1));
		}
		if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) {
			setLayout(CycleLayout(currentLayout_, 1));
		}
	}
	if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
		PopLastUtf8Codepoint(internalBuffer_);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false)) {
		internalBuffer_.clear();
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		cancel = true;
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
	    ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
		confirmKeyboard();
	}

	if (confirm || cancel) {
		callback = confirm ? std::move(pendingCallback_) : ResultCallback{};
		pendingCallback_ = nullptr;
		internalKeyboardOpen_ = false;
		internalPopupRequested_ = false;
		internalFocusTextField_ = false;
		internalFocusFirstKey_ = false;
		openedWithGamepad_ = false;
		deferredDefaultFocusFrames_ = 0;
		uppercase_ = false;
		currentLayout_ = Layout::Latin;
		activeOptions_ = {};
		awaitingResult_.store(false);
		internalBuffer_.clear();
	}

	ImGui::End();
	if (deferredDefaultFocusFrames_ > 0) {
		deferredDefaultFocusFrames_--;
	}
	lock.unlock();

	if (callback) {
		callback(std::move(resultValue));
	}
}
