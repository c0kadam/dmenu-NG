#include "InputListener.h"

#include <WinUser.h>
#include <Windows.h>
#include <dinput.h>
#include <algorithm>
#include <array>

#include <imgui.h>
#include "imgui_internal.h"

#include "Renderer.h"
#include "ScreenKeyboardBridge.h"
#include "WheelerCooperativeOpening.h"
#include "ime/IMEManager.h"
#include "ime/SimpleIMEBridge.h"

#include "menus/ModSettings.h"
#include "menus/Settings.h"

// Cached modifier key states from input events.
static bool s_mkbModifierDown = false;
static bool s_gamepadModifierDown = false;
static bool s_hintBindingHeld = false;
static constexpr std::array<ImGuiKey, 16> kTrackedGamepadKeys = {
	ImGuiKey_GamepadDpadUp,
	ImGuiKey_GamepadDpadDown,
	ImGuiKey_GamepadDpadLeft,
	ImGuiKey_GamepadDpadRight,
	ImGuiKey_GamepadStart,
	ImGuiKey_GamepadBack,
	ImGuiKey_GamepadL3,
	ImGuiKey_GamepadR3,
	ImGuiKey_GamepadL1,
	ImGuiKey_GamepadR1,
	ImGuiKey_GamepadFaceDown,
	ImGuiKey_GamepadFaceRight,
	ImGuiKey_GamepadFaceLeft,
	ImGuiKey_GamepadFaceUp,
	ImGuiKey_GamepadL2,
	ImGuiKey_GamepadR2
};
static std::array<bool, kTrackedGamepadKeys.size()> s_blockedGamepadKeys = {};
static int s_suppressFaceButtonsFrames = 0;

static int GetTrackedGamepadKeyIndex(ImGuiKey key)
{
	for (std::size_t i = 0; i < kTrackedGamepadKeys.size(); ++i) {
		if (kTrackedGamepadKeys[i] == key) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

static void SetBlockedGamepadKey(ImGuiKey key, bool blocked)
{
	const int index = GetTrackedGamepadKeyIndex(key);
	if (index >= 0) {
		s_blockedGamepadKeys[static_cast<std::size_t>(index)] = blocked;
	}
}

// enum : uint32_t
// {
//     kInvalid        = static_cast<uint32_t>(-1),
//     kKeyboardOffset = 0,
//     kMouseOffset    = 256,
//     kGamepadOffset  = 266,
//     kMaxOffset      = 282
// };

// enum
// {
//     kGamepadButtonOffset_DPAD_UP = kGamepadOffset, // 266
//     kGamepadButtonOffset_DPAD_DOWN,
//     kGamepadButtonOffset_DPAD_LEFT,
//     kGamepadButtonOffset_DPAD_RIGHT,
//     kGamepadButtonOffset_START,
//     kGamepadButtonOffset_BACK,
//     kGamepadButtonOffset_LEFT_THUMB,
//     kGamepadButtonOffset_RIGHT_THUMB,
//     kGamepadButtonOffset_LEFT_SHOULDER,
//     kGamepadButtonOffset_RIGHT_SHOULDER,
//     kGamepadButtonOffset_A,
//     kGamepadButtonOffset_B,
//     kGamepadButtonOffset_X,
//     kGamepadButtonOffset_Y,
//     kGamepadButtonOffset_LT,
//     kGamepadButtonOffset_RT // 281
// };

#define IM_VK_KEYPAD_ENTER (VK_RETURN + 256)
static void SubmitImGuiModifierEvents(ImGuiIO& io)
{
	const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	const bool altDown = (GetKeyState(VK_MENU) & 0x8000) != 0;
	const bool superDown = ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) != 0;

	io.AddKeyEvent(ImGuiMod_Ctrl, ctrlDown);
	io.AddKeyEvent(ImGuiMod_Shift, shiftDown);
	io.AddKeyEvent(ImGuiMod_Alt, altDown);
	io.AddKeyEvent(ImGuiMod_Super, superDown);
}

static ImGuiKey ImGui_ImplWin32_VirtualKeyToImGuiKey(WPARAM wParam)
{
	switch (wParam) {
	case VK_TAB:
		return ImGuiKey_Tab;
	case VK_LEFT:
		return ImGuiKey_LeftArrow;
	case VK_RIGHT:
		return ImGuiKey_RightArrow;
	case VK_UP:
		return ImGuiKey_UpArrow;
	case VK_DOWN:
		return ImGuiKey_DownArrow;
	case VK_PRIOR:
		return ImGuiKey_PageUp;
	case VK_NEXT:
		return ImGuiKey_PageDown;
	case VK_HOME:
		return ImGuiKey_Home;
	case VK_END:
		return ImGuiKey_End;
	case VK_INSERT:
		return ImGuiKey_Insert;
	case VK_DELETE:
		return ImGuiKey_Delete;
	case VK_BACK:
		return ImGuiKey_Backspace;
	case VK_SPACE:
		return ImGuiKey_Space;
	case VK_RETURN:
		return ImGuiKey_Enter;
	case VK_ESCAPE:
		return ImGuiKey_Escape;
	case VK_OEM_7:
		return ImGuiKey_Apostrophe;
	case VK_OEM_COMMA:
		return ImGuiKey_Comma;
	case VK_OEM_MINUS:
		return ImGuiKey_Minus;
	case VK_OEM_PERIOD:
		return ImGuiKey_Period;
	case VK_OEM_2:
		return ImGuiKey_Slash;
	case VK_OEM_1:
		return ImGuiKey_Semicolon;
	case VK_OEM_PLUS:
		return ImGuiKey_Equal;
	case VK_OEM_4:
		return ImGuiKey_LeftBracket;
	case VK_OEM_5:
		return ImGuiKey_Backslash;
	case VK_OEM_6:
		return ImGuiKey_RightBracket;
	case VK_OEM_3:
		return ImGuiKey_GraveAccent;
	case VK_CAPITAL:
		return ImGuiKey_CapsLock;
	case VK_SCROLL:
		return ImGuiKey_ScrollLock;
	case VK_NUMLOCK:
		return ImGuiKey_NumLock;
	case VK_SNAPSHOT:
		return ImGuiKey_PrintScreen;
	case VK_PAUSE:
		return ImGuiKey_Pause;
	case VK_NUMPAD0:
		return ImGuiKey_Keypad0;
	case VK_NUMPAD1:
		return ImGuiKey_Keypad1;
	case VK_NUMPAD2:
		return ImGuiKey_Keypad2;
	case VK_NUMPAD3:
		return ImGuiKey_Keypad3;
	case VK_NUMPAD4:
		return ImGuiKey_Keypad4;
	case VK_NUMPAD5:
		return ImGuiKey_Keypad5;
	case VK_NUMPAD6:
		return ImGuiKey_Keypad6;
	case VK_NUMPAD7:
		return ImGuiKey_Keypad7;
	case VK_NUMPAD8:
		return ImGuiKey_Keypad8;
	case VK_NUMPAD9:
		return ImGuiKey_Keypad9;
	case VK_DECIMAL:
		return ImGuiKey_KeypadDecimal;
	case VK_DIVIDE:
		return ImGuiKey_KeypadDivide;
	case VK_MULTIPLY:
		return ImGuiKey_KeypadMultiply;
	case VK_SUBTRACT:
		return ImGuiKey_KeypadSubtract;
	case VK_ADD:
		return ImGuiKey_KeypadAdd;
	case IM_VK_KEYPAD_ENTER:
		return ImGuiKey_KeypadEnter;
	case VK_LSHIFT:
		return ImGuiKey_LeftShift;
	case VK_LCONTROL:
		return ImGuiKey_LeftCtrl;
	case VK_LMENU:
		return ImGuiKey_LeftAlt;
	case VK_LWIN:
		return ImGuiKey_LeftSuper;
	case VK_RSHIFT:
		return ImGuiKey_RightShift;
	case VK_RCONTROL:
		return ImGuiKey_RightCtrl;
	case VK_RMENU:
		return ImGuiKey_RightAlt;
	case VK_RWIN:
		return ImGuiKey_RightSuper;
	case VK_APPS:
		return ImGuiKey_Menu;
	case '0':
		return ImGuiKey_0;
	case '1':
		return ImGuiKey_1;
	case '2':
		return ImGuiKey_2;
	case '3':
		return ImGuiKey_3;
	case '4':
		return ImGuiKey_4;
	case '5':
		return ImGuiKey_5;
	case '6':
		return ImGuiKey_6;
	case '7':
		return ImGuiKey_7;
	case '8':
		return ImGuiKey_8;
	case '9':
		return ImGuiKey_9;
	case 'A':
		return ImGuiKey_A;
	case 'B':
		return ImGuiKey_B;
	case 'C':
		return ImGuiKey_C;
	case 'D':
		return ImGuiKey_D;
	case 'E':
		return ImGuiKey_E;
	case 'F':
		return ImGuiKey_F;
	case 'G':
		return ImGuiKey_G;
	case 'H':
		return ImGuiKey_H;
	case 'I':
		return ImGuiKey_I;
	case 'J':
		return ImGuiKey_J;
	case 'K':
		return ImGuiKey_K;
	case 'L':
		return ImGuiKey_L;
	case 'M':
		return ImGuiKey_M;
	case 'N':
		return ImGuiKey_N;
	case 'O':
		return ImGuiKey_O;
	case 'P':
		return ImGuiKey_P;
	case 'Q':
		return ImGuiKey_Q;
	case 'R':
		return ImGuiKey_R;
	case 'S':
		return ImGuiKey_S;
	case 'T':
		return ImGuiKey_T;
	case 'U':
		return ImGuiKey_U;
	case 'V':
		return ImGuiKey_V;
	case 'W':
		return ImGuiKey_W;
	case 'X':
		return ImGuiKey_X;
	case 'Y':
		return ImGuiKey_Y;
	case 'Z':
		return ImGuiKey_Z;
	case VK_F1:
		return ImGuiKey_F1;
	case VK_F2:
		return ImGuiKey_F2;
	case VK_F3:
		return ImGuiKey_F3;
	case VK_F4:
		return ImGuiKey_F4;
	case VK_F5:
		return ImGuiKey_F5;
	case VK_F6:
		return ImGuiKey_F6;
	case VK_F7:
		return ImGuiKey_F7;
	case VK_F8:
		return ImGuiKey_F8;
	case VK_F9:
		return ImGuiKey_F9;
	case VK_F10:
		return ImGuiKey_F10;
	case VK_F11:
		return ImGuiKey_F11;
	case VK_F12:
		return ImGuiKey_F12;
	default:
		return ImGuiKey_None;
	}
}

static enum : std::uint32_t
{
	kInvalid = static_cast<std::uint32_t>(-1),
	kKeyboardOffset = 0,
	kMouseOffset = 256,
	kGamepadOffset = 266
};
			
static inline std::uint32_t GetGamepadIndex(RE::BSWin32GamepadDevice::Key a_key)
{
	using Key = RE::BSWin32GamepadDevice::Key;

	std::uint32_t index;
	switch (a_key) {
	case Key::kUp:
		index = 0;
		break;
	case Key::kDown:
		index = 1;
		break;
	case Key::kLeft:
		index = 2;
		break;
	case Key::kRight:
		index = 3;
		break;
	case Key::kStart:
		index = 4;
		break;
	case Key::kBack:
		index = 5;
		break;
	case Key::kLeftThumb:
		index = 6;
		break;
	case Key::kRightThumb:
		index = 7;
		break;
	case Key::kLeftShoulder:
		index = 8;
		break;
	case Key::kRightShoulder:
		index = 9;
		break;
	case Key::kA:
		index = 10;
		break;
	case Key::kB:
		index = 11;
		break;
	case Key::kX:
		index = 12;
		break;
	case Key::kY:
		index = 13;
		break;
	case Key::kLeftTrigger:
		index = 14;
		break;
	case Key::kRightTrigger:
		index = 15;
		break;
	default:
		index = kInvalid;
		break;
	}

	return index != kInvalid ? index + kGamepadOffset : kInvalid;
}

static ImGuiKey MapGamepadKeyToImGui(RE::BSWin32GamepadDevice::Key a_key)
{
	using Key = RE::BSWin32GamepadDevice::Key;
	switch (a_key) {
	case Key::kUp:
		return ImGuiKey_GamepadDpadUp;
	case Key::kDown:
		return ImGuiKey_GamepadDpadDown;
	case Key::kLeft:
		return ImGuiKey_GamepadDpadLeft;
	case Key::kRight:
		return ImGuiKey_GamepadDpadRight;
	case Key::kStart:
		return ImGuiKey_GamepadStart;
	case Key::kBack:
		return ImGuiKey_GamepadBack;
	case Key::kLeftThumb:
		return ImGuiKey_GamepadL3;
	case Key::kRightThumb:
		return ImGuiKey_GamepadR3;
	case Key::kLeftShoulder:
		return ImGuiKey_GamepadL1;
	case Key::kRightShoulder:
		return ImGuiKey_GamepadR1;
	case Key::kA:
		return ImGuiKey_GamepadFaceDown;
	case Key::kB:
		return ImGuiKey_GamepadFaceRight;
	case Key::kX:
		return ImGuiKey_GamepadFaceLeft;
	case Key::kY:
		return ImGuiKey_GamepadFaceUp;
	case Key::kLeftTrigger:
		return ImGuiKey_GamepadL2;
	case Key::kRightTrigger:
		return ImGuiKey_GamepadR2;
	default:
		return ImGuiKey_None;
	}
}

static ImGuiKey MapBoundInputCodeToImGuiKey(std::uint32_t inputCode)
{
	switch (inputCode) {
	case 266:
		return ImGuiKey_GamepadDpadUp;
	case 267:
		return ImGuiKey_GamepadDpadDown;
	case 268:
		return ImGuiKey_GamepadDpadLeft;
	case 269:
		return ImGuiKey_GamepadDpadRight;
	case 270:
		return ImGuiKey_GamepadStart;
	case 271:
		return ImGuiKey_GamepadBack;
	case 272:
		return ImGuiKey_GamepadL3;
	case 273:
		return ImGuiKey_GamepadR3;
	case 274:
		return ImGuiKey_GamepadL1;
	case 275:
		return ImGuiKey_GamepadR1;
	case 276:
		return ImGuiKey_GamepadFaceDown;
	case 277:
		return ImGuiKey_GamepadFaceRight;
	case 278:
		return ImGuiKey_GamepadFaceLeft;
	case 279:
		return ImGuiKey_GamepadFaceUp;
	case 280:
		return ImGuiKey_GamepadL2;
	case 281:
		return ImGuiKey_GamepadR2;
	default:
		return ImGuiKey_None;
	}
}

static void SubmitThumbstickAnalog(ImGuiIO& io, bool isLeftStick, float xValue, float yValue)
{
	constexpr float kDeadzone = 0.18f;
	const float x = (std::clamp)(xValue, -1.0f, 1.0f);
	const float y = (std::clamp)(yValue, -1.0f, 1.0f);

	const float left = x < -kDeadzone ? -x : 0.0f;
	const float right = x > kDeadzone ? x : 0.0f;
	// In Skyrim input, positive Y means up for sticks.
	const float up = y > kDeadzone ? y : 0.0f;
	const float down = y < -kDeadzone ? -y : 0.0f;

	const ImGuiKey keyLeft = isLeftStick ? ImGuiKey_GamepadLStickLeft : ImGuiKey_GamepadRStickLeft;
	const ImGuiKey keyRight = isLeftStick ? ImGuiKey_GamepadLStickRight : ImGuiKey_GamepadRStickRight;
	const ImGuiKey keyUp = isLeftStick ? ImGuiKey_GamepadLStickUp : ImGuiKey_GamepadRStickUp;
	const ImGuiKey keyDown = isLeftStick ? ImGuiKey_GamepadLStickDown : ImGuiKey_GamepadRStickDown;

	io.AddKeyAnalogEvent(keyLeft, left > 0.0f, left);
io.AddKeyAnalogEvent(keyRight, right > 0.0f, right);
io.AddKeyAnalogEvent(keyUp, up > 0.0f, up);
io.AddKeyAnalogEvent(keyDown, down > 0.0f, down);
}

void InputListener::ProcessEvent(RE::InputEvent** a_event)
{
	if (!a_event)
		return;

	auto& io = ImGui::GetIO();
	const bool menuEnabledAtStart = Renderer::IsEnabled();
	const bool screenKeyboardPending = ScreenKeyboardBridge::GetSingleton().IsAwaitingResult();

	for (auto event = *a_event; event; event = event->next) {
		if (const auto charEvent = event->AsCharEvent()) {
			if (IME::SimpleIMEBridge::Get().ShouldSuppressGameCharEvent()) {
				// SimpleIME consumes Skyrim's raw character stream while active and sends
				// authoritative committed text through its custom Scaleform event instead.
				continue;
			}
			const auto codepoint = charEvent->keyCode;
			if (!IME::Manager::Get().ShouldSuppressInputCharacter(codepoint)) {
				io.AddInputCharacter(codepoint);
			}
		} else if (const auto thumb = event->AsThumbstickEvent()) {
			if (thumb->GetDevice() != RE::INPUT_DEVICE::kGamepad) {
				continue;
			}

			const bool isLeftStick = thumb->GetIDCode() == RE::ThumbstickEvent::InputType::kLeftThumbstick;
			const bool isRightStick = thumb->GetIDCode() == RE::ThumbstickEvent::InputType::kRightThumbstick;
			if (!isLeftStick && !isRightStick) {
				continue;
			}

			SubmitThumbstickAnalog(io, isLeftStick, thumb->xValue, thumb->yValue);
		} else if (const auto button = event->AsButtonEvent()) {
			if (button->IsPressed() && !button->IsDown())
				continue;

			auto scan_code = button->GetIDCode();
			const auto device = button->GetDevice();

			using DeviceType = RE::INPUT_DEVICE;
			std::uint32_t input = scan_code;
			switch (device) {
			case DeviceType::kMouse:
				input += kMouseOffset;
				break;
			case DeviceType::kKeyboard:
				input += kKeyboardOffset;
				break;
			case DeviceType::kGamepad:
				input = GetGamepadIndex((RE::BSWin32GamepadDevice::Key)input);
				break;
			default:
				continue;
			}

			if (input == kInvalid) {
				continue;
			}
			const bool cooperativeOpeningMatched =
				WheelerCooperativeOpening::IsCurrentEventMatched(reinterpret_cast<std::uintptr_t>(event));

			if (button->IsDown()) {
				ModSettings::submitInput(input);
			}

			// Submit to Settings key capture (for rebinding toggle/modifier inputs).
			const bool wasCapturingInput = Settings::IsCapturingInput();
			if (button->IsDown()) {
				Settings::submitKeyCapture(input);
			}

			const bool isCapturingInput = wasCapturingInput;

			if (Settings::key_toggle_modifier_mkb != 0 && input == Settings::key_toggle_modifier_mkb) {
				s_mkbModifierDown = button->IsPressed();
			}
			if (Settings::key_toggle_modifier_gamepad != 0 && input == Settings::key_toggle_modifier_gamepad) {
				s_gamepadModifierDown = button->IsPressed();
			}

			const bool isMkbMenuToggleBinding =
				Settings::key_toggle_dmenu_mkb != 0 &&
				input == Settings::key_toggle_dmenu_mkb;
			const bool isGamepadMenuToggleBinding =
				Settings::key_toggle_dmenu_gamepad != 0 &&
				input == Settings::key_toggle_dmenu_gamepad;
			const bool isMenuToggleBinding = isMkbMenuToggleBinding || isGamepadMenuToggleBinding;
			bool isMenuToggleInput = false;
			if (!isCapturingInput && !screenKeyboardPending && button->IsDown() && !io.WantTextInput) {
				if (isMkbMenuToggleBinding) {
					const bool modifierOk =
						(Settings::key_toggle_modifier_mkb == 0) || s_mkbModifierDown || cooperativeOpeningMatched;
					isMenuToggleInput = true;
					if (modifierOk) {
						Renderer::flip();
					}
				} else if (isGamepadMenuToggleBinding) {
					const bool modifierOk =
						(Settings::key_toggle_modifier_gamepad == 0) || s_gamepadModifierDown || cooperativeOpeningMatched;
					isMenuToggleInput = true;
					if (modifierOk) {
						Renderer::flip();
					}
				}
			}

			bool consumeBoundInput = isMenuToggleBinding;
			if (screenKeyboardPending && isMenuToggleBinding) {
				if (button->IsDown()) {
					ScreenKeyboardBridge::GetSingleton().CancelActiveRequest();
				}
				consumeBoundInput = true;
			}
			const bool isHintBindingInput =
				Settings::key_toggle_hints_gamepad != 0 &&
				input == Settings::key_toggle_hints_gamepad;

			if (isHintBindingInput && !button->IsPressed()) {
				s_hintBindingHeld = false;
				INFO("HintKey released (input={}, scan={}, down={})", input, scan_code, button->IsDown() ? 1 : 0);
			}

			if (!isCapturingInput &&
			    Renderer::IsEnabled() &&
			    !isMenuToggleInput &&
			    isHintBindingInput) {
				consumeBoundInput = true;
				if (button->IsPressed() && !s_hintBindingHeld) {
					ModSettings::ToggleHintsVisibility();
					s_suppressFaceButtonsFrames = 2;
					s_hintBindingHeld = true;
					INFO("HintKey accepted (input={}, scan={}, suppressFrames={})", input, scan_code, s_suppressFaceButtonsFrames);
				} else if (button->IsPressed() && s_hintBindingHeld) {
					INFO("HintKey ignored (held) (input={}, scan={})", input, scan_code);
				}
			}

			if (!isCapturingInput && device == RE::INPUT_DEVICE::kGamepad) {
				const bool isReservedGamepadBinding =
					(Settings::key_toggle_dmenu_gamepad != 0 && input == Settings::key_toggle_dmenu_gamepad) ||
					(Settings::key_toggle_hints_gamepad != 0 && input == Settings::key_toggle_hints_gamepad);
				if (isReservedGamepadBinding) {
					const bool blocked = button->IsDown();
					const ImGuiKey blockedKey = MapGamepadKeyToImGui(static_cast<RE::BSWin32GamepadDevice::Key>(scan_code));
					if (blockedKey != ImGuiKey_None) {
						SetBlockedGamepadKey(blockedKey, blocked);
					}
				}
			}

			switch (device) {
			case RE::INPUT_DEVICE::kMouse:
				if (consumeBoundInput) {
					break;
				}
				if (scan_code > 7)  // middle scroll
					io.AddMouseWheelEvent(0, button->Value() * (scan_code == 8 ? 1 : -1));
				else {
					if (scan_code > 5)
						scan_code = 5;
					io.AddMouseButtonEvent(scan_code, button->IsPressed());
				}
				break;
			case RE::INPUT_DEVICE::kKeyboard: {
				if (consumeBoundInput) {
					break;
				}
				uint32_t key = MapVirtualKeyEx(scan_code, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));
				switch (scan_code) {
				case DIK_LEFTARROW:
					key = VK_LEFT;
					break;
				case DIK_RIGHTARROW:
					key = VK_RIGHT;
					break;
				case DIK_UPARROW:
					key = VK_UP;
					break;
				case DIK_DOWNARROW:
					key = VK_DOWN;
					break;
				case DIK_DELETE:
					key = VK_DELETE;
					break;
				case DIK_END:
					key = VK_END;
					break;
				case DIK_HOME:
					key = VK_HOME;
					break;  // pos1
				case DIK_PRIOR:
					key = VK_PRIOR;
					break;  // page up
				case DIK_NEXT:
					key = VK_NEXT;
					break;  // page down
				case DIK_INSERT:
					key = VK_INSERT;
					break;
				case DIK_NUMPAD0:
					key = VK_NUMPAD0;
					break;
				case DIK_NUMPAD1:
					key = VK_NUMPAD1;
					break;
				case DIK_NUMPAD2:
					key = VK_NUMPAD2;
					break;
				case DIK_NUMPAD3:
					key = VK_NUMPAD3;
					break;
				case DIK_NUMPAD4:
					key = VK_NUMPAD4;
					break;
				case DIK_NUMPAD5:
					key = VK_NUMPAD5;
					break;
				case DIK_NUMPAD6:
					key = VK_NUMPAD6;
					break;
				case DIK_NUMPAD7:
					key = VK_NUMPAD7;
					break;
				case DIK_NUMPAD8:
					key = VK_NUMPAD8;
					break;
				case DIK_NUMPAD9:
					key = VK_NUMPAD9;
					break;
				case DIK_DECIMAL:
					key = VK_DECIMAL;
					break;
				case DIK_NUMPADENTER:
					key = IM_VK_KEYPAD_ENTER;
					break;
				case DIK_RMENU:
					key = VK_RMENU;
					break;  // right alt
				case DIK_RCONTROL:
					key = VK_RCONTROL;
					break;  // right control
				case DIK_LWIN:
					key = VK_LWIN;
					break;  // left win
				case DIK_RWIN:
					key = VK_RWIN;
					break;  // right win
				case DIK_APPS:
					key = VK_APPS;
					break;
				default:
					break;
				}

				io.AddKeyEvent(ImGui_ImplWin32_VirtualKeyToImGuiKey(key), button->IsPressed());
				SubmitImGuiModifierEvents(io);
				break;
			}
			case RE::INPUT_DEVICE::kGamepad: {
				if (consumeBoundInput) {
					break;
				}

				const ImGuiKey gamepadKey = MapGamepadKeyToImGui(static_cast<RE::BSWin32GamepadDevice::Key>(scan_code));
				if (gamepadKey != ImGuiKey_None) {
					if (gamepadKey == ImGuiKey_GamepadL2 || gamepadKey == ImGuiKey_GamepadR2) {
						io.AddKeyAnalogEvent(
							gamepadKey,
							button->IsPressed(),
							button->IsPressed() ? button->Value() : 0.0f);
					} else {
						io.AddKeyEvent(gamepadKey, button->IsPressed());
					}
				}
				break;
			}
			default:
				continue;
			}
		}
	}
	return;
}

void InputListener::ApplyBlockedImGuiGamepadKeys()
{
	auto& io = ImGui::GetIO();
	const bool menuEnabled = Renderer::IsEnabled();
	const ImGuiKey hintToggleKey = menuEnabled ? MapBoundInputCodeToImGuiKey(Settings::key_toggle_hints_gamepad) : ImGuiKey_None;
	const bool suppressFaceButtonsNow = s_suppressFaceButtonsFrames > 0;

	auto shouldSuppress = [&](ImGuiKey key) -> bool {
		if (key == ImGuiKey_None) {
			return false;
		}

		if (menuEnabled && hintToggleKey != ImGuiKey_None && key == hintToggleKey) {
			return true;
		}

		if (suppressFaceButtonsNow &&
		    (key == ImGuiKey_GamepadFaceDown ||
		     key == ImGuiKey_GamepadFaceRight ||
		     key == ImGuiKey_GamepadFaceLeft ||
		     key == ImGuiKey_GamepadFaceUp)) {
			return true;
		}

		const int idx = GetTrackedGamepadKeyIndex(key);
		return idx >= 0 && s_blockedGamepadKeys[static_cast<std::size_t>(idx)];
	};

	if (ImGuiContext* ctx = ImGui::GetCurrentContext()) {
		auto& queue = ctx->InputEventsQueue;
		for (int i = queue.Size - 1; i >= 0; --i) {
			const ImGuiInputEvent& ev = queue[i];
			if (ev.Type != ImGuiInputEventType_Key) {
				continue;
			}

			if (shouldSuppress(ev.Key.Key)) {
				queue.erase(&queue[i]);
			}
		}
	}

	auto suppressKey = [&](ImGuiKey key) {
		if (key == ImGuiKey_None) {
			return;
		}

		if (key == ImGuiKey_GamepadL2 || key == ImGuiKey_GamepadR2) {
			io.AddKeyAnalogEvent(key, false, 0.0f);
		} else {
			io.AddKeyEvent(key, false);
		}

		ImGuiKeyData* keyData = ImGui::GetKeyData(key);
		if (keyData) {
			keyData->Down = false;
			keyData->AnalogValue = 0.0f;
			keyData->DownDuration = -1.0f;
			keyData->DownDurationPrev = -1.0f;
		}
	};

	for (std::size_t i = 0; i < kTrackedGamepadKeys.size(); ++i) {
		const ImGuiKey key = kTrackedGamepadKeys[i];
		if (!shouldSuppress(key)) {
			continue;
		}

		suppressKey(key);
	}

	if (suppressFaceButtonsNow) {
		s_suppressFaceButtonsFrames--;
	}
}
