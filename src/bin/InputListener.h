#pragma once
#include "PCH.h"

#include <cstdint>
#include <optional>

class InputListener
{
public:
	static InputListener* GetSingleton()
	{
		static InputListener listener;
		return std::addressof(listener);
	}

	void ProcessEvent(RE::InputEvent** a_event);
	static std::optional<std::uint32_t> ToInputCode(const RE::ButtonEvent& a_button);

	static void ApplyBlockedImGuiGamepadKeys();
};
