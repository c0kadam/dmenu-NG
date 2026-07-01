#include "IMEWidgets.h"

#include "imgui_internal.h"
#include "imgui_stdlib.h"

#include "IMEManager.h"

namespace
{
	struct CallbackChainData
	{
		ImGuiInputTextCallback callback{ nullptr };
		void* userData{ nullptr };
		ImGuiID widgetId{ 0 };
	};

	int InputTextCallback(ImGuiInputTextCallbackData* data)
	{
		if (data == nullptr) {
			return 0;
		}

		auto* chain = static_cast<CallbackChainData*>(data->UserData);
		if (chain != nullptr) {
			IME::Manager::Get().ApplyPendingCommit(data, chain->widgetId);
			if (chain->callback != nullptr) {
				data->UserData = chain->userData;
				return chain->callback(data);
			}
		}

		return 0;
	}

	void RegisterLastTextItem(const char* label, bool multiline)
	{
		if (!IME::Manager::Get().IsImeEnabled()) {
			return;
		}

		const ImGuiID itemId = ImGui::GetItemID();
		if (itemId == 0) {
			return;
		}

		const bool active = ImGui::IsItemActive();
		const bool focused = ImGui::IsItemFocused();
		const bool keepAlive = IME::Manager::Get().ShouldKeepTextTargetAlive(itemId);
		if (!active && !(focused && ImGui::GetIO().WantTextInput) && !keepAlive) {
			return;
		}

		IME::Manager::Get().RegisterTextTarget(
			itemId,
			label != nullptr ? label : "",
			ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()),
			active,
			focused,
			multiline);
	}
}

namespace IMEWidgets
{
	bool InputText(const char* label, std::string* text, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* userData)
	{
		if (!IME::Manager::Get().IsImeEnabled()) {
			return ImGui::InputText(label, text, flags, callback, userData);
		}

		CallbackChainData chainData{ callback, userData, ImGui::GetID(label) };
		const bool changed = ImGui::InputText(
			label,
			text,
			flags | ImGuiInputTextFlags_CallbackAlways,
			InputTextCallback,
			&chainData);
		RegisterLastTextItem(label, false);
		return changed;
	}

	bool InputTextMultiline(const char* label, std::string* text, const ImVec2& size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* userData)
	{
		if (!IME::Manager::Get().IsImeEnabled()) {
			return ImGui::InputTextMultiline(label, text, size, flags, callback, userData);
		}

		CallbackChainData chainData{ callback, userData, ImGui::GetID(label) };
		const bool changed = ImGui::InputTextMultiline(
			label,
			text,
			size,
			flags | ImGuiInputTextFlags_CallbackAlways,
			InputTextCallback,
			&chainData);
		RegisterLastTextItem(label, true);
		return changed;
	}

	bool InputTextWithHint(const char* label, const char* hint, std::string* text, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* userData)
	{
		if (!IME::Manager::Get().IsImeEnabled()) {
			return ImGui::InputTextWithHint(label, hint, text, flags, callback, userData);
		}

		CallbackChainData chainData{ callback, userData, ImGui::GetID(label) };
		const bool changed = ImGui::InputTextWithHint(
			label,
			hint,
			text,
			flags | ImGuiInputTextFlags_CallbackAlways,
			InputTextCallback,
			&chainData);
		RegisterLastTextItem(label, false);
		return changed;
	}

	bool InputText(const char* label, char* buffer, std::size_t bufferSize, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* userData)
	{
		if (!IME::Manager::Get().IsImeEnabled()) {
			return ImGui::InputText(label, buffer, bufferSize, flags, callback, userData);
		}

		CallbackChainData chainData{ callback, userData, ImGui::GetID(label) };
		const bool changed = ImGui::InputText(
			label,
			buffer,
			bufferSize,
			flags | ImGuiInputTextFlags_CallbackAlways,
			InputTextCallback,
			&chainData);
		RegisterLastTextItem(label, false);
		return changed;
	}

	bool InputTextMultiline(const char* label, char* buffer, std::size_t bufferSize, const ImVec2& size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* userData)
	{
		if (!IME::Manager::Get().IsImeEnabled()) {
			return ImGui::InputTextMultiline(label, buffer, bufferSize, size, flags, callback, userData);
		}

		CallbackChainData chainData{ callback, userData, ImGui::GetID(label) };
		const bool changed = ImGui::InputTextMultiline(
			label,
			buffer,
			bufferSize,
			size,
			flags | ImGuiInputTextFlags_CallbackAlways,
			InputTextCallback,
			&chainData);
		RegisterLastTextItem(label, true);
		return changed;
	}

	bool TextFilter(const char* label, ImGuiTextFilter& filter, float width)
	{
		if (!IME::Manager::Get().IsImeEnabled()) {
			return filter.Draw(label, width);
		}

		if (width != 0.0f) {
			ImGui::SetNextItemWidth(width);
		}

		const bool changed = InputText(label, filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf));
		if (changed) {
			filter.Build();
		}
		return changed;
	}
}
