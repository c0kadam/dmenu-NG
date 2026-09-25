#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include <filesystem>

#include "imgui.h"
#include "Utils.h"
#include "ime/IMEWidgets.h"
namespace Utils
{
	namespace
	{
		_GetFormEditorID g_po3GetFormEditorID = nullptr;
		std::unordered_map<RE::FormID, std::string> g_nativeEditorIDs;
		bool g_editorIDCacheInitialized = false;
	}

	namespace imgui
	{
		bool HoverNote(const char* text, const char* note)
		{
			return ImGui::HoverNote(text, note);
		}

	}

	void InitializeFormEditorIDCache()
	{
		if (g_editorIDCacheInitialized) {
			return;
		}
		g_editorIDCacheInitialized = true;

		if (const auto tweaks = ::GetModuleHandleW(L"po3_Tweaks.dll")) {
			g_po3GetFormEditorID = reinterpret_cast<_GetFormEditorID>(::GetProcAddress(tweaks, "GetFormEditorID"));
		}

		std::size_t weatherCount = 0;
		std::size_t regionCount = 0;
		const auto& [map, lock] = RE::TESForm::GetAllFormsByEditorID();
		[[maybe_unused]] const RE::BSReadLockGuard guard{ lock };
		if (map) {
			g_nativeEditorIDs.reserve(map->size());
			for (const auto& [editorID, form] : *map) {
				if (!form || editorID.empty()) {
					continue;
				}

				g_nativeEditorIDs.try_emplace(form->GetFormID(), editorID.c_str());
				if (form->GetFormType() == RE::FormType::Weather) {
					++weatherCount;
				} else if (form->GetFormType() == RE::FormType::Region) {
					++regionCount;
				}
			}
		}

		logger::warn(
			"EditorID cache diagnostics: po3 export={}, native entries={}, native weather={}, native region={}"sv,
			g_po3GetFormEditorID ? "available" : "unavailable",
			g_nativeEditorIDs.size(),
			weatherCount,
			regionCount);
	}

	bool IsFormEditorIDCacheInitialized() noexcept
	{
		return g_editorIDCacheInitialized;
	}

	std::string getFormEditorID(const RE::TESForm* a_form, EditorIDSource* a_source)
	{
		auto setSource = [a_source](EditorIDSource a_value) {
			if (a_source) {
				*a_source = a_value;
			}
		};

		if (!a_form) {
			setSource(EditorIDSource::Unavailable);
			return {};
		}

		if (const auto editorID = a_form->GetFormEditorID(); editorID && *editorID) {
			setSource(EditorIDSource::Direct);
			return editorID;
		}

		if (g_po3GetFormEditorID) {
			if (const auto editorID = g_po3GetFormEditorID(a_form->GetFormID()); editorID && *editorID) {
				setSource(EditorIDSource::Po3Tweaks);
				return editorID;
			}
		}

		if (const auto it = g_nativeEditorIDs.find(a_form->GetFormID()); it != g_nativeEditorIDs.end() && !it->second.empty()) {
			setSource(EditorIDSource::NativeMap);
			return it->second;
		}

		setSource(EditorIDSource::Unavailable);
		return {};
	}

	std::string_view GetEditorIDSourceName(EditorIDSource a_source) noexcept
	{
		switch (a_source) {
		case EditorIDSource::Direct:
			return "direct"sv;
		case EditorIDSource::Po3Tweaks:
			return "po3"sv;
		case EditorIDSource::NativeMap:
			return "native-map"sv;
		default:
			return "hex-fallback"sv;
		}
	}

	int SliderStepIndex(float a_value, float a_min, float a_step)
	{
		return int((a_value - a_min) / a_step);
	}

	float SliderValueAtStep(int a_index, float a_min, float a_step)
	{
		return a_min + float(a_index) * a_step;
	}


}

settingsLoader::settingsLoader(const char* settingsFile)
{
	_ini.LoadFile(settingsFile);
	if (_ini.IsEmpty()) {
		logger::info("Warning: {} is empty.", settingsFile);
	}
	_settingsFile = settingsFile;
}

settingsLoader::~settingsLoader()
{
	log();
}


/*Set the active section. Load() will load keys from this section.*/

void settingsLoader::setActiveSection(const char* section)
{
	_section = section;
}

/*Load a boolean value if present.*/

void settingsLoader::load(bool& settingRef, const char* key)
{
	if (_ini.GetValue(_section, key)) {
		bool val = _ini.GetBoolValue(_section, key);
		settingRef = val;
		_loadedSettings++;
	}
}

/*Load a float value if present.*/

void settingsLoader::load(float& settingRef, const char* key)
{
	if (_ini.GetValue(_section, key)) {
		float val = static_cast<float>(_ini.GetDoubleValue(_section, key));
		settingRef = val;
		_loadedSettings++;
	}
}

/*Load an unsigned int value if present.*/

void settingsLoader::load(uint32_t& settingRef, const char* key)
{
	if (_ini.GetValue(_section, key)) {
		uint32_t val = static_cast<uint32_t>(_ini.GetDoubleValue(_section, key));
		settingRef = val;
		_loadedSettings++;
	}
}

void settingsLoader::load(std::string& settingRef, const char* key)
{
	if (const char* value = _ini.GetValue(_section, key)) {
		settingRef = value;
		_loadedSettings++;
	}
}

void settingsLoader::save(bool& settingRef, const char* key)
{
	if (settingRef) {
		_ini.SetValue(_section, key, "true");
	} else {
		_ini.SetValue(_section, key, "false");
	}
	_savedSettings++;
}

void settingsLoader::save(float& settingRef, const char* key)
{
	_ini.SetValue(_section, key, std::to_string(settingRef).data());
	_savedSettings++;
}

void settingsLoader::save(uint32_t& settingRef, const char* key)
{
	_ini.SetValue(_section, key, std::to_string(settingRef).data());
	_savedSettings++;
}

/*Load an integer value if present.*/

void settingsLoader::load(int& settingRef, const char* key)
{
	if (_ini.GetValue(_section, key)) {
		int val = static_cast<int>(_ini.GetDoubleValue(_section, key));
		settingRef = val;
		_loadedSettings++;
	}
}

void settingsLoader::flush()
{
	// Ensure parent directory exists before saving
	std::filesystem::path filePath(_settingsFile);
	std::filesystem::path parentDir = filePath.parent_path();
	if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
		std::error_code ec;
		std::filesystem::create_directories(parentDir, ec);
		if (ec) {
			logger::error("Failed to create directory {}: {}", parentDir.string(), ec.message());
		}
	}
	_ini.SaveFile(_settingsFile);
}

namespace ImGui
{
	bool SliderFloatWithSteps(const char* label, float* v, float v_min, float v_max, float v_step)
	{
		char text_buf[64] = {};
		ImFormatString(text_buf, IM_ARRAYSIZE(text_buf), "%g", *v);

		// Map from [v_min,v_max] to [0,N]
		const int countValues = Utils::SliderStepIndex(v_max, v_min, v_step);
		int v_i = Utils::SliderStepIndex(*v, v_min, v_step);
		const bool value_changed = SliderInt(label, &v_i, 0, countValues, text_buf);

		// Remap from [0,N] to [v_min,v_max]
		*v = Utils::SliderValueAtStep(v_i, v_min, v_step);
		return value_changed;
	}

	bool HoverNoteIcon(const char* note, const ImVec4* color)
	{
		const ImVec4 defaultColor(0.5f, 0.5f, 0.5f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, color ? *color : defaultColor);
		const char* note_text = note ? note : "(?)";
		ImGui::TextUnformatted(note_text);
		ImGui::PopStyleColor();

		return ImGui::IsItemHovered();
	}

	void ShowSimpleTooltip(const char* text)
	{
		if (text == nullptr || text[0] == '\0') {
			return;
		}

		ImGui::BeginTooltip();
		ImGui::TextUnformatted(text);
		ImGui::EndTooltip();
	}

	bool HoverNote(const char* text, const char* note)
	{
		const bool hovered = HoverNoteIcon(note, nullptr);
		if (hovered) {
			ShowSimpleTooltip(text);
		}
		return hovered;
	}


	bool ToggleButton(const char* str_id, bool* v)
	{
		bool ret = false;
		ImVec2 p = ImGui::GetCursorScreenPos();
		ImDrawList* draw_list = ImGui::GetWindowDrawList();

		float height = ImGui::GetFrameHeight();
		float width = height * 1.55f;
		float radius = height * 0.50f;

		if (ImGui::InvisibleButton(str_id, ImVec2(width, height))) {
			*v = !*v;
			ret = true;
		}

		const ImRect bb = ImRect(p, ImVec2(p.x + width, p.y + height));
		ImGui::RenderNavHighlight(bb, ImGui::GetItemID());

		float t = *v ? 1.0f : 0.0f;

		ImGuiContext& g = *GImGui;
		float ANIM_SPEED = 0.08f;
		if (g.LastActiveId == g.CurrentWindow->GetID(str_id))  // && g.LastActiveIdTimer < ANIM_SPEED)
		{
			float t_anim = ImSaturate(g.LastActiveIdTimer / ANIM_SPEED);
			t = *v ? (t_anim) : (1.0f - t_anim);
		}

		ImU32 col_bg;
		if (ImGui::IsItemHovered())
			col_bg = ImGui::GetColorU32(ImLerp(ImVec4(0.78f, 0.78f, 0.78f, 1.0f), ImVec4(0.64f, 0.83f, 0.34f, 1.0f), t));
		else
			col_bg = ImGui::GetColorU32(ImLerp(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), ImVec4(0.56f, 0.83f, 0.26f, 1.0f), t));

		draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, height * 0.5f);
		draw_list->AddCircleFilled(ImVec2(p.x + radius + t * (width - radius * 2.0f), p.y + radius), radius - 1.5f, IM_COL32(255, 255, 255, 255));

		return ret;
	}

	bool InputTextRequired(const char* label, std::string* str, ImGuiInputTextFlags flags) 
	{
		bool empty = str->empty();
		if (empty) {
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 0.0f, 0.0f, 0.2f));
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1.0f, 0.0f, 0.0f, 0.2f));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.0f, 0.0f, 0.0f, 0.2f));
		}
		bool ret = IMEWidgets::InputText(label, str, flags);
		if (empty) {
			ImGui::PopStyleColor(3);
		}
		return ret;
	}

	bool InputTextWithPaste(const char* label, std::string& text, const ImVec2& size, bool multiline, ImGuiInputTextFlags flags)
	{
		ImGui::PushID(&text);
		// Uses imgui_stdlib overloads; native Ctrl+C/V/X/Z/A shortcuts are handled by ImGui.
		bool result = multiline ?
			IMEWidgets::InputTextMultiline(label, &text, size, flags) :
			IMEWidgets::InputText(label, &text, flags);
		ImGui::PopID();
		return result;
	}

	bool InputTextWithPasteRequired(const char* label, std::string& text, const ImVec2& size, bool multiline, ImGuiInputTextFlags flags)
	{
		ImGui::PushID(&text);
		bool empty = text.empty();
		if (empty) {
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 0.0f, 0.0f, 0.2f));
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1.0f, 0.0f, 0.0f, 0.2f));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.0f, 0.0f, 0.0f, 0.2f));
		}
		bool result = multiline ?
			IMEWidgets::InputTextMultiline(label, &text, size, flags) :
			IMEWidgets::InputText(label, &text, flags);
		if (empty) {
			ImGui::PopStyleColor(3);
		}
		ImGui::PopID();
		return result;
	}
}
