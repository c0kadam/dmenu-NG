#pragma once

#include <string>

#include "imgui.h"

struct ImGuiTextFilter;

namespace IMEWidgets
{
	bool InputText(const char* label, std::string* text, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr);
	bool InputTextMultiline(const char* label, std::string* text, const ImVec2& size = ImVec2(0, 0), ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr);
	bool InputTextWithHint(const char* label, const char* hint, std::string* text, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr);

	bool InputText(const char* label, char* buffer, std::size_t bufferSize, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr);
	bool InputTextMultiline(const char* label, char* buffer, std::size_t bufferSize, const ImVec2& size = ImVec2(0, 0), ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr);

	bool TextFilter(const char* label, ImGuiTextFilter& filter, float width = 0.0f);
}
