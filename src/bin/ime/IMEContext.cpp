#include "IMEContext.h"

namespace IME
{
	void Context::Clear()
	{
		widgetId = 0;
		label.clear();
		rect = ImRect();
		active = false;
		focused = false;
		multiline = false;
		frameSeen = -1;
	}

	bool Context::IsValid() const
	{
		return widgetId != 0;
	}

	bool Context::IsFresh(int frameCount) const
	{
		return IsValid() && frameSeen == frameCount;
	}

	ImVec2 Context::GetOverlayAnchor() const
	{
		return ImVec2(rect.Min.x, rect.Max.y + 4.0f);
	}
}
