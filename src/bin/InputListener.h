#pragma once
#include "PCH.h"

#include <cstdint>
#include <optional>

class InputListener
{
public:
	enum class InputDeviceClass : std::uint8_t
	{
		Unknown,
		Keyboard,
		Mouse,
		Gamepad,
		Count
	};

	static InputListener* GetSingleton()
	{
		static InputListener listener;
		return std::addressof(listener);
	}

	void ProcessEvent(RE::InputEvent** a_event);
	static std::optional<std::uint32_t> ToInputCode(const RE::ButtonEvent& a_button);
	static InputDeviceClass ClassifyInputCode(std::uint32_t a_inputCode);

	static void ApplyBlockedImGuiGamepadKeys();
};
