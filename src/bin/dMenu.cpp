#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include "imgui_internal.h"
#include "imgui.h"
#include <array>
#include <optional>

#include "dMenu.h"
#include "menus/Trainer.h"
#include "menus/AIM.h"
#include "menus/Settings.h"
#include "menus/ModSettings.h"
#include "menus/Translator.h"

#include "Renderer.h"
#include "ScreenKeyboardBridge.h"

void DMenu::draw()
{
	static int lastDrawFrame = -1000;
	static int navDebugFrames = 0;
	const int currentFrame = ImGui::GetFrameCount();
	const bool menuJustOpened = currentFrame != lastDrawFrame + 1;
	lastDrawFrame = currentFrame;

	auto stepVisibleTab = [](Tab current, int direction) {
		constexpr std::array<Tab, 4> kTabOrder = {
			Tab::Trainer,
			Tab::AIM,
			Tab::ModSettings,
			Tab::Settings
		};

		std::size_t index = 0;
		for (std::size_t i = 0; i < kTabOrder.size(); ++i) {
			if (kTabOrder[i] == current) {
				index = i;
				break;
			}
		}

		const int count = static_cast<int>(kTabOrder.size());
		int next = static_cast<int>(index) + direction;
		while (next < 0) {
			next += count;
		}
		next %= count;
		return kTabOrder[static_cast<std::size_t>(next)];
	};

	auto getVisibleTabIndex = [](Tab current) {
		constexpr std::array<Tab, 4> kTabOrder = {
			Tab::Trainer,
			Tab::AIM,
			Tab::ModSettings,
			Tab::Settings
		};

		for (std::size_t i = 0; i < kTabOrder.size(); ++i) {
			if (kTabOrder[i] == current) {
				return static_cast<int>(i);
			}
		}

		return 0;
	};

	auto requestPrimaryActionFocus = [](Tab current) {
		switch (current) {
		case Tab::ModSettings:
			ModSettings::RequestPrimaryActionFocus();
			return true;
		case Tab::Settings:
			Settings::RequestPrimaryActionFocus();
			return true;
		default:
			return false;
		}
	};

	static std::optional<Tab> requestedTab;
	const bool screenKeyboardActive = ScreenKeyboardBridge::GetSingleton().IsAwaitingResult();
	const bool gamepadL1Pressed = ImGui::IsKeyPressed(ImGuiKey_GamepadL1);
	const bool gamepadR1Pressed = ImGui::IsKeyPressed(ImGuiKey_GamepadR1);
	const bool gamepadDpadDownPressed = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown);
	const bool gamepadDpadRightPressed = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight);
	const bool gamepadAPressed = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown);
	if (navDebugFrames > 0 &&
	    (gamepadL1Pressed || gamepadR1Pressed || gamepadDpadDownPressed || gamepadDpadRightPressed || gamepadAPressed)) {
		INFO(
			"NavDebugKeys: L1={} R1={} Down={} Right={} A={} currentTab={} requestedTab={}",
			gamepadL1Pressed ? 1 : 0,
			gamepadR1Pressed ? 1 : 0,
			gamepadDpadDownPressed ? 1 : 0,
			gamepadDpadRightPressed ? 1 : 0,
			gamepadAPressed ? 1 : 0,
			static_cast<int>(currentTab),
			requestedTab.has_value() ? static_cast<int>(requestedTab.value()) : -1);
	}
	if (!screenKeyboardActive && gamepadL1Pressed) {
		requestedTab = stepVisibleTab(currentTab, -1);
	} else if (!screenKeyboardActive && gamepadR1Pressed) {
		constexpr int kLastVisibleTabIndex = 3;
		if (getVisibleTabIndex(currentTab) >= kLastVisibleTabIndex && requestPrimaryActionFocus(currentTab)) {
			requestedTab.reset();
		} else {
			requestedTab = stepVisibleTab(currentTab, +1);
		}
	}

	ImVec2 screenSize = ImGui::GetMainViewport()->Size;
	float screenSizeX = screenSize.x;
	float screenSizeY = screenSize.y;
	ImGui::SetNextWindowSize(ImVec2(
		Settings::relative_window_size_h * screenSizeX,
		Settings::relative_window_size_v * screenSizeY));
	if (menuJustOpened || requestedTab.has_value()) {
		ImGui::SetNextWindowFocus();
	}

	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_None;
	if (Settings::lockWindowPos) {
		windowFlags |= ImGuiWindowFlags_NoMove;
	}
	if (Settings::lockWindowSize) {
		windowFlags |= ImGuiWindowFlags_NoResize;
	}
	ImGui::Begin("dMenu", nullptr, windowFlags);
	if (menuJustOpened) {
		navDebugFrames = 12;
	}
	if (navDebugFrames > 0) {
		ImGuiContext* ctx = ImGui::GetCurrentContext();
		const char* navWindowName =
			(ctx && ctx->NavWindow && ctx->NavWindow->Name) ? ctx->NavWindow->Name : "<none>";
		INFO(
			"NavDebug: windowFocused={} childFocused={} navWindow='{}' navId={} currentTab={}",
			ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ? 1 : 0,
			ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) ? 1 : 0,
			navWindowName,
			ctx ? ctx->NavId : 0,
			static_cast<int>(currentTab));
		navDebugFrames--;
	}
	const bool requestActiveTabFocus = ImGui::IsWindowAppearing() || requestedTab.has_value();

	if (screenKeyboardActive) {
		ImGui::BeginDisabled();
	}
	if (ImGui::BeginTabBar("TabBar", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
		if (ImGui::BeginTabItem(
				TR("tab_trainer", "Trainer"),
				nullptr,
				requestedTab.has_value() && requestedTab.value() == Tab::Trainer ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
			currentTab = Trainer;
			if (requestActiveTabFocus && (!requestedTab.has_value() || requestedTab.value() == Tab::Trainer)) {
				ImGui::SetKeyboardFocusHere();
			}
			Trainer::show();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(
				TR("tab_aim", "AIM"),
				nullptr,
				requestedTab.has_value() && requestedTab.value() == Tab::AIM ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
			currentTab = Tab::AIM;
			if (requestActiveTabFocus && (!requestedTab.has_value() || requestedTab.value() == Tab::AIM)) {
				ImGui::SetKeyboardFocusHere();
			}
			AIM::show();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(
				TR("tab_mod_config", "Mod Config"),
				nullptr,
				requestedTab.has_value() && requestedTab.value() == Tab::ModSettings ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
			currentTab = Tab::ModSettings;
			if (requestActiveTabFocus && (!requestedTab.has_value() || requestedTab.value() == Tab::ModSettings)) {
				ImGui::SetKeyboardFocusHere();
			}
			ModSettings::show();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem(
				TR("tab_settings", "Settings"),
				nullptr,
				requestedTab.has_value() && requestedTab.value() == Tab::Settings ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
			currentTab = Tab::Settings;
			if (requestActiveTabFocus && (!requestedTab.has_value() || requestedTab.value() == Tab::Settings)) {
				ImGui::SetKeyboardFocusHere();
			}
			Settings::show();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
	if (screenKeyboardActive) {
		ImGui::EndDisabled();
	}

	// Clear request only once target tab has actually been selected.
	if (requestedTab.has_value() && requestedTab.value() == currentTab) {
		requestedTab.reset();
	}

	ImGui::End();
}

ImVec2 DMenu::relativeSize(float a_width, float a_height)
{
	ImVec2 parentSize = ImGui::GetMainViewport()->Size;
	return ImVec2(a_width * parentSize.x, a_height * parentSize.y);
}

void DMenu::init(float a_screenWidth, float a_screenHeight)
{
	INFO("Initializing DMenu");
	AIM::init();
	Trainer::init();

	ImVec2 mainWindowSize = {
		float(a_screenWidth * Settings::relative_window_size_h),
		float(a_screenHeight * Settings::relative_window_size_v)
	};
	ImGui::SetNextWindowSize(mainWindowSize, ImGuiCond_FirstUseEver);
	ImVec2 mainWindowPos = { Settings::windowPos_x, Settings::windowPos_y };
	ImGui::SetNextWindowPos(mainWindowPos, ImGuiCond_FirstUseEver);
	ImGui::GetIO().FontGlobalScale = Settings::fontScale;

	INFO("DMenu initialized");

	initialized = true;
}
