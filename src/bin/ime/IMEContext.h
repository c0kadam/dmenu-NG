#pragma once

#include <string>

#include "imgui.h"
#include "imgui_internal.h"

namespace IME
{
	class Context
	{
	public:
		void Clear();
		bool IsValid() const;
		bool IsFresh(int frameCount) const;
		ImVec2 GetOverlayAnchor() const;

		ImGuiID widgetId{ 0 };
		std::string label;
		ImRect rect;
		bool active{ false };
		bool focused{ false };
		bool multiline{ false };
		int frameSeen{ -1 };
	};
}
