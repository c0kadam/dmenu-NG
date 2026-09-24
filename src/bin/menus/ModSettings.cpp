#include "imgui_internal.h"
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include "imgui_stdlib.h"


#include "SimpleIni.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <fstream>
#include "ModSettings.h"

#include "bin/Utils.h"
#include "bin/ime/IMEWidgets.h"
#include "bin/HintMedia.h"
#include "Settings.h"
static RE::GameSettingCollection* gsc = nullptr;
inline bool ModSettings::entry_base::Control::Req::satisfied()
{
	bool val = false;
	if (type == kReqType_Checkbox) {
		auto it = ModSettings::m_checkbox_toggle.find(id);
		if (it == ModSettings::m_checkbox_toggle.end()) {
			return false;
		}
		val = it->second->value;
	} else if (type == kReqType_GameSetting) {
		if (!gsc) {
			gsc = RE::GameSettingCollection::GetSingleton();
			if (!gsc)
				return false;
		}
		auto setting = gsc->GetSetting(id.c_str());
		if (!setting) 
			return false;
		val = setting->GetBool();
	}
	return this->_not ? !val : val;
}

inline bool ModSettings::entry_base::Control::satisfied()
{
	for (auto& req : reqs) {
		if (!req.satisfied()) {
			return false;
		}
	}
	return true;
}


using json = nlohmann::json;

namespace
{
	static constexpr uint32_t kMouseLeftInput = 256;
	static constexpr uint32_t kEnterInput = 28;
	static constexpr uint32_t kSpaceInput = 57;
	static constexpr uint32_t kNumEnterInput = 156;
	static constexpr uint32_t kGamepadAInput = 276;

	static const ModSettings::entry_base* g_hintFocusedEntryLastFrame = nullptr;
	static const ModSettings::entry_base* g_hintFocusedEntryThisFrame = nullptr;
	static const ModSettings::entry_base* g_hintToggledEntry = nullptr;

	static const ModSettings::entry_base* g_pinnedMediaHintEntry = nullptr;
	static ImVec2 g_pinnedMediaHintPos(0.0f, 0.0f);
	static ImVec2 g_pinnedMediaAnchor(0.0f, 0.0f);
	static double g_pinnedMediaCloseAt = 0.0;
	static std::set<uint32_t> g_keyMapIgnoredInputs;
	static std::set<uint32_t> g_inputsCurrentlyDown;
	static std::optional<uint32_t> g_mostRecentInputDown;
	static std::optional<uint32_t> g_externalKeyMapIgnoredInput;
	static int g_keyMapIgnoreUntilFrame = -1;
	static ModSettings::mod_setting* g_keyMapCaptureMod = nullptr;
	static bool g_keyMapCaptureCommitsImmediately = false;

	void ClearPinnedMediaHint()
	{
		g_pinnedMediaHintEntry = nullptr;
		g_pinnedMediaCloseAt = 0.0;
	}

	void BeginKeyMapCapture(ModSettings::setting_keymap* keymap)
	{
		ModSettings::keyMapListening = keymap;
		g_keyMapCaptureMod = nullptr;
		g_keyMapCaptureCommitsImmediately = false;
		g_externalKeyMapIgnoredInput.reset();
		g_keyMapIgnoredInputs.clear();

		if (ImGuiContext* ctx = ImGui::GetCurrentContext()) {
			if (ctx->NavInputSource == ImGuiInputSource_Gamepad) {
				g_keyMapIgnoredInputs.insert(kGamepadAInput);
			} else if (ctx->NavInputSource == ImGuiInputSource_Keyboard) {
				g_keyMapIgnoredInputs.insert(kEnterInput);
				g_keyMapIgnoredInputs.insert(kNumEnterInput);
				g_keyMapIgnoredInputs.insert(kSpaceInput);
			}
		}

		if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			g_keyMapIgnoredInputs.insert(kMouseLeftInput);
		}
		if (ImGui::IsKeyDown(ImGuiKey_Enter) || ImGui::IsKeyReleased(ImGuiKey_Enter)) {
			g_keyMapIgnoredInputs.insert(kEnterInput);
		}
		if (ImGui::IsKeyDown(ImGuiKey_KeypadEnter) || ImGui::IsKeyReleased(ImGuiKey_KeypadEnter)) {
			g_keyMapIgnoredInputs.insert(kNumEnterInput);
		}
		if (ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyReleased(ImGuiKey_Space)) {
			g_keyMapIgnoredInputs.insert(kSpaceInput);
		}
		if (ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown) || ImGui::IsKeyReleased(ImGuiKey_GamepadFaceDown)) {
			g_keyMapIgnoredInputs.insert(kGamepadAInput);
		}

		g_keyMapIgnoreUntilFrame = ImGui::GetFrameCount() + 2;
	}

	bool BeginExternalKeyMapCapture(ModSettings::mod_setting* mod, ModSettings::setting_keymap* keymap)
	{
		if (!mod || !keymap || ModSettings::keyMapListening != nullptr) {
			return false;
		}

		ModSettings::keyMapListening = keymap;
		g_keyMapCaptureMod = mod;
		g_keyMapCaptureCommitsImmediately = true;
		g_keyMapIgnoredInputs.clear();
		g_externalKeyMapIgnoredInput.reset();
		if (g_mostRecentInputDown && g_inputsCurrentlyDown.contains(*g_mostRecentInputDown)) {
			g_externalKeyMapIgnoredInput = g_mostRecentInputDown;
			g_keyMapIgnoredInputs.insert(*g_externalKeyMapIgnoredInput);
		}
		g_keyMapIgnoreUntilFrame = -1;
		return true;
	}

	void ClearKeyMapCapture()
	{
		ModSettings::keyMapListening = nullptr;
		g_keyMapCaptureMod = nullptr;
		g_keyMapCaptureCommitsImmediately = false;
		g_externalKeyMapIgnoredInput.reset();
		g_keyMapIgnoredInputs.clear();
		g_keyMapIgnoreUntilFrame = -1;
	}

	void RefreshIgnoredKeyMapInputs()
	{
		if (g_keyMapCaptureCommitsImmediately) {
			return;
		}

		if (g_keyMapIgnoreUntilFrame >= 0 && ImGui::GetFrameCount() > g_keyMapIgnoreUntilFrame) {
			g_keyMapIgnoredInputs.clear();
			g_keyMapIgnoreUntilFrame = -1;
		}
	}

	float GetModSettingsFooterHeight()
	{
		// Taskbar-like footer strip at the bottom of Mod Configuration tab.
		const float singleRowHeight = ImGui::GetFrameHeight() + 20.0f;
		const float compactHintHeight = ImGui::GetTextLineHeightWithSpacing() * 2.0f + 18.0f;
		return (std::max)(singleRowHeight, compactHintHeight);
	}

	bool IsItemHoveredOrFocused()
	{
		return ImGui::IsItemHovered() || ImGui::IsItemFocused();
	}

	bool DrawManualCollapsibleHeader(const char* id, const char* label, bool expanded)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float height = ImGui::GetFrameHeight();
		const ImVec2 size(ImGui::GetContentRegionAvail().x, height);
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
		const bool pressed = ImGui::Selectable(id, expanded, ImGuiSelectableFlags_SpanAvailWidth, size);
		ImGui::PopStyleColor(3);

		const ImRect bb(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		const bool focused = ImGui::IsItemFocused();

		ImU32 bgColor = 0;
		if (active) {
			bgColor = ImGui::GetColorU32(ImGuiCol_HeaderActive);
		} else if (hovered || focused) {
			bgColor = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
		} else if (expanded) {
			bgColor = ImGui::GetColorU32(ImGuiCol_Header);
		} else {
			ImVec4 frameBg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
			frameBg.w = (std::max)(frameBg.w, 0.35f);
			bgColor = ImGui::GetColorU32(frameBg);
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImGui::RenderNavHighlight(bb, ImGui::GetItemID(), ImGuiNavRenderCursorFlags_Compact);
		ImGui::RenderFrame(bb.Min, bb.Max, bgColor, true, style.FrameRounding);
		drawList->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding);

		const float arrowScale = 0.80f;
		const float arrowSize = ImGui::GetFontSize() * arrowScale;
		const ImVec2 arrowPos(
			bb.Min.x + style.FramePadding.x,
			bb.Min.y + (bb.GetHeight() - arrowSize) * 0.5f);
		ImGui::RenderArrow(
			drawList,
			arrowPos,
			ImGui::GetColorU32(ImGuiCol_Text),
			expanded ? ImGuiDir_Down : ImGuiDir_Right,
			arrowScale);

		const float textOffsetX = style.FramePadding.x * 2.0f + ImGui::GetFontSize();
		const ImVec2 textPos(
			bb.Min.x + textOffsetX,
			bb.Min.y + (bb.GetHeight() - ImGui::GetTextLineHeight()) * 0.5f);
		drawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), label);

		return pressed;
	}

	std::string ToLowerASCII(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return value;
	}

	std::string ResolveHintPath(const std::string& rawPath)
	{
		if (rawPath.empty()) {
			return {};
		}

		std::error_code ec;
		const std::filesystem::path source(rawPath);
		std::filesystem::path absPath = source.is_absolute() ? source : std::filesystem::absolute(source, ec);
		if (ec) {
			absPath = source;
			ec.clear();
		}

		std::filesystem::path normalized = std::filesystem::weakly_canonical(absPath, ec);
		if (ec) {
			normalized = absPath.lexically_normal();
		}

		return normalized.string();
	}

	std::string ToBackslashPath(const std::filesystem::path& pathValue)
	{
		std::string text = pathValue.string();
		std::replace(text.begin(), text.end(), '/', '\\');
		return text;
	}

	std::string ToDataRelativePathIfPossible(const std::filesystem::path& pathValue)
	{
		std::error_code ec;
		std::filesystem::path normalized = std::filesystem::weakly_canonical(pathValue, ec);
		if (ec) {
			normalized = pathValue.lexically_normal();
		}

		std::string backslashPath = ToBackslashPath(normalized);
		if (backslashPath.empty()) {
			return backslashPath;
		}

		std::string lowerPath = ToLowerASCII(backslashPath);
		if (lowerPath == "data" || lowerPath.rfind("data\\", 0) == 0) {
			return backslashPath;
		}

		const std::string token = "\\data\\";
		const std::size_t tokenPos = lowerPath.find(token);
		if (tokenPos != std::string::npos) {
			return "Data\\" + backslashPath.substr(tokenPos + token.size());
		}

		if (lowerPath.size() >= 5 && lowerPath.compare(lowerPath.size() - 5, 5, "\\data") == 0) {
			return "Data";
		}

		// MO2/Vortex gibi ortamlarda mutlak yol "Data" segmenti içermeyebilir.
		// Bu durumda bilinen Data kök alt klasörlerini anchor alıp taşınabilir "Data\\..." üret.
		static constexpr const char* DATA_ANCHORS[] = {
			"\\skse\\",
			"\\meshes\\",
			"\\textures\\",
			"\\interface\\",
			"\\scripts\\",
			"\\sound\\",
			"\\music\\",
			"\\strings\\",
			"\\video\\",
			"\\seq\\",
			"\\lodsettings\\",
			"\\mcm\\"
		};
		for (const char* anchor : DATA_ANCHORS) {
			const std::string anchorText(anchor);
			const std::size_t anchorPos = lowerPath.find(anchorText);
			if (anchorPos == std::string::npos) {
				continue;
			}
			const std::size_t relativeStart = backslashPath[anchorPos] == '\\' ? anchorPos + 1 : anchorPos;
			if (relativeStart < backslashPath.size()) {
				return "Data\\" + backslashPath.substr(relativeStart);
			}
		}

		return backslashPath;
	}

	std::filesystem::path ResolvePickerBasePath(const std::string& rawPath)
	{
		if (rawPath.empty()) {
			return {};
		}

		std::string normalized = rawPath;
		std::replace(normalized.begin(), normalized.end(), '/', '\\');
		const std::string lowered = ToLowerASCII(normalized);
		if (lowered == "data" || lowered.rfind("data\\", 0) == 0) {
			std::string tail = normalized.size() > 4 ? normalized.substr(4) : "";
			while (!tail.empty() && (tail.front() == '\\' || tail.front() == '/')) {
				tail.erase(tail.begin());
			}
			return std::filesystem::absolute(std::filesystem::path("Data") / std::filesystem::path(tail));
		}

		std::filesystem::path p(normalized);
		return p.is_absolute() ? p : std::filesystem::absolute(p);
	}

	bool IsFlipbookFramePath(const std::filesystem::path& pathValue)
	{
		const std::string ext = ToLowerASCII(pathValue.extension().string());
		return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tif" || ext == ".tiff";
	}

	bool IsMediaTypeFileMatch(const std::filesystem::path& pathValue, ModSettings::entry_base::HintMediaConfig::Type type)
	{
		const std::string ext = ToLowerASCII(pathValue.extension().string());
		switch (type) {
		case ModSettings::entry_base::HintMediaConfig::Type::Gif:
			return ext == ".gif";
		case ModSettings::entry_base::HintMediaConfig::Type::Webp:
			return ext == ".webp";
		case ModSettings::entry_base::HintMediaConfig::Type::Webm:
			return ext == ".webm";
		case ModSettings::entry_base::HintMediaConfig::Type::Flipbook:
		default:
			return IsFlipbookFramePath(pathValue);
		}
	}

	bool IsDataRelativePathText(const std::string& rawPath)
	{
		if (rawPath.empty()) {
			return false;
		}
		std::string normalized = rawPath;
		std::replace(normalized.begin(), normalized.end(), '/', '\\');
		const std::string lowered = ToLowerASCII(normalized);
		return lowered == "data" || lowered.rfind("data\\", 0) == 0;
	}

	void SyncHintMediaPathAndCache(ModSettings::entry_base::HintMediaConfig& media)
	{
		if (media.path.empty()) {
			media.resolved_path.clear();
			media.cacheKey.clear();
			return;
		}

		media.path = ToDataRelativePathIfPossible(std::filesystem::path(media.path));
		media.resolved_path = ResolveHintPath(media.path);
		// Keep cache key deterministic and portable by tracking the stored path text.
		media.cacheKey = media.path;
	}

	std::vector<std::string> BuildHintMediaCandidates(
		const std::filesystem::path& basePath,
		ModSettings::entry_base::HintMediaConfig::Type type)
	{
		std::vector<std::string> candidates;
		std::error_code ec;
		if (!std::filesystem::exists(basePath, ec) || ec || !std::filesystem::is_directory(basePath, ec) || ec) {
			return candidates;
		}

		std::set<std::string> uniqueCandidates;
		for (std::filesystem::recursive_directory_iterator it(basePath, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
			if (ec) {
				break;
			}

			const auto& p = it->path();
			if (type == ModSettings::entry_base::HintMediaConfig::Type::Flipbook) {
				if (!it->is_regular_file(ec) || ec || !IsFlipbookFramePath(p)) {
					continue;
				}
				uniqueCandidates.insert(ToDataRelativePathIfPossible(p.parent_path()));
			} else {
				if (!it->is_regular_file(ec) || ec || !IsMediaTypeFileMatch(p, type)) {
					continue;
				}
				uniqueCandidates.insert(ToDataRelativePathIfPossible(p));
			}
		}

		candidates.assign(uniqueCandidates.begin(), uniqueCandidates.end());
		return candidates;
	}

	bool ContainsCaseInsensitive(const std::string& value, const std::string& needle)
	{
		if (needle.empty()) {
			return true;
		}
		return ToLowerASCII(value).find(ToLowerASCII(needle)) != std::string::npos;
	}

	std::string GetBindingDisplayName(uint32_t inputCode)
	{
		if (inputCode == 0) {
			return TR("key_unmapped", "Unmapped");
		}

		return ModSettings::setting_keymap::keyid_to_str(static_cast<int>(inputCode));
	}

	std::string BuildBindingChord(uint32_t primaryInput, uint32_t modifierInput)
	{
		if (primaryInput == 0) {
			return TR("key_unmapped", "Unmapped");
		}

		if (modifierInput == 0) {
			return GetBindingDisplayName(primaryInput);
		}

		return fmt::format(
			fmt::runtime(TR("modsettings_footer_combo_fmt", "{} + {}")),
			GetBindingDisplayName(modifierInput),
			GetBindingDisplayName(primaryInput));
	}

	struct FooterHighlightToken
	{
		std::string text;
		ImVec4 color;
	};

	void DrawFooterTextSegment(const char* text, const ImVec4* color, bool continueLine)
	{
		if (text == nullptr || text[0] == '\0') {
			return;
		}

		if (continueLine) {
			ImGui::SameLine(0.0f, 0.0f);
		}

		if (color != nullptr) {
			ImGui::TextColored(*color, "%s", text);
		} else {
			ImGui::TextUnformatted(text);
		}
	}

	void DrawFooterFormattedText(const char* formatText, std::initializer_list<FooterHighlightToken> highlights)
	{
		const std::string format = formatText ? formatText : "";
		auto tokenIt = highlights.begin();
		std::size_t cursor = 0;
		bool continueLine = false;

		while (cursor <= format.size()) {
			const std::size_t placeholderPos = format.find("{}", cursor);
			const std::size_t segmentLength =
				placeholderPos == std::string::npos ? format.size() - cursor : placeholderPos - cursor;
			const std::string segment = format.substr(cursor, segmentLength);
			if (!segment.empty()) {
				DrawFooterTextSegment(segment.c_str(), nullptr, continueLine);
				continueLine = true;
			}

			if (placeholderPos == std::string::npos) {
				break;
			}

			if (tokenIt != highlights.end()) {
				DrawFooterTextSegment(tokenIt->text.c_str(), &tokenIt->color, continueLine);
				continueLine = true;
				++tokenIt;
			}

			cursor = placeholderPos + 2;
		}
	}

	void ShowModSettingsFooterHints(float availableWidth)
	{
		if (availableWidth < 180.0f) {
			return;
		}

		const ImVec4 helpTextColor(0.90f, 0.90f, 0.90f, 1.0f);
		const ImVec4 noteColor(1.00f, 0.80f, 0.25f, 1.0f);
		const ImVec4 mkbBindingColor(0.45f, 0.82f, 1.00f, 1.0f);
		const ImVec4 gamepadBindingColor(0.58f, 0.92f, 0.46f, 1.0f);
		const bool hasHintBinding = Settings::key_toggle_hints_gamepad != 0;
		const bool hasMkbBinding = Settings::key_toggle_dmenu_mkb != 0;
		const bool hasGamepadBinding = Settings::key_toggle_dmenu_gamepad != 0;

		ImGui::BeginChild(
			"##modsettings_footer_hints",
			ImVec2(availableWidth, 0.0f),
			false,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, helpTextColor);

		ImGui::TextUnformatted(TR("modsettings_footer_hint_prefix", "Hint: click"));
		ImGui::SameLine(0.0f, 4.0f);
		ImGui::TextColored(noteColor, "(?)");
		ImGui::SameLine(0.0f, 4.0f);
		if (hasHintBinding) {
			DrawFooterFormattedText(
				TR("modsettings_footer_hint_suffix", "or press {}"),
				{ FooterHighlightToken{ GetBindingDisplayName(Settings::key_toggle_hints_gamepad), gamepadBindingColor } });
		} else {
			ImGui::TextUnformatted(TR("modsettings_footer_hint_click_only", "for help"));
		}

		if (hasMkbBinding && hasGamepadBinding) {
			DrawFooterFormattedText(
				TR("modsettings_footer_toggle_both", "dMenu: MKB {} | Pad {}"),
				{
					FooterHighlightToken{ BuildBindingChord(Settings::key_toggle_dmenu_mkb, Settings::key_toggle_modifier_mkb), mkbBindingColor },
					FooterHighlightToken{ BuildBindingChord(Settings::key_toggle_dmenu_gamepad, Settings::key_toggle_modifier_gamepad), gamepadBindingColor }
				});
		} else if (hasMkbBinding) {
			DrawFooterFormattedText(
				TR("modsettings_footer_toggle_mkb", "dMenu: MKB {}"),
				{ FooterHighlightToken{ BuildBindingChord(Settings::key_toggle_dmenu_mkb, Settings::key_toggle_modifier_mkb), mkbBindingColor } });
		} else if (hasGamepadBinding) {
			DrawFooterFormattedText(
				TR("modsettings_footer_toggle_gamepad", "dMenu: Pad {}"),
				{ FooterHighlightToken{ BuildBindingChord(Settings::key_toggle_dmenu_gamepad, Settings::key_toggle_modifier_gamepad), gamepadBindingColor } });
		} else {
			ImGui::TextUnformatted(TR("modsettings_footer_toggle_none", "dMenu: Unmapped"));
		}

		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		ImGui::EndChild();
	}

	struct HintPickerState
	{
		std::string basePath = "Data\\SKSE\\Plugins\\dMenu\\hints";
		std::string filter;
		std::vector<std::string> candidates;
		ModSettings::entry_base::HintMediaConfig::Type mediaType = ModSettings::entry_base::HintMediaConfig::Type::Flipbook;
		bool loaded = false;
	};

	HintPickerState& GetHintPickerState()
	{
		static HintPickerState state;
		return state;
	}

	ModSettings::entry_base::HintConfig::ShowOn ParseHintShowOn(const nlohmann::json& hintJson)
	{
		const std::string showOn = ToLowerASCII(hintJson.value("showOn", "note"));
		if (showOn == "control") {
			return ModSettings::entry_base::HintConfig::ShowOn::Control;
		}
		if (showOn == "both") {
			return ModSettings::entry_base::HintConfig::ShowOn::Both;
		}
		return ModSettings::entry_base::HintConfig::ShowOn::Note;
	}

	std::optional<ModSettings::entry_base::HintMediaConfig::Type> ParseHintMediaType(const nlohmann::json& mediaJson)
	{
		const std::string type = ToLowerASCII(mediaJson.value("type", "flipbook"));
		if (type == "flipbook") {
			return ModSettings::entry_base::HintMediaConfig::Type::Flipbook;
		}
		if (type == "gif") {
			return ModSettings::entry_base::HintMediaConfig::Type::Gif;
		}
		if (type == "webp") {
			return ModSettings::entry_base::HintMediaConfig::Type::Webp;
		}
		if (type == "webm") {
			return ModSettings::entry_base::HintMediaConfig::Type::Webm;
		}
		return std::nullopt;
	}

	void ParseHintConfig(const nlohmann::json& entryJson, ModSettings::entry_base* entry)
	{
		if (!entryJson.contains("hint") || !entryJson["hint"].is_object()) {
			return;
		}

		const auto& hintJson = entryJson["hint"];
		ModSettings::entry_base::HintConfig hintConfig;
		hintConfig.showOn = ParseHintShowOn(hintJson);

		if (hintJson.contains("media") && hintJson["media"].is_object()) {
			const auto& mediaJson = hintJson["media"];
			auto mediaType = ParseHintMediaType(mediaJson);
			if (mediaType.has_value()) {
				ModSettings::entry_base::HintMediaConfig mediaConfig;
				mediaConfig.type = *mediaType;
				mediaConfig.path = mediaJson.value("path", "");
				mediaConfig.resolved_path = ResolveHintPath(mediaConfig.path);
				mediaConfig.cacheKey = mediaJson.value("cacheKey", "");
				if (mediaConfig.cacheKey.empty()) {
					mediaConfig.cacheKey = mediaConfig.path;
				}
				mediaConfig.fps = (std::clamp)(mediaJson.value("fps", 24), 1, 120);
				mediaConfig.maxW = (std::clamp)(mediaJson.value("maxW", 420), 32, 1024);
				mediaConfig.maxH = (std::clamp)(mediaJson.value("maxH", 240), 32, 1024);
				mediaConfig.loop = mediaJson.value("loop", true);
				mediaConfig.preload = mediaJson.value("preload", false);

				if (!mediaConfig.path.empty()) {
					hintConfig.media = std::move(mediaConfig);
				}
			}
		}

		entry->hint = std::move(hintConfig);
	}

	const char* HintShowOnToString(ModSettings::entry_base::HintConfig::ShowOn showOn)
	{
		switch (showOn) {
		case ModSettings::entry_base::HintConfig::ShowOn::Control:
			return "control";
		case ModSettings::entry_base::HintConfig::ShowOn::Both:
			return "both";
		case ModSettings::entry_base::HintConfig::ShowOn::Note:
		default:
			return "note";
		}
	}

	int HintShowOnToIndex(ModSettings::entry_base::HintConfig::ShowOn showOn)
	{
		switch (showOn) {
		case ModSettings::entry_base::HintConfig::ShowOn::Control:
			return 1;
		case ModSettings::entry_base::HintConfig::ShowOn::Both:
			return 2;
		case ModSettings::entry_base::HintConfig::ShowOn::Note:
		default:
			return 0;
		}
	}

	ModSettings::entry_base::HintConfig::ShowOn HintShowOnFromIndex(int index)
	{
		switch (index) {
		case 1:
			return ModSettings::entry_base::HintConfig::ShowOn::Control;
		case 2:
			return ModSettings::entry_base::HintConfig::ShowOn::Both;
		case 0:
		default:
			return ModSettings::entry_base::HintConfig::ShowOn::Note;
		}
	}

	const char* HintMediaTypeToString(ModSettings::entry_base::HintMediaConfig::Type mediaType)
	{
		switch (mediaType) {
		case ModSettings::entry_base::HintMediaConfig::Type::Gif:
			return "gif";
		case ModSettings::entry_base::HintMediaConfig::Type::Webp:
			return "webp";
		case ModSettings::entry_base::HintMediaConfig::Type::Webm:
			return "webm";
		case ModSettings::entry_base::HintMediaConfig::Type::Flipbook:
		default:
			return "flipbook";
		}
	}

	int HintMediaTypeToIndex(ModSettings::entry_base::HintMediaConfig::Type mediaType)
	{
		switch (mediaType) {
		case ModSettings::entry_base::HintMediaConfig::Type::Flipbook:
			return 0;
		case ModSettings::entry_base::HintMediaConfig::Type::Gif:
			return 1;
		case ModSettings::entry_base::HintMediaConfig::Type::Webp:
			return 2;
		case ModSettings::entry_base::HintMediaConfig::Type::Webm:
			return 3;
		default:
			return 0;
		}
	}

	ModSettings::entry_base::HintMediaConfig::Type HintMediaTypeFromIndex(int index)
	{
		switch (index) {
		case 1:
			return ModSettings::entry_base::HintMediaConfig::Type::Gif;
		case 2:
			return ModSettings::entry_base::HintMediaConfig::Type::Webp;
		case 3:
			return ModSettings::entry_base::HintMediaConfig::Type::Webm;
		case 0:
		default:
			return ModSettings::entry_base::HintMediaConfig::Type::Flipbook;
		}
	}

	void PopulateHintJson(const ModSettings::entry_base* entry, nlohmann::json& entryJson)
	{
		if (!entry->hint.has_value()) {
			return;
		}

		nlohmann::json hintJson = nlohmann::json::object();
		hintJson["showOn"] = HintShowOnToString(entry->hint->showOn);

		if (entry->hint->media.has_value()) {
			const auto& media = *entry->hint->media;
			nlohmann::json mediaJson = nlohmann::json::object();
			mediaJson["type"] = HintMediaTypeToString(media.type);
			const std::string portablePath = media.path.empty() ? media.path : ToDataRelativePathIfPossible(std::filesystem::path(media.path));
			mediaJson["path"] = portablePath;
			mediaJson["fps"] = media.fps;
			mediaJson["maxW"] = media.maxW;
			mediaJson["maxH"] = media.maxH;
			mediaJson["loop"] = media.loop;
			mediaJson["preload"] = media.preload;
			if (!media.cacheKey.empty()) {
				mediaJson["cacheKey"] = media.cacheKey;
			}
			hintJson["media"] = std::move(mediaJson);
		}

		entryJson["hint"] = std::move(hintJson);
	}

	bool ShowEntryHintTooltip(ModSettings::entry_base* entry, bool hoveredControl, bool showNoteIcon)
	{
		const ImVec2 controlRectMin = ImGui::GetItemRectMin();
		const ImVec2 controlRectMax = ImGui::GetItemRectMax();

		if (ImGui::IsItemFocused()) {
			g_hintFocusedEntryThisFrame = entry;
		}
		const bool hintToggledForEntry = (g_hintToggledEntry == entry);

		const bool hasDesc = !entry->desc.empty();
		const bool hasHintConfig = entry->hint.has_value();
		const bool hasAnyHint = hasDesc || hasHintConfig;
		const bool hasMedia = hasHintConfig && entry->hint->media.has_value();
		bool hoveredNote = false;
		bool clickedNote = false;

		if (showNoteIcon && hasAnyHint) {
			ImGui::SameLine();
			const ImVec4 mediaNoteColor(1.00f, 0.80f, 0.25f, 1.00f);
			hoveredNote = ImGui::HoverNoteIcon("(?)", hasMedia ? &mediaNoteColor : nullptr);
			clickedNote = ImGui::IsItemClicked(ImGuiMouseButton_Left);
			if (clickedNote) {
				const ImVec2 rectMin = ImGui::GetItemRectMin();
				const ImVec2 rectMax = ImGui::GetItemRectMax();
				g_pinnedMediaHintPos = ImVec2(rectMax.x + 8.0f, rectMin.y);
				g_pinnedMediaAnchor = ImVec2((rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y) * 0.5f);
				g_pinnedMediaCloseAt = 0.0;
			}
		}

		const bool useClickToOpenMedia = hasMedia && showNoteIcon && Settings::hint_media_click_to_open;
		if (useClickToOpenMedia && clickedNote) {
			if (g_pinnedMediaHintEntry == entry) {
				ClearPinnedMediaHint();
			} else {
				g_pinnedMediaHintEntry = entry;
				g_pinnedMediaCloseAt = 0.0;
			}
		}

		const bool focusedThisFrame = ImGui::IsItemFocused();
		if (focusedThisFrame && !hoveredNote && !clickedNote) {
			g_hintFocusedEntryThisFrame = entry;
		}

		if (g_pinnedMediaHintEntry != nullptr && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			ClearPinnedMediaHint();
		}

		bool forceShowMedia = useClickToOpenMedia && (g_pinnedMediaHintEntry == entry);
		if (forceShowMedia) {
			const ImVec2 mousePos = ImGui::GetMousePos();
			const float hoverRadius = (std::clamp)(Settings::hint_media_click_hover_radius, 8.0f, 480.0f);
			const float dx = mousePos.x - g_pinnedMediaAnchor.x;
			const float dy = mousePos.y - g_pinnedMediaAnchor.y;
			const bool insideRadius = (dx * dx + dy * dy) <= (hoverRadius * hoverRadius);
			const bool shouldKeepOpen = hoveredControl || hoveredNote || insideRadius;

			if (shouldKeepOpen) {
				g_pinnedMediaCloseAt = 0.0;
			} else {
				const float graceSec = (std::clamp)(static_cast<float>(Settings::hint_media_click_close_grace_ms), 0.0f, 1500.0f) / 1000.0f;
				const double now = ImGui::GetTime();
				if (graceSec <= 0.0f) {
					ClearPinnedMediaHint();
					forceShowMedia = false;
				} else if (g_pinnedMediaCloseAt <= 0.0) {
					g_pinnedMediaCloseAt = now + graceSec;
				} else if (now >= g_pinnedMediaCloseAt) {
					ClearPinnedMediaHint();
					forceShowMedia = false;
				}
			}

			if (forceShowMedia && Settings::hint_media_click_follow_mouse) {
				const float offsetX = (std::clamp)(Settings::hint_media_click_follow_offset_x, -640.0f, 640.0f);
				const float offsetY = (std::clamp)(Settings::hint_media_click_follow_offset_y, -640.0f, 640.0f);
				g_pinnedMediaHintPos = ImVec2(mousePos.x + offsetX, mousePos.y + offsetY);
			}
		}

		// While a media hint is pinned on another entry, suppress other hover hints.
		if (g_pinnedMediaHintEntry != nullptr && g_pinnedMediaHintEntry != entry) {
			return false;
		}
		if (forceShowMedia && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			ClearPinnedMediaHint();
			return false;
		}

		const bool mouseInteractingWithEntry = hoveredNote || clickedNote;
		const bool forceByToggle = hintToggledForEntry && !mouseInteractingWithEntry;
		const ImVec2 toggledHintPos(controlRectMax.x + 8.0f, controlRectMin.y);

		if (hasHintConfig) {
			HintMediaManager::Get().TryPreload(*entry);
			bool tooltipHoveredControl = hoveredControl;
			bool tooltipHoveredNote = hoveredNote;
			if (useClickToOpenMedia) {
				// Backup behavior: in click-to-open mode, don't auto-open on hover.
				tooltipHoveredControl = false;
				tooltipHoveredNote = false;
			}

			bool forceTooltip = forceShowMedia;
			const ImVec2* forcedTooltipPos = forceShowMedia ? &g_pinnedMediaHintPos : nullptr;
			if (forceByToggle) {
				forceTooltip = true;
				forcedTooltipPos = &toggledHintPos;
			}

			if (HintMediaManager::Get().DrawHintTooltip(
				    *entry,
				    entry->desc.get(),
				    tooltipHoveredControl,
				    tooltipHoveredNote,
				    forceTooltip,
				    forcedTooltipPos)) {
				return true;
			}
			// If a hint config is present and nothing is drawable, keep behavior silent.
			return false;
		}

		if (forceByToggle && hasDesc) {
			ImGui::SetNextWindowPos(toggledHintPos, ImGuiCond_Always);
			ImGui::ShowSimpleTooltip(entry->desc.get());
			return true;
		}

		if (hasDesc && showNoteIcon && hoveredNote) {
			ImGui::ShowSimpleTooltip(entry->desc.get());
			return true;
		}
		if (hasDesc && !showNoteIcon && hoveredControl) {
			ImGui::ShowSimpleTooltip(entry->desc.get());
			return true;
		}

		return false;
	}
}

void ModSettings::SendAllSettingsUpdateEvent()
{
	for (auto& mod : mods) {
		SendSettingsUpdateEvent(mod->name);
	}
}

void ModSettings::MarkIniDirty(mod_setting* mod)
{
	ini_dirty_mods.insert(mod);
}

void ModSettings::FlushIniDirtyMods()
{
	const std::vector<mod_setting*> dirtyMods(ini_dirty_mods.begin(), ini_dirty_mods.end());
	for (auto* mod : dirtyMods) {
		commit_ini_dirty_mod(mod);
	}
	ini_dirty_mods.clear();
}

bool ModSettings::CommitIniDirtyMod(mod_setting* mod)
{
	if (!mod || ini_dirty_mods.find(mod) == ini_dirty_mods.end()) {
		return false;
	}

	commit_ini_dirty_mod(mod);
	ini_dirty_mods.erase(mod);
	return true;
}

bool ModSettings::CommitIniDirtyPage(const void* a_pageIdentity)
{
	if (!a_pageIdentity) {
		return false;
	}

	for (auto* mod : mods) {
		if (static_cast<const void*>(mod) == a_pageIdentity) {
			return CommitIniDirtyMod(mod);
		}
	}

	return false;
}

void ModSettings::commit_ini_dirty_mod(mod_setting* mod)
{
	flush_ini(mod);
	flush_game_setting(mod);
	for (auto& callback : mod->callbacks) {
		callback();
	}
	SendSettingsUpdateEvent(mod->name);
}

void ModSettings::FlushJsonDirtyMods()
{
	for (auto& mod : json_dirty_mods) {
		flush_json(mod);
	}
	json_dirty_mods.clear();
}
void ModSettings::show_reloadTranslationButton()
{
	if (ImGui::Button(TR("modsettings_reload_translation", "Reload Translation"))) {
		Translator::ReLoadTranslations();
	}
}
  // not used anymore, we auto save.
void ModSettings::show_saveButton()
{
	const bool has_ini_changes = !ini_dirty_mods.empty();
	const bool has_json_changes = edit_mode && !json_dirty_mods.empty();
	bool unsaved_changes = has_ini_changes || has_json_changes;
	
	if (unsaved_changes) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.9f, 0.5f, 1.0f));
    }

    if (ImGui::Button(TR("modsettings_save", "Save"))) {
		if (has_ini_changes) {
			FlushIniDirtyMods();
		}
		if (has_json_changes) {
			FlushJsonDirtyMods();
		}
    }

    if (unsaved_changes) {
        ImGui::PopStyleColor(3);
    }
}

void ModSettings::show_cancelButton()
{
	bool unsaved_changes = !ini_dirty_mods.empty();

	if (ImGui::Button(TR("modsettings_cancel", "Cancel"))) {
		for (auto& mod : ini_dirty_mods) {
			load_ini(mod);
		}
		ini_dirty_mods.clear();
	}
}

void ModSettings::show_saveJsonButton()
{
	bool unsaved_changes = !json_dirty_mods.empty();

	if (unsaved_changes) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.4f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.9f, 0.5f, 1.0f));
	}

	ImGui::PushID("save_config_button");
	if (ImGui::Button(TR("modsettings_save_config", "Save Config"))) {
		FlushJsonDirtyMods();
	}
	ImGui::PopID();

	if (unsaved_changes) {
		ImGui::PopStyleColor(3);
	}
}

void ModSettings::show_buttons_window()
{
	// Render a visible, in-frame footer with compact guidance on the left
	// and action controls anchored on the right.
	const float footerHeight = GetModSettingsFooterHeight();
	const float footerPadX = 10.0f;
	const float footerPadY = 8.0f;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(footerPadX, footerPadY));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.20f, 0.20f, 0.20f, 0.98f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.75f, 0.75f, 0.75f, 0.65f));

	ImGui::BeginChild(
		"##modsettings_footer",
		ImVec2(0, footerHeight),
		ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	const ImGuiStyle& style = ImGui::GetStyle();
	const float spacing = style.ItemSpacing.x;

	auto buttonWidth = [](const char* label) {
		return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	};
	auto checkboxWidth = [](const char* label) {
		return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
	};

	float controlsWidth = 0.0f;
	controlsWidth += buttonWidth(TR("modsettings_save", "Save"));
	controlsWidth += spacing + buttonWidth(TR("modsettings_cancel", "Cancel"));
	controlsWidth += spacing + checkboxWidth(TR("modsettings_auto_save", "Auto Save"));
	const float totalWidth = ImGui::GetContentRegionAvail().x;
	const bool showHints = totalWidth >= controlsWidth + 180.0f + spacing * 2.0f;
	const bool focusPrimaryAction = request_primary_action_focus;
	request_primary_action_focus = false;

	if (ImGui::BeginTable(
			"##modsettings_footer_layout",
			2,
			ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings,
			ImVec2(0.0f, 0.0f))) {
		ImGui::TableSetupColumn("Hints", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, controlsWidth);
		ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetContentRegionAvail().y);

		ImGui::TableSetColumnIndex(0);
		if (showHints) {
			ShowModSettingsFooterHints(ImGui::GetContentRegionAvail().x);
		}

		ImGui::TableSetColumnIndex(1);
		const float contentHeight = ImGui::GetContentRegionAvail().y;
		const float verticalOffset = (std::max)(0.0f, (contentHeight - ImGui::GetFrameHeight()) * 0.5f);
		if (verticalOffset > 0.0f) {
			ImGui::Dummy(ImVec2(0.0f, verticalOffset));
		}

		if (focusPrimaryAction) {
			ImGui::SetKeyboardFocusHere();
		}
		show_saveButton();
		if (focusPrimaryAction) {
			ImGui::SetItemDefaultFocus();
		}
		ImGui::SameLine();
		show_cancelButton();
		ImGui::SameLine();
		ImGui::Checkbox(TR("modsettings_auto_save", "Auto Save"), &auto_save_enabled);

		ImGui::EndTable();
	}

	ImGui::EndChild();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar();
}


void ModSettings::show_entry_edit(entry_base* entry, mod_setting* mod)
{
	ImGui::PushID(entry);

	bool edited = false;

	// Show input fields to edit the setting name and ini id
	if (ImGui::InputTextWithPasteRequired("Name", entry->name.def))
		edited = true;
	if (ImGui::InputTextWithPaste("Description", entry->desc.def, ImVec2(ImGui::GetCurrentWindow()->Size.x * 0.8, ImGui::GetTextLineHeight() * 3), true, ImGuiInputTextFlags_AutoSelectAll))
		edited = true;

	if (entry->type == kEntryType_Group) {
		ImGui::Separator();
		ImGui::TextUnformatted("Group Layout");

		auto* group = dynamic_cast<entry_group*>(entry);
		int layoutModeIndex = group->layout_mode == entry_group::LayoutMode::Grid ? 1 : 0;
		const char* layoutModeLabels[] = { "stack", "grid" };
		if (ImGui::BeginCombo("Layout Mode", layoutModeLabels[layoutModeIndex])) {
			for (int i = 0; i < 2; i++) {
				const bool isSelected = layoutModeIndex == i;
				if (ImGui::Selectable(layoutModeLabels[i], isSelected)) {
					layoutModeIndex = i;
					group->layout_mode = layoutModeIndex == 1 ? entry_group::LayoutMode::Grid : entry_group::LayoutMode::Stack;
					if (group->layout_mode == entry_group::LayoutMode::Grid && group->layout_columns < 2) {
						group->layout_columns = 2;
					}
					edited = true;
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		if (group->layout_mode == entry_group::LayoutMode::Grid) {
			int columns = group->layout_columns;
			if (ImGui::InputInt("Grid Columns", &columns)) {
				group->layout_columns = (std::clamp)(columns, 2, 10);
				edited = true;
			}
		}
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Hint Media");
	bool hintEnabled = entry->hint.has_value();
	if (ImGui::Checkbox("Enable Hint", &hintEnabled)) {
		if (hintEnabled) {
			entry_base::HintConfig hintConfig;
			hintConfig.showOn = entry_base::HintConfig::ShowOn::Note;
			entry->hint = std::move(hintConfig);
		} else {
			entry->hint.reset();
		}
		edited = true;
	}

	if (hintEnabled && entry->hint.has_value()) {
		auto& hint = *entry->hint;
		const char* showOnLabels[] = { "note", "control", "both" };
		int showOnIndex = HintShowOnToIndex(hint.showOn);
		if (ImGui::BeginCombo("Show On", showOnLabels[showOnIndex])) {
			for (int i = 0; i < 3; i++) {
				const bool isSelected = showOnIndex == i;
				if (ImGui::Selectable(showOnLabels[i], isSelected)) {
					hint.showOn = HintShowOnFromIndex(i);
					edited = true;
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		bool mediaEnabled = hint.media.has_value();
		if (ImGui::Checkbox("Enable Hint Media", &mediaEnabled)) {
			if (mediaEnabled) {
				entry_base::HintMediaConfig mediaConfig;
				mediaConfig.type = entry_base::HintMediaConfig::Type::Flipbook;
				mediaConfig.fps = 8;
				mediaConfig.maxW = 360;
				mediaConfig.maxH = 220;
				mediaConfig.loop = true;
				mediaConfig.preload = false;
				hint.media = std::move(mediaConfig);
			} else {
				hint.media.reset();
			}
			edited = true;
		}

		if (hint.media.has_value()) {
			auto& media = *hint.media;
			const char* mediaTypeLabels[] = { "flipbook", "gif", "webp", "webm" };
			int mediaTypeIndex = HintMediaTypeToIndex(media.type);
			if (ImGui::BeginCombo("Media Type", mediaTypeLabels[mediaTypeIndex])) {
				for (int i = 0; i < 4; i++) {
					const bool isSelected = mediaTypeIndex == i;
					if (ImGui::Selectable(mediaTypeLabels[i], isSelected)) {
						media.type = HintMediaTypeFromIndex(i);
						edited = true;
					}
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			if (ImGui::InputTextWithPaste("Media Path", media.path)) {
				SyncHintMediaPathAndCache(media);
				edited = true;
			}

			HintPickerState& pickerState = GetHintPickerState();
			if (ImGui::Button("Pick From Data...")) {
				if (pickerState.basePath.empty()) {
					pickerState.basePath = "Data\\SKSE\\Plugins\\dMenu\\hints";
				}
				pickerState.filter.clear();
				pickerState.loaded = false;
				pickerState.mediaType = media.type;
				ImGui::OpenPopup("Hint Media Picker");
			}

			if (ImGui::BeginPopupModal("Hint Media Picker", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::TextUnformatted("Hint media path helper");
				if (ImGui::InputTextWithPaste("Base Path", pickerState.basePath)) {
					pickerState.loaded = false;
				}
				if (ImGui::InputTextWithPaste("Filter", pickerState.filter)) {
					// live filter only
				}
				if (ImGui::Button("Rescan")) {
					pickerState.loaded = false;
				}
				ImGui::SameLine();
				if (ImGui::Button("Close")) {
					ImGui::CloseCurrentPopup();
				}

				if (!pickerState.loaded || pickerState.mediaType != media.type) {
					pickerState.mediaType = media.type;
					const std::filesystem::path basePath = ResolvePickerBasePath(pickerState.basePath);
					pickerState.candidates = BuildHintMediaCandidates(basePath, media.type);
					pickerState.loaded = true;
				}

				ImGui::Text("Candidates: %d", static_cast<int>(pickerState.candidates.size()));
				if (ImGui::BeginChild("##hint_media_candidates", ImVec2(760.f, 260.f), true)) {
					for (const auto& candidate : pickerState.candidates) {
						if (!ContainsCaseInsensitive(candidate, pickerState.filter)) {
							continue;
						}
						if (ImGui::Selectable(candidate.c_str(), false)) {
							media.path = ToDataRelativePathIfPossible(std::filesystem::path(candidate));
							SyncHintMediaPathAndCache(media);
							edited = true;
							ImGui::CloseCurrentPopup();
							break;
						}
					}
				}
				ImGui::EndChild();
				ImGui::EndPopup();
			}

			if (media.type == entry_base::HintMediaConfig::Type::Flipbook) {
				int fps = media.fps;
				if (ImGui::InputInt("FPS", &fps)) {
					media.fps = (std::clamp)(fps, 1, 120);
					edited = true;
				}
			}

			int maxW = media.maxW;
			if (ImGui::InputInt("MaxW", &maxW)) {
				media.maxW = (std::clamp)(maxW, 32, 1024);
				edited = true;
			}

			int maxH = media.maxH;
			if (ImGui::InputInt("MaxH", &maxH)) {
				media.maxH = (std::clamp)(maxH, 32, 1024);
				edited = true;
			}

			if (ImGui::Checkbox("Loop", &media.loop)) {
				edited = true;
			}
			if (ImGui::Checkbox("Preload", &media.preload)) {
				edited = true;
			}

			std::string validationMessage;
			if (media.path.empty()) {
				validationMessage = "Hint media path is empty.";
			} else {
				const std::string ext = ToLowerASCII(std::filesystem::path(media.path).extension().string());
				if (media.type == entry_base::HintMediaConfig::Type::Gif && ext != ".gif") {
					validationMessage = "GIF type expects a .gif file path.";
				} else if (media.type == entry_base::HintMediaConfig::Type::Webp && ext != ".webp") {
					validationMessage = "WebP type expects a .webp file path.";
				} else if (media.type == entry_base::HintMediaConfig::Type::Webm && ext != ".webm") {
					validationMessage = "WebM type expects a .webm file path.";
				}
			}

			if (!validationMessage.empty()) {
				ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s", validationMessage.c_str());
			}
			if (!media.path.empty() && !IsDataRelativePathText(media.path)) {
				ImGui::TextColored(ImVec4(1.f, 0.8f, 0.35f, 1.f), "%s", "Portable path recommended: Data\\... (auto-normalized when possible)");
			}
		}
	}

	int current_type = entry->type;

	// Show fields specific to the selected setting type
	switch (current_type) {
	case kEntryType_Checkbox:
		{
			setting_checkbox* checkbox = dynamic_cast<setting_checkbox*>(entry);
			if (ImGui::Checkbox("Default", &checkbox->default_value)) {
				edited = true;
			}
			std::string old_control_id = checkbox->control_id;
			if (ImGui::InputTextWithPaste("Control ID", checkbox->control_id)) {  // on change of control id
				if (!old_control_id.empty()) {                            // remove old control id
					m_checkbox_toggle.erase(old_control_id);
				}
				if (!checkbox->control_id.empty()) {
					m_checkbox_toggle[checkbox->control_id] = checkbox;  // update control id
				}
				edited = true;
			}
			break;
		}
	case kEntryType_Slider:
		{
			setting_slider* slider = dynamic_cast<setting_slider*>(entry);
			if (ImGui::InputFloat("Default", &slider->default_value))
				edited = true;
			if (ImGui::InputFloat("Min", &slider->min))
				edited = true;
			if (ImGui::InputFloat("Max", &slider->max))
				edited = true;
			if (ImGui::InputFloat("Step", &slider->step))
				edited = true;
			break;
		}
	case kEntryType_Textbox:
		{
			setting_textbox* textbox = dynamic_cast<setting_textbox*>(entry);
			if (ImGui::InputTextWithPaste("Default", textbox->default_value))
				edited = true;
			break;
		}
	case kEntryType_Dropdown:
		{
			setting_dropdown* dropdown = dynamic_cast<setting_dropdown*>(entry);
			ImGui::Text("Dropdown options");
			if (ImGui::BeginChild("##dropdown_items", ImVec2(0, 200), true, ImGuiWindowFlags_AlwaysAutoResize))
			{
				int buf;
				if (ImGui::InputInt("Default", &buf, 0, 100)) {
					dropdown->default_value = buf;
					edited = true;
				}
				if (ImGui::Button("Add")) {
					dropdown->options.emplace_back();
					edited = true;
				}
				for (int i = 0; i < dropdown->options.size(); i++) {
					ImGui::PushID(i);
					if (IMEWidgets::InputText("Option", &dropdown->options[i]))
						edited = true;
					ImGui::SameLine();
					if (ImGui::Button("-")) {
						dropdown->options.erase(dropdown->options.begin() + i);
						if (dropdown->value == i) {  // erased current value
							dropdown->value = 0;     // reset to 1st value
						}
						edited = true;
					}

					ImGui::PopID();
				}
			}
			ImGui::EndChild();
			break;
		}
	case kEntryType_Text:
		{
			// color palette to set text color
			ImGui::Text("%s", TR("modsettings_text_color", "Text color"));
			if (ImGui::BeginChild("##text_color", ImVec2(0, 100), true, ImGuiWindowFlags_AlwaysAutoResize)) {
				entry_text* text = dynamic_cast<entry_text*>(entry);
				float colorArray[4] = { text->_color.x, text->_color.y, text->_color.z, text->_color.w };
				if (ImGui::ColorEdit4("Color", colorArray)) {
					text->_color = ImVec4(colorArray[0], colorArray[1], colorArray[2], colorArray[3]);
					edited = true;
				}
			}
			ImGui::EndChild();
			break;
		}
	case kEntryType_Color:
	{
			// color palette to set text color
			ImGui::Text("%s", TR("modsettings_color", "Color"));
			if (ImGui::BeginChild("##color", ImVec2(0, 100), true, ImGuiWindowFlags_AlwaysAutoResize)) {
				setting_color* color = dynamic_cast<setting_color*>(entry);
				float colorArray[4] = { color->default_color.x, color->default_color.y, color->default_color.z, color->default_color.w };
				if (ImGui::ColorEdit4("Default Color", colorArray)) {
					color->default_color = ImVec4(colorArray[0], colorArray[1], colorArray[2], colorArray[3]);
					edited = true;
				}
			}
			ImGui::EndChild();
			break;
	}
	case kEntryType_Keymap:
	{
			// keymap editor
			ImGui::Text("%s", TR("modsettings_keymap", "Keymap"));
			if (ImGui::BeginChild("##keymap", ImVec2(0, 100), true, ImGuiWindowFlags_AlwaysAutoResize)) {
				setting_keymap* keymap = dynamic_cast<setting_keymap*>(entry);
				if (ImGui::InputInt("Default Key ID", &(keymap->default_value))) {
					edited = true;
				}
			}
			ImGui::EndChild();
			break;
	}
	case kEntryType_Button:
	{
			ImGui::Text("%s", TR("modsettings_button", "Button"));
			if (ImGui::BeginChild("##button", ImVec2(0, 100), true, ImGuiWindowFlags_AlwaysAutoResize)) {
				entry_button* button = dynamic_cast<entry_button*>(entry);
				if (IMEWidgets::InputText("ID", &(button->id))) {
					edited = true;
				}
			}
			ImGui::EndChild();
	}
	default:
		break;
	}

	ImGui::Text("%s", TR("modsettings_control", "Control"));
	// choose fail action
	static const char* failActions[] = { "Disable", "Hide" };
	if (ImGui::BeginCombo("Fail Action", failActions[(int)entry->control.failAction])) {
		for (int i = 0; i < 2; i++) {
			bool is_selected = ((int)entry->control.failAction == i);
			if (ImGui::Selectable(failActions[i], is_selected))
				entry->control.failAction = static_cast<entry_base::Control::FailAction>(i);
			if (is_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	if (ImGui::Button("Add")) {
		entry->control.reqs.emplace_back();
		edited = true;
	}

	if (ImGui::BeginChild("##control_requirements", ImVec2(0, 100), true, ImGuiWindowFlags_AlwaysAutoResize)) {
		// set the number of columns to 3


		for (int i = 0; i < entry->control.reqs.size(); i++) {
			auto& req = entry->control.reqs[i];  // get a reference to the requirement at index i
			ImGui::PushID(&req);
			const int numColumns = 3;
			ImGui::Columns(numColumns, nullptr, false);
			// set the width of each column to be the same
			ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.5f);
			ImGui::SetColumnWidth(1, ImGui::GetWindowWidth() * 0.25f);
			ImGui::SetColumnWidth(2, ImGui::GetWindowWidth() * 0.25f);

			if (ImGui::InputTextWithPaste("id", req.id)) {
				edited = true;
			}

			// add the second column
			ImGui::NextColumn();
			static const char* reqTypes[] = { "Checkbox", "GameSetting" };
			if (ImGui::BeginCombo("Type", reqTypes[(int)req.type])) {
				for (int i = 0; i < 2; i++) {
					bool is_selected = ((int)req.type == i);
					if (ImGui::Selectable(reqTypes[i], is_selected))
						req.type = static_cast<entry_base::Control::Req::ReqType>(i);
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			// add the third column
			ImGui::NextColumn();
			if (ImGui::Checkbox("not", &req._not)) {
				edited = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("-")) {
				entry->control.reqs.erase(entry->control.reqs.begin() + i);  // erase the requirement at index i
				edited = true;
				i--;  // update the loop index to account for the erased element
			}
			ImGui::Columns(1);

			ImGui::PopID();
		}
	}
	ImGui::EndChild();

	

	ImGui::Text("%s", TR("modsettings_localization", "Localization"));
	if (ImGui::BeginChild((std::string(entry->name.def) + "##Localization").c_str(), ImVec2(0, 100), true, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (ImGui::InputTextWithPaste("Name", entry->name.key))
			edited = true;
		if (ImGui::InputTextWithPaste("Description", entry->desc.key))
			edited = true;
	}
	ImGui::EndChild();

	if (entry->is_setting()) {
		setting_base* setting = dynamic_cast<setting_base*>(entry);
		ImGui::Text("%s", TR("modsettings_serialization", "Serialization"));
		if (ImGui::BeginChild((std::string(setting->name.def) + "##serialization").c_str(), ImVec2(0, 100), true, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (ImGui::InputTextWithPasteRequired("ini ID", setting->ini_id))
				edited = true;

			if (ImGui::InputTextWithPasteRequired("ini Section", setting->ini_section))
				edited = true;

			if (ImGui::InputTextWithPaste("Game Setting", setting->gameSetting)) {
				edited = true;
			}
			if (setting->type == kEntryType_Slider) {
				if (!setting->gameSetting.empty()) {
					switch (setting->gameSetting[0]) {
					case 'f':
					case 'i':
					case 'u':
						break;
					default:
						ImGui::TextColored(ImVec4(1, 0, 0, 1), "For sliders, game setting must start with f, i, or u for float, int, or uint respectively for type specification.");
						break;
					}
				}
			}
		}
		ImGui::EndChild();
	}
	if (edited) {
		json_dirty_mods.insert(mod);
	}
	ImGui::PopID();
}



void ModSettings::show_entry(entry_base* entry, mod_setting* mod)
{
	show_entry_impl(entry, mod, 0.5f);  // Default width fraction for stack layout
}

void ModSettings::show_entry_impl(entry_base* entry, mod_setting* mod, float widthFrac)
{
	ImGui::PushID(entry);
	bool edited = false;
	
	// check if all control requirements are met
	bool available = entry->control.satisfied();
	if (!available) {
		switch (entry->control.failAction) {
		case entry_base::Control::FailAction::kFailAction_Hide:
			if (!edit_mode) { // use disable fail action under edit mode
				ImGui::PopID();
				return;
			}
		default:
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
			ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
		}
	}
		
	float width = ImGui::GetContentRegionAvail().x * widthFrac;
	switch (entry->type) {
	case kEntryType_Checkbox:
		{
			setting_checkbox* checkbox = dynamic_cast<setting_checkbox*>(entry);

			if (ImGui::Checkbox(checkbox->name.get(), &checkbox->value)) {
				edited = true;
			}
			const bool hoveredControl = IsItemHoveredOrFocused();
			if (hoveredControl) {
				if (ImGui::IsKeyPressed(ImGuiKey_R)) {
					edited |= checkbox->reset();
				}
			}

			ShowEntryHintTooltip(entry, hoveredControl, true);
		}
		break;

	case kEntryType_Slider:
		{
			setting_slider* slider = dynamic_cast<setting_slider*>(entry);

			// Set the width of the slider to a fraction of the available width
			ImGui::SetNextItemWidth(width);
			if (ImGui::SliderFloatWithSteps(slider->name.get(), &slider->value, slider->min, slider->max, slider->step)) {
				edited = true;
			}

			const bool hoveredControl = IsItemHoveredOrFocused();
			if (hoveredControl) {
				if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ||
				    ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) ||
				    ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft)) {
					if (slider->value > slider->min) {
						slider->value = (std::max)(slider->min, slider->value - slider->step);
						edited = true;
					}
				}
				if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) ||
				    ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) ||
				    ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight)) {
					if (slider->value < slider->max) {
						slider->value = (std::min)(slider->max, slider->value + slider->step);
						edited = true;
					}
				}

				if (ImGui::IsKeyPressed(ImGuiKey_R)) {
					edited |= slider->reset();
				}
			}

			ShowEntryHintTooltip(entry, hoveredControl, true);
		}
		break;

	case kEntryType_Textbox:
		{
			setting_textbox* textbox = dynamic_cast<setting_textbox*>(entry);

			ImGui::SetNextItemWidth(width);
			if (IMEWidgets::InputText(textbox->name.get(), &textbox->value)) {
				edited = true;
			}
			const bool hoveredControl = IsItemHoveredOrFocused();

			if (hoveredControl) {
				if (ImGui::IsKeyPressed(ImGuiKey_R)) {
					edited |= textbox->reset();

				}
			}

			ShowEntryHintTooltip(entry, hoveredControl, true);
		}
		break;

	case kEntryType_Dropdown:
		{
			setting_dropdown* dropdown = dynamic_cast<setting_dropdown*>(entry);
			const char* name = dropdown->name.get();
			int selected = dropdown->value;

			std::vector<std::string> options = dropdown->options;
			std::vector<const char*> cstrings;
			for (auto& option : options) {
				cstrings.push_back(option.c_str());
			}
			const char* preview_value = "";
			if (selected >= 0 && selected < options.size()) {
				preview_value = cstrings[selected];
			}
			ImGui::SetNextItemWidth(width);
			if (ImGui::BeginCombo(name, preview_value)) {
				for (int i = 0; i < options.size(); i++) {
					bool is_selected = (selected == i);

					if (ImGui::Selectable(cstrings[i], is_selected)) {
						selected = i;
						dropdown->value = selected;
						edited = true;
					}

					if (is_selected) {
						ImGui::SetItemDefaultFocus();
					}
				}

				ImGui::EndCombo();
			}

			const bool hoveredControl = IsItemHoveredOrFocused();
			if (hoveredControl) {
				if (ImGui::IsKeyPressed(ImGuiKey_R)) {
					edited |= dropdown->reset();
				}
			}

			ShowEntryHintTooltip(entry, hoveredControl, true);
		}
		break;
	case kEntryType_Text:
		{
			entry_text* t = dynamic_cast<entry_text*>(entry);
			ImGui::TextColored(t->_color, t->name.get());
			const bool hoveredControl = IsItemHoveredOrFocused();
			ShowEntryHintTooltip(entry, hoveredControl, true);
		}
		break;
	case kEntryType_Group:
		{
			entry_group* g = dynamic_cast<entry_group*>(entry);
			ImGuiStorage* storage = ImGui::GetStateStorage();
			const ImGuiID openId = ImGui::GetID("##group_expanded");
			bool expanded = storage->GetBool(openId, false);
			if (DrawManualCollapsibleHeader("##group_toggle", g->name.get(), expanded)) {
				expanded = !expanded;
				storage->SetBool(openId, expanded);
			}
			const bool hoveredControl = IsItemHoveredOrFocused();

			ShowEntryHintTooltip(entry, hoveredControl, false);
			if (expanded) {
				// Use grid layout if enabled and not in edit mode
				if (g->layout_mode == entry_group::LayoutMode::Grid && g->layout_columns > 1 && !edit_mode) {
					show_entries_grid(g->entries, mod, g->layout_columns);
				} else {
					show_entries(g->entries, mod);
				}
			}
		}
		break;
	case kEntryType_Keymap:
		{
			setting_keymap* k = dynamic_cast<setting_keymap*>(entry);
			std::string hash = std::to_string((unsigned long long)(void**)k);
			bool hoveredControl = false;
			if (ImGui::Button("Remap")) {
				ImGui::OpenPopup(hash.data());
				BeginKeyMapCapture(k);
			}
			hoveredControl |= IsItemHoveredOrFocused();
			ImGui::SameLine();
			if (ImGui::Button("Unmap")) {
				k->value = 0;
				edited = true;
			}
			hoveredControl |= IsItemHoveredOrFocused();
			ImGui::SameLine();
			ImGui::Text("%s:", k->name.get());
			hoveredControl |= IsItemHoveredOrFocused();
			ImGui::SameLine();
			ImGui::Text(setting_keymap::keyid_to_str(k->value));
			hoveredControl |= IsItemHoveredOrFocused();

			ShowEntryHintTooltip(entry, hoveredControl, true);

			if (ImGui::BeginPopupModal(hash.data())) {
				ImGui::Text("%s", TR("modsettings_enter_key", "Enter the key you wish to map"));
				if (keyMapListening == nullptr) {
					edited = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
		break;
	case kEntryType_Color:
		{
			setting_color* color = dynamic_cast<setting_color*>(entry);
			float colorArray[4] = { color->color.x, color->color.y, color->color.z, color->color.w };

			const ImGuiColorEditFlags compactFlags =
				ImGuiColorEditFlags_DisplayRGB |
				ImGuiColorEditFlags_AlphaBar;

			ImGui::SetNextItemWidth(width);
			const std::string colorEditLabel = std::string(color->name.get()) + "##color_preview";
			bool colorChanged = ImGui::ColorEdit4(colorEditLabel.c_str(), colorArray, compactFlags);
			bool hoveredControl = IsItemHoveredOrFocused();
			const bool previewFocused = ImGui::IsItemFocused();
			const bool previewActive = ImGui::IsItemActive();

			ImGuiContext* ctx = ImGui::GetCurrentContext();
			const bool navFromGamepad =
				ctx != nullptr &&
				ctx->NavInputSource == ImGuiInputSource_Gamepad;
			const bool gamepadEditHeld =
				ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown) ||
				ImGui::IsKeyDown(ImGuiKey_Enter);
			const bool showGamepadPicker =
				navFromGamepad &&
				(previewActive || (previewFocused && gamepadEditHeld));

			if (showGamepadPicker) {
				const float pickerWidth = (std::max)(220.0f, width);
				ImGui::SetNextItemWidth(pickerWidth);
				const ImGuiColorEditFlags pickerFlags =
					ImGuiColorEditFlags_DisplayRGB |
					ImGuiColorEditFlags_PickerHueBar |
					ImGuiColorEditFlags_NoSidePreview |
					ImGuiColorEditFlags_AlphaBar;

				if (ImGui::ColorPicker4("##gamepad_color_picker", colorArray, pickerFlags)) {
					colorChanged = true;
				}

				const bool pickerFocused = ImGui::IsItemFocused();
				const bool pickerActive = ImGui::IsItemActive();
				const bool pickerHovered = ImGui::IsItemHovered();
				hoveredControl |= pickerHovered || pickerFocused || pickerActive;

				if (navFromGamepad && (previewFocused || previewActive || pickerFocused || pickerActive)) {
					auto analog = [](ImGuiKey key) -> float {
						const ImGuiKeyData* keyData = ImGui::GetKeyData(key);
						if (keyData != nullptr) {
							return keyData->AnalogValue;
						}
						return ImGui::IsKeyDown(key) ? 1.0f : 0.0f;
					};

					float hue = 0.0f;
					float saturation = 0.0f;
					float value = 0.0f;
					ImGui::ColorConvertRGBtoHSV(
						colorArray[0],
						colorArray[1],
						colorArray[2],
						hue,
						saturation,
						value);

					const float dt = (std::clamp)(ImGui::GetIO().DeltaTime, 0.0f, 0.050f);
					const float stickX = analog(ImGuiKey_GamepadLStickRight) - analog(ImGuiKey_GamepadLStickLeft);
					const float stickY = analog(ImGuiKey_GamepadLStickUp) - analog(ImGuiKey_GamepadLStickDown);
					const float paletteSpeed = 1.35f;

					const float nextSaturation = (std::clamp)(saturation + stickX * paletteSpeed * dt, 0.0f, 1.0f);
					const float nextValue = (std::clamp)(value + stickY * paletteSpeed * dt, 0.0f, 1.0f);

					const bool hueUpStep =
						ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, true) ||
						ImGui::IsKeyPressed(ImGuiKey_UpArrow, true);
					const bool hueDownStep =
						ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, true) ||
						ImGui::IsKeyPressed(ImGuiKey_DownArrow, true);

					float nextHue = hue;
					if (hueUpStep) {
						nextHue += 0.020f;
					}
					if (hueDownStep) {
						nextHue -= 0.020f;
					}
					if (hueUpStep || hueDownStep) {
						INFO(
							"ColorHueStep: entry='{}' up={} down={} hue={:.3f}->{:.3f}",
							color->name.def,
							hueUpStep ? 1 : 0,
							hueDownStep ? 1 : 0,
							hue,
							nextHue);
					}
					while (nextHue < 0.0f) {
						nextHue += 1.0f;
					}
					while (nextHue > 1.0f) {
						nextHue -= 1.0f;
					}

					if (nextHue != hue || nextSaturation != saturation || nextValue != value) {
						float outR = 0.0f;
						float outG = 0.0f;
						float outB = 0.0f;
						ImGui::ColorConvertHSVtoRGB(nextHue, nextSaturation, nextValue, outR, outG, outB);
						colorArray[0] = outR;
						colorArray[1] = outG;
						colorArray[2] = outB;
						colorChanged = true;
					}
				}
			}

			if (colorChanged) {
				color->color = ImVec4(colorArray[0], colorArray[1], colorArray[2], colorArray[3]);
				edited = true;
			}

			if (hoveredControl && ImGui::IsKeyPressed(ImGuiKey_R)) {
				edited |= color->reset();
			}

			if (hoveredControl && navFromGamepad) {
				g_hintFocusedEntryThisFrame = entry;
			}
			ShowEntryHintTooltip(entry, hoveredControl, true);
		}
		break;
	case kEntryType_Button:
	{
			entry_button* b = dynamic_cast<entry_button*>(entry);
			if (ImGui::Button(b->name.get())) {
				// trigger button callback
				std::string custom_event_name = "dmenu_buttonCallback";
				send_mod_callback_event(custom_event_name, b->id);
			}
			const bool hoveredControl = IsItemHoveredOrFocused();
			ShowEntryHintTooltip(entry, hoveredControl, true);
	}
	break;
	default:
		break;
	}
	if (!available) { // disabled before
		ImGui::PopStyleVar();
		ImGui::PopItemFlag();
		ImGui::PopStyleColor();
	}
	if (edited) {
		MarkIniDirty(mod);
	}
	ImGui::PopID();
}

void ModSettings::show_entries(std::vector<entry_base*>& entries, mod_setting* mod)
{
	ImGui::PushID(&entries);
	bool edited = false;

	
	for (auto it = entries.begin(); it != entries.end(); it++) {
		ModSettings::entry_base* entry = *it;

		ImGui::PushID(entry);

		ImGui::Indent();
		// edit entry
		bool entry_deleted = false;
		bool all_entries_deleted = false;

		if (edit_mode) {
			if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
				if (it != entries.begin()) {
					std::iter_swap(it, it - 1);
					edited = true;
				}
			}
			ImGui::SameLine();
			if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
				if (it != entries.end() - 1) {
					std::iter_swap(it, it + 1);
					edited = true;
				}
			}
			ImGui::SameLine();

			if (ImGui::Button("Edit")) {  // Get the size of the current window
				ImGui::OpenPopup("Edit Setting");
				ImVec2 windowSize = ImGui::GetWindowSize();
				// Set the size of the pop-up to be proportional to the window size
				ImVec2 popupSize(windowSize.x * 0.5f, windowSize.y * 0.5f);
				ImGui::SetNextWindowSize(popupSize);
			}
			if (ImGui::BeginPopup("Edit Setting", ImGuiWindowFlags_AlwaysAutoResize)) {
				// Get the size of the current window
				show_entry_edit(entry, mod);
				ImGui::EndPopup();
			}

			
			ImGui::SameLine();

			// delete  button
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
			if (ImGui::Button("Delete")) {
				// delete entry
				ImGui::OpenPopup("Delete Confirmation");
				// move popup mousepos
				ImVec2 mousePos = ImGui::GetMousePos();
				ImGui::SetNextWindowPos(mousePos);
			}
			if (ImGui::BeginPopupModal("Delete Confirmation", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Text("%s", TR("modsettings_confirm_delete", "Are you sure you want to delete this setting?"));
				ImGui::Separator();

				if (ImGui::Button("Yes", ImVec2(120, 0))) {
					if (entry->type == entry_type::kEntryType_Checkbox) {
						setting_checkbox* checkbox = (setting_checkbox*)entry;
						m_checkbox_toggle.erase(checkbox->control_id);
					}
					bool should_decrement = it != entries.begin();
					it = entries.erase(it);
					if (should_decrement) {
						it--;
					}
					if (it == entries.end()) {
						all_entries_deleted = true;
					}
					edited = true;
					delete entry;
					entry_deleted = true;
					// TODO: delete everything in a group if deleting group.
					ImGui::CloseCurrentPopup();
				}
				ImGui::SetItemDefaultFocus();
				ImGui::SameLine();
				if (ImGui::Button("No", ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			ImGui::PopStyleColor();
			ImGui::SameLine();

		}

		if (!entry_deleted) {
			show_entry(entry, mod);
		}

		ImGui::Unindent();
		ImGui::PopID();
		if (all_entries_deleted) {
			break;
		}
	}

		// add entry
	if (edit_mode) {
		if (ImGui::Button("New Entry")) {
			// correct popup position
			ImGui::SetNextWindowPos(ImGui::GetCursorScreenPos());
			ImGui::OpenPopup("Add Entry");
		}
		if (ImGui::BeginPopup("Add Entry")) {
			if (ImGui::Selectable("Checkbox")) {
				setting_checkbox* checkbox = new setting_checkbox();
				entries.push_back(checkbox);
				edited = true;
				INFO("added entry");
			}
			if (ImGui::Selectable("Slider")) {
				setting_slider* slider = new setting_slider();
				entries.push_back(slider);
				edited = true;
				INFO("added entry");
			}
			if (ImGui::Selectable("Textbox")) {
				setting_textbox* textbox = new setting_textbox();
				entries.push_back(textbox);
				edited = true;
				INFO("added entry");
			}
			if (ImGui::Selectable("Dropdown")) {
				setting_dropdown* dropdown = new setting_dropdown();
				dropdown->options.push_back("Option 0");
				dropdown->options.push_back("Option 1");
				dropdown->options.push_back("Option 2");
				entries.push_back(dropdown);
				edited = true;
				INFO("added entry");
			}
			if (ImGui::Selectable("Keymap")) {
				setting_keymap* keymap = new setting_keymap();
				entries.push_back(keymap);
				edited = true;
				INFO("added entry");
			}
			if (ImGui::Selectable("Text")) {
				entry_text* text = new entry_text();
				entries.push_back(text);
				edited = true;
				INFO("added entry");
			}
			if (ImGui::Selectable("Group")) {
				entry_group* group = new entry_group();
				entries.push_back(group);
				edited = true;
				INFO("added entry");
			}
			if (ImGui::Selectable("Color")) {
				setting_color* color = new setting_color();
				entries.push_back(color);
				edited = true;
				INFO("added entry");
			}
			if (ImGui::Selectable("Button")) {
				entry_button* button = new entry_button();
				entries.push_back(button);
				edited = true;
				INFO("added entry");
			}
			ImGui::EndPopup();
		}
	}
	if (edited) {
		json_dirty_mods.insert(mod);
	}
	ImGui::PopID();
}

void ModSettings::show_entries_grid(std::vector<entry_base*>& entries, mod_setting* mod, int columns)
{
	ImGui::PushID(&entries);

	ImGuiTableFlags flags =
		ImGuiTableFlags_SizingStretchSame |
		ImGuiTableFlags_NoBordersInBody |
		ImGuiTableFlags_PadOuterX;

	ImGui::Indent();
	if (ImGui::BeginTable("##grid", columns, flags)) {
		for (auto* entry : entries) {
			ImGui::TableNextColumn();
			show_entry_impl(entry, mod, 1.0f);  // Full width within cell
		}
		ImGui::EndTable();
	}
	ImGui::Unindent();

	ImGui::PopID();
}

inline void ModSettings::SendSettingsUpdateEvent(std::string& modName)
{
	auto eventSource = SKSE::GetModCallbackEventSource();
	if (!eventSource) {
		return;
	}
	SKSE::ModCallbackEvent callbackEvent;
	callbackEvent.eventName = "dmenu_updateSettings";
	callbackEvent.strArg = modName;
	eventSource->SendEvent(&callbackEvent);
}

void ModSettings::send_mod_callback_event(std::string& mod_name, std::string& str_arg)
{
	auto eventSource = SKSE::GetModCallbackEventSource();
	if (!eventSource) {
		return;
	}
	SKSE::ModCallbackEvent callbackEvent;
	callbackEvent.eventName = mod_name.data();
	callbackEvent.strArg = str_arg.data();
	eventSource->SendEvent(&callbackEvent);
}



void ModSettings::show_modSetting(mod_setting* mod)
{

	show_entries(mod->entries, mod);

}



void ModSettings::submitInput(uint32_t id)
{
	if (keyMapListening == nullptr) {
		return;
	}

	if (g_keyMapIgnoredInputs.contains(id)) {
		return;
	}

	auto* keymap = keyMapListening;
	auto* captureMod = g_keyMapCaptureMod;
	const bool commitImmediately = g_keyMapCaptureCommitsImmediately;
	const bool changed = keymap->value != static_cast<int>(id);
	keymap->value = static_cast<int>(id);
	ClearKeyMapCapture();

	if (changed && commitImmediately && captureMod) {
		MarkIniDirty(captureMod);
		CommitIniDirtyPage(captureMod);
	}
}

void ModSettings::ObserveKeymapCaptureInput(uint32_t id, bool isDown)
{
	if (isDown) {
		g_inputsCurrentlyDown.insert(id);
		g_mostRecentInputDown = id;
		return;
	}

	g_inputsCurrentlyDown.erase(id);
	if (g_externalKeyMapIgnoredInput && *g_externalKeyMapIgnoredInput == id) {
		g_keyMapIgnoredInputs.erase(id);
		g_externalKeyMapIgnoredInput.reset();
	}
}

bool ModSettings::IsKeymapCapturing()
{
	return keyMapListening != nullptr;
}

bool ModSettings::IsExternalKeymapCaptureActive()
{
	return g_keyMapCaptureCommitsImmediately && keyMapListening != nullptr;
}

void ModSettings::ToggleHintsVisibility()
{
	if (g_hintFocusedEntryLastFrame == nullptr) {
		g_hintToggledEntry = nullptr;
		ClearPinnedMediaHint();
		INFO("HintToggle: no focused entry, cleared toggled hint target");
		return;
	}

	if (g_hintToggledEntry == g_hintFocusedEntryLastFrame) {
		g_hintToggledEntry = nullptr;
		ClearPinnedMediaHint();
		INFO("HintToggle: hiding focused entry hint '{}'", g_hintFocusedEntryLastFrame->name.def);
		return;
	}

	g_hintToggledEntry = g_hintFocusedEntryLastFrame;
	ClearPinnedMediaHint();
	INFO("HintToggle: showing focused entry hint '{}'", g_hintFocusedEntryLastFrame->name.def);
}

bool ModSettings::AreHintsHidden()
{
	return g_hintToggledEntry == nullptr;
}

void ModSettings::RequestPrimaryActionFocus()
{
	request_primary_action_focus = true;
}

inline std::string ModSettings::get_type_str(entry_type t)
{
	switch (t) {
	case entry_type::kEntryType_Checkbox:
		return "checkbox";
	case entry_type::kEntryType_Slider:
		return "slider";
	case entry_type::kEntryType_Textbox:
		return "textbox";
	case entry_type::kEntryType_Dropdown:
		return "dropdown";
	case entry_type::kEntryType_Text:
		return "text";
	case entry_type::kEntryType_Group:
		return "group";
	case entry_type::kEntryType_Color:
		return "color";
	case entry_type::kEntryType_Keymap:
		return "keymap";
	case entry_type::kEntryType_Button:
		return "button";
	default:
		return "invalid";
	}
}

void ModSettings::show()
{
	RefreshIgnoredKeyMapInputs();

	g_hintFocusedEntryThisFrame = g_hintFocusedEntryLastFrame;

	
	// a button on the rhs of this same line
	ImGui::SameLine(ImGui::GetWindowWidth() - 100.0f);  // Move cursor to the right side of the window
	ImGui::ToggleButton(TR("modsettings_edit_config", "Edit Config"), &edit_mode);
	if (ImGui::IsWindowAppearing()) {
		ImGui::SetItemDefaultFocus();
	}

		// Set window padding and item spacing
	const float padding = 8.0f;
	const float spacing = 8.0f;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));

	// Reserve a fixed footer strip so action buttons stay inside the frame
	// and content cannot draw over them.
	const float footerHeight = GetModSettingsFooterHeight();
	ImGui::BeginChild("##modsettings_content", ImVec2(0, -footerHeight), ImGuiChildFlags_NavFlattened);
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
	for (auto& mod : mods) {
		ImGui::PushID(mod);
		ImGuiStorage* storage = ImGui::GetStateStorage();
		const ImGuiID openId = ImGui::GetID("##mod_expanded");
		bool expanded = storage->GetBool(openId, false);
		if (DrawManualCollapsibleHeader("##mod_toggle", mod->name.c_str(), expanded)) {
			expanded = !expanded;
			storage->SetBool(openId, expanded);
		}

		if (expanded) {
			show_modSetting(mod);
		}
		ImGui::PopID();
	}
	ImGui::PopStyleColor();
	ImGui::EndChild();

	if (g_hintFocusedEntryThisFrame != nullptr &&
	    g_hintFocusedEntryLastFrame != g_hintFocusedEntryThisFrame) {
		g_hintToggledEntry = nullptr;
	}
	if (g_hintFocusedEntryThisFrame != nullptr) {
		g_hintFocusedEntryLastFrame = g_hintFocusedEntryThisFrame;
	}
	
	ImGui::PopStyleVar(2);

	// Auto-save pending changes if enabled.
	if (auto_save_enabled) {
		if (!ini_dirty_mods.empty()) {
			FlushIniDirtyMods();
		}
		if (!json_dirty_mods.empty()) {
			FlushJsonDirtyMods();
		}
	}

	show_buttons_window();
}

static const std::string SETTINGS_DIR = "Data\\SKSE\\Plugins\\dmenu\\customSettings";
void ModSettings::init()
{
	// Load all mods from the "Mods" directory

	INFO("Loading .json configurations...");
	namespace fs = std::filesystem;
	for (auto& file : std::filesystem::directory_iterator(SETTINGS_DIR)) {
		if (file.path().extension() == ".json") {
			load_json(file.path());
		}
	}

	INFO("Loading .ini config serializations...");
	// Read serialized settings from the .ini file for each mod
	for (auto& mod : mods) {
		load_ini(mod);
		insert_game_setting(mod);
		flush_game_setting(mod);
	}
	INFO("Mod settings initialized");
}

void ModSettings::ForEachLoadedPage(const std::function<void(const PageVisit&)>& a_callback)
{
	if (!a_callback) {
		return;
	}

	for (const auto* mod : mods) {
		if (mod) {
			a_callback(PageVisit{ mod, mod->name });
		}
	}
}

void ModSettings::ForEachLoadedPageName(const std::function<void(std::string_view)>& a_callback)
{
	if (!a_callback) {
		return;
	}

	ForEachLoadedPage([&a_callback](const PageVisit& page) {
		a_callback(page.name);
	});
}

std::vector<ModSettings::mod_setting*> ModSettings::ForEachSetting(
	const std::function<void(std::string_view)>& a_pageCallback,
	const std::function<std::optional<bool>(const CheckboxVisit&)>& a_checkboxCallback,
	const std::function<SliderUpdate(const SliderVisit&)>& a_sliderCallback)
{
	std::vector<mod_setting*> changedMods;
	if (!a_checkboxCallback && !a_sliderCallback) {
		return changedMods;
	}

	PageSettingsCallbacks callbacks{};
	callbacks.checkbox = a_checkboxCallback;
	callbacks.slider = a_sliderCallback;

	for (auto* mod : mods) {
		if (!mod) {
			continue;
		}

		if (a_pageCallback) {
			a_pageCallback(mod->name);
		}
		for_each_setting(mod, mod->entries, true, 0, changedMods, callbacks);
	}

	return changedMods;
}

bool ModSettings::VisitPageSettings(
	const void* a_pageIdentity,
	const PageSettingsCallbacks& a_callbacks)
{
	if (!a_pageIdentity) {
		return false;
	}

	for (auto* mod : mods) {
		if (static_cast<const void*>(mod) != a_pageIdentity) {
			continue;
		}

		std::vector<mod_setting*> changedMods;
		for_each_setting(mod, mod->entries, true, 0, changedMods, a_callbacks);
		return !changedMods.empty();
	}

	return false;
}

std::vector<ModSettings::mod_setting*> ModSettings::ForEachCheckbox(
	const std::function<void(std::string_view)>& a_pageCallback,
	const std::function<std::optional<bool>(const CheckboxVisit&)>& a_checkboxCallback)
{
	return ForEachSetting(a_pageCallback, a_checkboxCallback, {});
}

bool ModSettings::has_visible_renderable_entry(
	const std::vector<entry_base*>& entries,
	bool enabled,
	const PageSettingsCallbacks& callbacks)
{
	for (auto* entry : entries) {
		if (!entry) {
			continue;
		}

		const bool available = entry->control.satisfied();
		if (!available && entry->control.failAction == entry_base::Control::kFailAction_Hide) {
			continue;
		}

		const bool entryEnabled = enabled && available;
		if (entry->is_group()) {
			auto* group = static_cast<entry_group*>(entry);
			if (has_visible_renderable_entry(group->entries, entryEnabled, callbacks)) {
				return true;
			}
			continue;
		}

		switch (entry->type) {
		case kEntryType_Checkbox:
			if (callbacks.checkbox) {
				return true;
			}
			break;
		case kEntryType_Slider:
			if (callbacks.slider) {
				return true;
			}
			break;
		case kEntryType_Dropdown:
			if (callbacks.dropdown) {
				return true;
			}
			break;
		case kEntryType_Textbox:
			if (callbacks.textbox) {
				return true;
			}
			break;
		case kEntryType_Text:
			if (callbacks.text) {
				return true;
			}
			break;
		case kEntryType_Color:
			if (callbacks.color) {
				return true;
			}
			break;
		case kEntryType_Keymap:
			if (callbacks.keymap) {
				return true;
			}
			break;
		case kEntryType_Button:
			if (callbacks.button) {
				return true;
			}
			break;
		default:
			break;
		}
	}

	return false;
}

void ModSettings::for_each_setting(
	mod_setting* mod,
	const std::vector<entry_base*>& entries,
	bool enabled,
	std::size_t groupDepth,
	std::vector<mod_setting*>& changedMods,
	const PageSettingsCallbacks& callbacks)
{
	for (auto* entry : entries) {
		if (!entry) {
			continue;
		}

		const bool available = entry->control.satisfied();
		if (!available && entry->control.failAction == entry_base::Control::kFailAction_Hide) {
			continue;
		}

		const bool entryEnabled = enabled && available;
		if (entry->is_group()) {
			auto* group = static_cast<entry_group*>(entry);
			if (callbacks.beginGroup && !has_visible_renderable_entry(group->entries, entryEnabled, callbacks)) {
				continue;
			}

			const GroupVisit visit{
				group,
				mod,
				mod->name,
				group->name.get(),
				group->desc.get(),
				groupDepth,
				entryEnabled
			};
			if (callbacks.beginGroup) {
				const bool showChildren = callbacks.beginGroup(visit);
				if (showChildren) {
					for_each_setting(mod, group->entries, entryEnabled, groupDepth + 1, changedMods, callbacks);
				}
				if (callbacks.endGroup) {
					callbacks.endGroup(visit);
				}
			} else {
				for_each_setting(mod, group->entries, entryEnabled, groupDepth + 1, changedMods, callbacks);
			}
			continue;
		}

		auto markCompletedEdit = [&]() {
			MarkIniDirty(mod);
			if (std::find(changedMods.begin(), changedMods.end(), mod) == changedMods.end()) {
				changedMods.push_back(mod);
			}
		};

		if (entry->type == kEntryType_Checkbox && callbacks.checkbox) {
			auto* checkbox = static_cast<setting_checkbox*>(entry);
			const CheckboxVisit visit{
				checkbox,
				mod,
				mod->name,
				checkbox->name.get(),
				checkbox->desc.get(),
				groupDepth,
				checkbox->value,
				entryEnabled
			};
			const auto replacement = callbacks.checkbox(visit);
			if (replacement && entryEnabled && *replacement != checkbox->value) {
				checkbox->value = *replacement;
				markCompletedEdit();
			}
			continue;
		}

		if (entry->type == kEntryType_Slider && callbacks.slider) {
			auto* slider = static_cast<setting_slider*>(entry);
			const int initialStepIndex = Utils::SliderStepIndex(slider->value, slider->min, slider->step);

			const SliderVisit visit{
				slider,
				mod,
				mod->name,
				slider->name.get(),
				slider->desc.get(),
				groupDepth,
				slider->value,
				slider->min,
				slider->max,
				slider->step,
				entryEnabled
			};
			const SliderUpdate update = callbacks.slider(visit);
			if (entryEnabled) {
				const int stepCount = Utils::SliderStepIndex(slider->max, slider->min, slider->step);
				const int stepIndex = update.stepIndex ?
					(std::clamp)(*update.stepIndex, 0, stepCount) :
					initialStepIndex;
				slider->value = Utils::SliderValueAtStep(stepIndex, slider->min, slider->step);
			}
			if (update.editCompleted && entryEnabled) {
				markCompletedEdit();
			}
			continue;
		}

		if (entry->type == kEntryType_Dropdown && callbacks.dropdown) {
			auto* dropdown = static_cast<setting_dropdown*>(entry);
			const DropdownVisit visit{
				dropdown,
				mod,
				mod->name,
				dropdown->name.get(),
				dropdown->desc.get(),
				std::span<const std::string>(dropdown->options.data(), dropdown->options.size()),
				groupDepth,
				dropdown->value,
				entryEnabled
			};
			const auto replacement = callbacks.dropdown(visit);
			if (replacement && entryEnabled && !dropdown->options.empty()) {
				const int selectedIndex = (std::clamp)(*replacement, 0, static_cast<int>(dropdown->options.size()) - 1);
				if (selectedIndex != dropdown->value) {
					dropdown->value = selectedIndex;
					markCompletedEdit();
				}
			}
			continue;
		}

		if (entry->type == kEntryType_Textbox && callbacks.textbox) {
			auto* textbox = static_cast<setting_textbox*>(entry);
			const TextboxVisit visit{
				textbox,
				mod,
				mod->name,
				textbox->name.get(),
				textbox->desc.get(),
				textbox->value,
				groupDepth,
				entryEnabled
			};
			const TextboxUpdate update = callbacks.textbox(visit);
			if (update.value && entryEnabled) {
				textbox->value = *update.value;
			}
			if (update.editCompleted && entryEnabled) {
				markCompletedEdit();
			}
			continue;
		}

		if (entry->type == kEntryType_Text && callbacks.text) {
			auto* text = static_cast<entry_text*>(entry);
			callbacks.text(TextVisit{
				text,
				mod,
				mod->name,
				text->name.get(),
				text->desc.get(),
				Rgba{ text->_color.x, text->_color.y, text->_color.z, text->_color.w },
				groupDepth,
				entryEnabled
			});
			continue;
		}

		if (entry->type == kEntryType_Color && callbacks.color) {
			auto* color = static_cast<setting_color*>(entry);
			const ColorVisit visit{
				color,
				mod,
				mod->name,
				color->name.get(),
				color->desc.get(),
				Rgba{ color->color.x, color->color.y, color->color.z, color->color.w },
				groupDepth,
				entryEnabled
			};
			const ColorUpdate update = callbacks.color(visit);
			if (update.value && entryEnabled) {
				color->color = ImVec4(
					update.value->red,
					update.value->green,
					update.value->blue,
					update.value->alpha);
			}
			if (update.editCompleted && entryEnabled) {
				markCompletedEdit();
			}
			continue;
		}

		if (entry->type == kEntryType_Keymap && callbacks.keymap) {
			auto* keymap = static_cast<setting_keymap*>(entry);
			const KeymapVisit visit{
				keymap,
				mod,
				mod->name,
				keymap->name.get(),
				keymap->desc.get(),
				setting_keymap::keyid_to_str(keymap->value),
				groupDepth,
				keyMapListening == keymap,
				entryEnabled
			};
			const KeymapAction action = callbacks.keymap(visit);
			if (!entryEnabled) {
				continue;
			}
			if (action == KeymapAction::BeginCapture) {
				BeginExternalKeyMapCapture(mod, keymap);
			} else if (action == KeymapAction::Unmap) {
				if (keyMapListening == keymap) {
					ClearKeyMapCapture();
				}
				if (keymap->value != 0) {
					keymap->value = 0;
					markCompletedEdit();
				}
			}
			continue;
		}

		if (entry->type == kEntryType_Button && callbacks.button) {
			auto* button = static_cast<entry_button*>(entry);
			const ButtonVisit visit{
				button,
				mod,
				mod->name,
				button->name.get(),
				button->desc.get(),
				groupDepth,
				entryEnabled
			};
			if (entryEnabled && callbacks.button(visit)) {
				std::string eventName = "dmenu_buttonCallback";
				send_mod_callback_event(eventName, button->id);
			}
		}
	}
}

ModSettings::entry_base* ModSettings::load_json_non_group(nlohmann::json& json)
{
	entry_base* e = nullptr;
	std::string type_str = json["type"].get<std::string>();
	if (type_str == "checkbox") {
		setting_checkbox* scb = new setting_checkbox();
		scb->value = json.contains("default") ? json["default"].get<bool>() : false;
		scb->default_value = scb->value;
		if (json.contains("control")) {
			if (json["control"].contains("id")) {
				scb->control_id = json["control"]["id"].get<std::string>();
				m_checkbox_toggle[scb->control_id] = scb;
			}
		}
		e = scb;
	} else if (type_str == "slider") {
		setting_slider* ssl = new setting_slider();
		ssl->value = json.contains("default") ? json["default"].get<float>() : 0;
		ssl->min = json["style"]["min"].get<float>();
		ssl->max = json["style"]["max"].get<float>();
		ssl->step = json["style"]["step"].get<float>();
		ssl->default_value = ssl->value;
		e = ssl;
	} else if (type_str == "textbox") {
		setting_textbox* stb = new setting_textbox();
		size_t buf_size = json.contains("size") ? json["size"].get<size_t>() : 64;
		stb->value = json.contains("default") ? json["default"].get<std::string>() : "";
		stb->default_value = stb->value;
		e = stb;
	} else if (type_str == "dropdown") {
		setting_dropdown* sdd = new setting_dropdown();
		sdd->value = json.contains("default") ? json["default"].get<int>() : 0;
		for (auto& option_json : json["options"]) {
			sdd->options.push_back(option_json.get<std::string>());
		}
		sdd->default_value = sdd->value;
		e = sdd;
	} else if (type_str == "text") {
		entry_text* et = new entry_text();
		if (json.contains("style") && json["style"].contains("color")) {
			et->_color = ImVec4(json["style"]["color"]["r"].get<float>(), json["style"]["color"]["g"].get<float>(), json["style"]["color"]["b"].get<float>(), json["style"]["color"]["a"].get<float>());
		}
		e = et;
	} else if (type_str == "color") {
		setting_color* sc = new setting_color();
		sc->default_color = ImVec4(json["default"]["r"].get<float>(), json["default"]["g"].get<float>(), json["default"]["b"].get<float>(), json["default"]["a"].get<float>());
		sc->color = sc->default_color;
		e = sc;
	} else if (type_str == "keymap") {
		setting_keymap* skm = new setting_keymap();
		skm->default_value = json["default"].get<int>();
		skm->value = skm->default_value;
		e = skm;
	} else if (type_str == "button") {
		entry_button* sb = new entry_button();
		sb->id = json["id"].get<std::string>();
		e = sb;
	} else {
		INFO("Unknown setting type: {}", type_str);
		return nullptr;
	}
	
	if (e->is_setting()) {
		setting_base* s = dynamic_cast<setting_base*>(e);
		if (json.contains("gameSetting")) {
			s->gameSetting = json["gameSetting"].get<std::string>();
		}
		s->ini_section = json["ini"]["section"].get<std::string>();
		s->ini_id = json["ini"]["id"].get<std::string>();
	}
	return e;

}

ModSettings::entry_group* ModSettings::load_json_group(nlohmann::json& group_json)
{
	entry_group* group = new entry_group();
	for (auto& entry_json : group_json["entries"]) {
		entry_base* entry = load_json_entry(entry_json);
		if (entry) {
			group->entries.push_back(entry);
		}
	}

	// Parse optional layout field for grid rendering
	if (group_json.contains("layout")) {
		const auto& layout = group_json["layout"];
		const std::string mode = layout.value("mode", "stack");
		if (mode == "grid") {
			group->layout_mode = entry_group::LayoutMode::Grid;
			group->layout_columns = (std::max)(1, layout.value("columns", 2));
		}
	}

	return group;
}

ModSettings::entry_base* ModSettings::load_json_entry(nlohmann::json& entry_json)
{
	ModSettings::entry_base* entry = nullptr;
	if (entry_json["type"].get<std::string>() == "group") {
		entry = load_json_group(entry_json);
	} else {
		entry = load_json_non_group(entry_json);
	}
	if (entry == nullptr) {
		INFO("ERROR: Failed to load json entry.");
		return nullptr;
	}

	entry->name.def = entry_json["text"]["name"].get<std::string>();
	if (entry_json["text"].contains("desc")) {
		entry->desc.def = entry_json["text"]["desc"].get<std::string>();
	}

	if (entry_json.contains("translation")) {
		if (entry_json["translation"].contains("name")) {
			entry->name.key = entry_json["translation"]["name"].get<std::string>();
		}
		if (entry_json["translation"].contains("desc")) {
			entry->desc.key = entry_json["translation"]["desc"].get<std::string>();
		}
	}

	ParseHintConfig(entry_json, entry);
	entry->control.failAction = entry_base::Control::kFailAction_Disable;

	if (entry_json.contains("control")) {
		if (entry_json["control"].contains("requirements")) {
			for (auto& req_json : entry_json["control"]["requirements"]) {
				entry_base::Control::Req req;
				req.id = req_json["id"].get<std::string>();
				req._not = !req_json["value"].get<bool>();
				std::string req_type = req_json["type"].get<std::string>();
				if (req_type == "checkbox") {
					req.type = entry_base::Control::Req::ReqType::kReqType_Checkbox;
				} else if (req_type == "gameSetting") {
					req.type = entry_base::Control::Req::ReqType::kReqType_GameSetting;
				}
				else {
					INFO("Error: unknown requirement type: {}", req_type);
					continue;
				}
				entry->control.reqs.push_back(req);
			}
		}
		if (entry_json["control"].contains("failAction")) {
			std::string failAction = entry_json["control"]["failAction"].get<std::string>();
			if (failAction == "disable") {
				entry->control.failAction = entry_base::Control::kFailAction_Disable;
			} else if (failAction == "hide") {
				entry->control.failAction = entry_base::Control::kFailAction_Hide;
			}
		}
	}

	entry->raw_entry_json = entry_json;
	
	return entry;
}

void ModSettings::load_json(std::filesystem::path path)
{
	std::string mod_path = path.string();
	// Load the JSON file for this mod
	std::ifstream json_file(mod_path);
	if (!json_file.is_open()) {
		// Handle error opening file
		return;
	}

	// Parse the JSON file
	nlohmann::json mod_json;
	try {
		json_file >> mod_json;
	} catch (const nlohmann::json::exception& e) {
		// Handle error parsing JSON
		return;
	}
	// Create a mod_setting object to hold the settings for this mod
	mod_setting* mod = new mod_setting();
	mod->raw_mod_json = mod_json;

	// name is .json's name
	mod->name = path.stem().string();
	mod->json_path = SETTINGS_DIR + "\\" + path.filename().string();
	try {
		if (mod_json.contains("ini")) {
			mod->ini_path = mod_json["ini"].get<std::string>();
		}
		else {
			mod->ini_path = SETTINGS_DIR + "\\ini\\" + mod->name.data() + ".ini";
		}

		for (auto& entry_json : mod_json["data"]) {
			entry_base* entry = load_json_entry(entry_json);
			if (entry) {
				mod->entries.push_back(entry);
			}
		}
		// Add the mod to the list of mods
		mods.push_back(mod);
	} catch (const nlohmann::json::exception& e) {
		// Handle error parsing JSON
		INFO("Exception parsing {} : {}", mod_path, e.what());
		return;
	}
	INFO("Loaded mod {}", mod->name);
}

void ModSettings::populate_non_group_json(entry_base* entry, nlohmann::json& json)
{
	if (!entry->is_setting()) {
		if (entry->type == entry_type::kEntryType_Text) {
			json["style"]["color"]["r"] = dynamic_cast<entry_text*>(entry)->_color.x;
			json["style"]["color"]["g"] = dynamic_cast<entry_text*>(entry)->_color.y;
			json["style"]["color"]["b"] = dynamic_cast<entry_text*>(entry)->_color.z;
			json["style"]["color"]["a"] = dynamic_cast<entry_text*>(entry)->_color.w;
		} else if (entry->type == entry_type::kEntryType_Button) {
			auto button = dynamic_cast<entry_button*>(entry);
			json["id"] = button->id;
		}
		return;  // no need to continue, the following fields are only for settings
	}
	
	setting_base* setting = dynamic_cast<setting_base*>(entry);
	json["ini"]["section"] = setting->ini_section;
	json["ini"]["id"] = setting->ini_id;
	if (setting->gameSetting != "") {
		json["gameSetting"] = dynamic_cast<setting_base*>(entry)->gameSetting;
	} else if (json.contains("gameSetting")) {
		json.erase("gameSetting");
	}
	if (setting->type == entry_type::kEntryType_Checkbox) {
		auto cb_setting = dynamic_cast<setting_checkbox*>(entry);
		json["default"] = cb_setting->default_value;
		if (cb_setting->control_id != "") {
			json["control"]["id"] = cb_setting->control_id;
		} else if (json.contains("control") && json["control"].is_object()) {
			json["control"].erase("id");
		}
	} else if (setting->type == entry_type::kEntryType_Slider) {
		auto slider_setting = dynamic_cast<setting_slider*>(entry);
		json["default"] = slider_setting->default_value;
		json["style"]["min"] = slider_setting->min;
		json["style"]["max"] = slider_setting->max;
		json["style"]["step"] = slider_setting->step;

	} else if (setting->type == entry_type::kEntryType_Textbox) {
		auto textbox_setting = dynamic_cast<setting_textbox*>(entry);
		json["default"] = textbox_setting->default_value;

	} else if (setting->type == entry_type::kEntryType_Dropdown) {
		auto dropdown_setting = dynamic_cast<setting_dropdown*>(entry);
		json["default"] = dropdown_setting->default_value;
		json["options"] = nlohmann::json::array();
		for (auto& option : dropdown_setting->options) {
			json["options"].push_back(option);
		}
	} else if (setting->type == entry_type::kEntryType_Color) {
		auto color_setting = dynamic_cast<setting_color*>(entry);
		json["default"]["r"] = color_setting->default_color.x;
		json["default"]["g"] = color_setting->default_color.y;
		json["default"]["b"] = color_setting->default_color.z;
		json["default"]["a"] = color_setting->default_color.w;
	} else if (setting->type == entry_type::kEntryType_Keymap) {
		auto keymap_setting = dynamic_cast<setting_keymap*>(entry);
		json["default"] = keymap_setting->default_value;
	}
	
}

void ModSettings::populate_group_json(entry_group* group, nlohmann::json& group_json)
{
	group_json["entries"] = nlohmann::json::array();
	for (auto& entry : group->entries) {
		nlohmann::json entry_json;
		populate_entry_json(entry, entry_json);
		group_json["entries"].push_back(entry_json);
	}

	if (group->layout_mode == entry_group::LayoutMode::Grid && group->layout_columns > 1) {
		group_json["layout"]["mode"] = "grid";
		group_json["layout"]["columns"] = (std::clamp)(group->layout_columns, 2, 10);
	} else if (group_json.contains("layout")) {
		group_json.erase("layout");
	}
}
void ModSettings::populate_entry_json(entry_base* entry, nlohmann::json& entry_json)
{
	if (entry->raw_entry_json.is_object()) {
		entry_json = entry->raw_entry_json;
	}
	if (!entry_json.is_object()) {
		entry_json = nlohmann::json::object();
	}

	// common fields for entry
	entry_json["text"]["name"] = entry->name.def;
	entry_json["text"]["desc"] = entry->desc.def;
	if (!entry->name.key.empty() || !entry->desc.key.empty() || entry_json.contains("translation")) {
		entry_json["translation"]["name"] = entry->name.key;
		entry_json["translation"]["desc"] = entry->desc.key;
	}
	entry_json["type"] = get_type_str(entry->type);
	PopulateHintJson(entry, entry_json);
	if (!entry->hint.has_value() && entry_json.contains("hint")) {
		entry_json.erase("hint");
	}

	nlohmann::json control_json = nlohmann::json::object();
	if (entry_json.contains("control") && entry_json["control"].is_object()) {
		control_json = entry_json["control"];
	}
	control_json["requirements"] = nlohmann::json::array();

	for (auto& req : entry->control.reqs) {
		nlohmann::json req_json;
		std::string req_type = "";
		switch (req.type) {
		case entry_base::Control::Req::kReqType_Checkbox:
			req_type = "checkbox";
			break;
		case entry_base::Control::Req::kReqType_GameSetting:
			req_type = "gameSetting";
			break;
		default:
			req_type = "ERRORTYPE";
			break;
		}
		req_json["type"] = req_type;
		req_json["value"] = !req._not;
		req_json["id"] = req.id;
		control_json["requirements"].push_back(req_json);
	}
	switch (entry->control.failAction) {
	case entry_base::Control::kFailAction_Disable:
		control_json["failAction"] = "disable";
		break;
	case entry_base::Control::kFailAction_Hide:
		control_json["failAction"] = "hide";
		break;
	default:
		control_json["failAction"] = "ERROR";
		break;
	}
	entry_json["control"] = control_json;
	if (entry->is_group()) {
		populate_group_json(dynamic_cast<entry_group*>(entry), entry_json);
	} else {
		populate_non_group_json(entry, entry_json);
	}

	entry->raw_entry_json = entry_json;
}
/* Serialize config to .json*/
void ModSettings::flush_json(mod_setting* mod)
{
	nlohmann::json mod_json = mod->raw_mod_json.is_object() ? mod->raw_mod_json : nlohmann::json::object();
	mod_json["name"] = mod->name;
	mod_json["ini"] = mod->ini_path;

	nlohmann::json data_json = nlohmann::json::array();

	for (auto& entry : mod->entries) {
		nlohmann::json entry_json;
		populate_entry_json(entry, entry_json);
		data_json.push_back(entry_json);
	}

	
	std::ofstream json_file(mod->json_path);
	if (!json_file.is_open()) {
		// Handle error opening file
		INFO("error: failed to open {}", mod->json_path);
		return;
	}
	
	mod_json["data"] = data_json;

	try {
		json_file << mod_json;
	} catch (const nlohmann::json::exception& e) {
		// Handle error parsing JSON
		ERROR("Exception dumping {} : {}", mod->json_path, e.what());
	}

	mod->raw_mod_json = mod_json;

	insert_game_setting(mod);
	flush_game_setting(mod);

	INFO("Saved config for {}", mod->name);
}

void ModSettings::get_all_settings(mod_setting* mod, std::vector<ModSettings::setting_base*>& r_vec)
{
	std::stack<entry_group*> group_stack;
	for (auto& entry : mod->entries) {
		if (entry->is_group()) {
			group_stack.push(dynamic_cast<entry_group*>(entry));
		} else if (entry->is_setting()) {
			r_vec.push_back(dynamic_cast<setting_base*>(entry));
		}
	}
	while (!group_stack.empty()) {  // get all groups
		auto group = group_stack.top();
		group_stack.pop();
		for (auto& entry : group->entries) {
			if (entry->is_group()) {
				group_stack.push(dynamic_cast<entry_group*>(entry));
			} else if (entry->is_setting()) {
				r_vec.push_back(dynamic_cast<setting_base*>(entry));
			}
		}
	}
}

void ModSettings::get_all_entries(mod_setting* mod, std::vector<ModSettings::entry_base*>& r_vec)
{
	std::stack<entry_group*> group_stack;
	for (auto& entry : mod->entries) {
		if (entry->is_group()) {
			group_stack.push(dynamic_cast<entry_group*>(entry));
		}
		r_vec.push_back(entry);
	}
	while (!group_stack.empty()) {  // get all groups
		auto group = group_stack.top();
		group_stack.pop();
		for (auto& entry : group->entries) {
			if (entry->is_group()) {
				group_stack.push(dynamic_cast<entry_group*>(entry));
			}
			r_vec.push_back(entry);
		}
	}
}


void ModSettings::load_ini(mod_setting* mod)
{
	// Create the path to the ini file for this mod
	INFO("loading .ini for {}", mod->name);
	// Load the ini file
	CSimpleIniA ini;
	ini.SetUnicode();
	SI_Error rc = ini.LoadFile(mod->ini_path.c_str());
	if (rc != SI_OK) {
		// Handle error loading file
		INFO(".ini file for {} not found. Creating a new .ini file.", mod->name);
		flush_ini(mod);
		return;
	}

	std::vector<ModSettings::setting_base*> settings;
	get_all_settings(mod, settings);
	// Iterate through each setting in the group
	for (auto& setting_ptr : settings) {
		if (setting_ptr->ini_id.empty() || setting_ptr->ini_section.empty()) {
			ERROR("Undefined .ini serialization for setting {}; failed to load value.", setting_ptr->name.def);
			continue;
		}
		// Get the value of this setting from the ini file
		std::string value;
		bool use_default = false;
		if (ini.KeyExists(setting_ptr->ini_section.c_str(), setting_ptr->ini_id.c_str())) {
			value = ini.GetValue(setting_ptr->ini_section.c_str(), setting_ptr->ini_id.c_str(), "");
		} else {
			INFO("Value not found for setting {} in .ini file. Using default entry value.", setting_ptr->name.def);
			use_default = true;
		}
		if (use_default) {
			// Convert the value to the appropriate type and assign it to the setting
			if (setting_ptr->type == kEntryType_Checkbox) {
				dynamic_cast<setting_checkbox*>(setting_ptr)->value = dynamic_cast<setting_checkbox*>(setting_ptr)->default_value;
			} else if (setting_ptr->type == kEntryType_Slider) {
				dynamic_cast<setting_slider*>(setting_ptr)->value = dynamic_cast<setting_slider*>(setting_ptr)->default_value;
			} else if (setting_ptr->type == kEntryType_Textbox) {
				dynamic_cast<setting_textbox*>(setting_ptr)->value = dynamic_cast<setting_textbox*>(setting_ptr)->default_value;
			} else if (setting_ptr->type == kEntryType_Dropdown) {
				dynamic_cast<setting_dropdown*>(setting_ptr)->value = dynamic_cast<setting_dropdown*>(setting_ptr)->default_value;
			} else if (setting_ptr->type == kEntryType_Color) {
				dynamic_cast<setting_color*>(setting_ptr)->color.x = dynamic_cast<setting_color*>(setting_ptr)->default_color.x;
				dynamic_cast<setting_color*>(setting_ptr)->color.y = dynamic_cast<setting_color*>(setting_ptr)->default_color.y;
				dynamic_cast<setting_color*>(setting_ptr)->color.z = dynamic_cast<setting_color*>(setting_ptr)->default_color.z;
				dynamic_cast<setting_color*>(setting_ptr)->color.w = dynamic_cast<setting_color*>(setting_ptr)->default_color.w;
			} else if (setting_ptr->type == kEntryType_Keymap) {
				dynamic_cast<setting_keymap*>(setting_ptr)->value = dynamic_cast<setting_keymap*>(setting_ptr)->default_value;
			}
		} else {
			// Convert the value to the appropriate type and assign it to the setting
			if (setting_ptr->type == kEntryType_Checkbox) {
				dynamic_cast<setting_checkbox*>(setting_ptr)->value = (value == "true");
			} else if (setting_ptr->type == kEntryType_Slider) {
				dynamic_cast<setting_slider*>(setting_ptr)->value = std::stof(value);
			} else if (setting_ptr->type == kEntryType_Textbox) {
				dynamic_cast<setting_textbox*>(setting_ptr)->value = value;
			} else if (setting_ptr->type == kEntryType_Dropdown) {
				dynamic_cast<setting_dropdown*>(setting_ptr)->value = std::stoi(value);
			} else if (setting_ptr->type == kEntryType_Color) {
				uint32_t colUInt = std::stoul(value);
				dynamic_cast<setting_color*>(setting_ptr)->color.x = ((colUInt >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
				dynamic_cast<setting_color*>(setting_ptr)->color.y = ((colUInt >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
				dynamic_cast<setting_color*>(setting_ptr)->color.z = ((colUInt >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
				dynamic_cast<setting_color*>(setting_ptr)->color.w = ((colUInt >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
			} else if (setting_ptr->type == kEntryType_Keymap) {
				dynamic_cast<setting_keymap*>(setting_ptr)->value = std::stoi(value);
			}
		}

	}
	INFO(".ini loaded.");
}



/* Flush changes to MOD into its .ini file*/
void ModSettings::flush_ini(mod_setting* mod)
{
	CSimpleIniA ini;
	ini.SetUnicode();

	const std::string& path = mod->ini_path;

	// 1) Load existing INI to preserve non-JSON keys
	const SI_Error rc = ini.LoadFile(path.c_str());
	if (rc != SI_OK) {
		std::error_code ec;
		const bool exists = std::filesystem::exists(path, ec) && !ec;

		// If file exists but load failed, back it up and proceed with fresh INI
		if (exists) {
			const std::string backup_path = path + ".bak";
			std::error_code copy_ec;
			std::filesystem::copy_file(
				path,
				backup_path,
				std::filesystem::copy_options::overwrite_existing,
				copy_ec
			);

			if (copy_ec) {
				ERROR("Failed to create backup of INI {}: {}", backup_path, copy_ec.message());
			} else {
				INFO("Created backup of malformed INI: {}", backup_path);
			}
		}

		// Keep UI working even if INI is malformed
		ini.Reset();
	}

	// 2) Apply JSON-controlled settings only
	std::vector<ModSettings::setting_base*> settings;
	get_all_settings(mod, settings);

	for (auto* setting : settings) {
		if (!setting || setting->ini_id.empty() || setting->ini_section.empty()) {
			ERROR("Undefined .ini serialization for setting {}; failed to save value.",
				  setting ? setting->name.def : "null");
			continue;
		}

		std::string value;

		switch (setting->type) {
		case kEntryType_Checkbox:
			value = static_cast<setting_checkbox*>(setting)->value ? "true" : "false";
			break;

		case kEntryType_Slider:
			value = std::to_string(static_cast<setting_slider*>(setting)->value);
			break;

		case kEntryType_Textbox:
			value = static_cast<setting_textbox*>(setting)->value;
			break;

		case kEntryType_Dropdown:
			value = std::to_string(static_cast<setting_dropdown*>(setting)->value);
			break;

		case kEntryType_Color: {
			auto* sc = static_cast<setting_color*>(setting);

			auto clamp01 = [](float v) {
				return (v < 0.0f) ? 0.0f : (v > 1.0f) ? 1.0f : v;
			};

			const uint32_t r = static_cast<uint32_t>(clamp01(sc->color.x) * 255.0f + 0.5f);
			const uint32_t g = static_cast<uint32_t>(clamp01(sc->color.y) * 255.0f + 0.5f);
			const uint32_t b = static_cast<uint32_t>(clamp01(sc->color.z) * 255.0f + 0.5f);
			const uint32_t a = static_cast<uint32_t>(clamp01(sc->color.w) * 255.0f + 0.5f);

			const ImU32 col = IM_COL32(r, g, b, a);
			value = std::to_string(col);
			break;
		}

		case kEntryType_Keymap:
			value = std::to_string(static_cast<setting_keymap*>(setting)->value);
			break;

		default:
			continue;
		}

		ini.SetValue(setting->ini_section.c_str(), setting->ini_id.c_str(), value.c_str());
	}

	// 3) Atomic save: temp + replace
	const std::string temp_path = path + ".tmp." + std::to_string(GetCurrentProcessId());

	// Best-effort cleanup from a prior crash
	DeleteFileA(temp_path.c_str());

	if (ini.SaveFile(temp_path.c_str()) != SI_OK) {
		ERROR("Failed to write temp INI file for {}", mod->name);
		DeleteFileA(temp_path.c_str());
		return;
	}

	if (!MoveFileExA(
			temp_path.c_str(),
			path.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {

		ERROR("Failed to replace INI file for {} (error {})", mod->name, GetLastError());
		DeleteFileA(temp_path.c_str());
	}
}





/* Flush changes to MOD's settings to game settings.
 * NOTE: In this build, direct writes to RE::Setting are disabled because
 * the underlying API changed in newer CommonLibSSE-NG versions.
 * Settings are still persisted via INI/JSON; this function is a no-op.
 */
void ModSettings::flush_game_setting(mod_setting* mod)
{
	(void)mod;
}

void ModSettings::insert_game_setting(mod_setting* mod)
{
	(void)mod;
}

void ModSettings::save_all_game_setting()
{
}

void ModSettings::insert_all_game_setting()
{
}

const char* ModSettings::setting_keymap::keyid_to_str(int key_id)
{
	switch (key_id) {
	case 0: 
		return TR("key_unmapped", "Unmapped");
	case 1:
		return TR("key_escape", "Escape");
	case 2:
		return "1";
	case 3:
		return "2";
	case 4:
		return "3";
	case 5:
		return "4";
	case 6:
		return "5";
	case 7:
		return "6";
	case 8:
		return "7";
	case 9:
		return "8";
	case 10:
		return "9";
	case 11:
		return "0";
	case 12:
		return TR("key_minus", "Minus");
	case 13:
		return TR("key_equals", "Equals");
	case 14:
		return TR("key_backspace", "Backspace");
	case 15:
		return TR("key_tab", "Tab");
	case 16:
		return "Q";
	case 17:
		return "W";
	case 18:
		return "E";
	case 19:
		return "R";
	case 20:
		return "T";
	case 21:
		return "Y";
	case 22:
		return "U";
	case 23:
		return "I";
	case 24:
		return "O";
	case 25:
		return "P";
	case 26:
		return TR("key_left_bracket", "Left Bracket");
	case 27:
		return TR("key_right_bracket", "Right Bracket");
	case 28:
		return TR("key_enter", "Enter");
	case 29:
		return TR("key_left_ctrl", "Left Control");
	case 30:
		return "A";
	case 31:
		return "S";
	case 32:
		return "D";
	case 33:
		return "F";
	case 34:
		return "G";
	case 35:
		return "H";
	case 36:
		return "J";
	case 37:
		return "K";
	case 38:
		return "L";
	case 39:
		return TR("key_semicolon", "Semicolon");
	case 40:
		return TR("key_apostrophe", "Apostrophe");
	case 41:
		return TR("key_console", "~ (Console)");
	case 42:
		return TR("key_left_shift", "Left Shift");
	case 43:
		return TR("key_backslash", "Back Slash");
	case 44:
		return "Z";
	case 45:
		return "X";
	case 46:
		return "C";
	case 47:
		return "V";
	case 48:
		return "B";
	case 49:
		return "N";
	case 50:
		return "M";
	case 51:
		return TR("key_comma", "Comma");
	case 52:
		return TR("key_period", "Period");
	case 53:
		return TR("key_forward_slash", "Forward Slash");
	case 54:
		return TR("key_right_shift", "Right Shift");
	case 55:
		return TR("key_num_mul", "NUM*");
	case 56:
		return TR("key_left_alt", "Left Alt");
	case 57:
		return TR("key_spacebar", "Spacebar");
	case 58:
		return TR("key_caps_lock", "Caps Lock");
	case 59:
		return TR("key_f1", "F1");
	case 60:
		return TR("key_f2", "F2");
	case 61:
		return TR("key_f3", "F3");
	case 62:
		return TR("key_f4", "F4");
	case 63:
		return TR("key_f5", "F5");
	case 64:
		return TR("key_f6", "F6");
	case 65:
		return TR("key_f7", "F7");
	case 66:
		return TR("key_f8", "F8");
	case 67:
		return TR("key_f9", "F9");
	case 68:
		return TR("key_f10", "F10");
	case 69:
		return TR("key_num_lock", "Num Lock");
	case 70:
		return TR("key_scroll_lock", "Scroll Lock");
	case 71:
		return "NUM7";
	case 72:
		return "NUM8";
	case 73:
		return "NUM9";
	case 74:
		return "NUM-";
	case 75:
		return "NUM4";
	case 76:
		return "NUM5";
	case 77:
		return "NUM6";
	case 78:
		return "NUM+";
	case 79:
		return "NUM1";
	case 80:
		return "NUM2";
	case 81:
		return "NUM3";
	case 82:
		return "NUM0";
	case 83:
		return TR("key_num_decimal", "NUM.");
	case 87:
		return "F11";
	case 88:
		return "F12";
	case 156:
		return TR("key_num_enter", "NUM Enter");
	case 157:
		return TR("key_right_ctrl", "Right Control");
	case 181:
		return TR("key_num_div", "NUM/");
	case 183:
		return TR("key_print_screen", "SysRq / PtrScr");
	case 184:
		return TR("key_right_alt", "Right Alt");
	case 197:
		return TR("key_pause", "Pause");
	case 199:
		return TR("key_home", "Home");
	case 200:
		return TR("key_up", "Up Arrow");
	case 201:
		return TR("key_page_up", "PgUp");
	case 203:
		return TR("key_left", "Left Arrow");
	case 205:
		return TR("key_right", "Right Arrow");
	case 207:
		return TR("key_end", "End");
	case 208:
		return TR("key_down", "Down Arrow");
	case 209:
		return TR("key_page_down", "PgDown");
	case 210:
		return TR("key_insert", "Insert");
	case 211:
		return TR("key_delete", "Delete");
	case 256:
		return TR("key_mouse_left", "Left Mouse Button");
	case 257:
		return TR("key_mouse_right", "Right Mouse Button");
	case 258:
		return TR("key_mouse_middle", "Middle/Wheel Mouse Button");
	case 259:
		return TR("key_mouse_3", "Mouse Button 3");
	case 260:
		return TR("key_mouse_4", "Mouse Button 4");
	case 261:
		return TR("key_mouse_5", "Mouse Button 5");
	case 262:
		return TR("key_mouse_6", "Mouse Button 6");
	case 263:
		return TR("key_mouse_7", "Mouse Button 7");
	case 264:
		return TR("key_mouse_wheel_up", "Mouse Wheel Up");
	case 265:
		return TR("key_mouse_wheel_down", "Mouse Wheel Down");
	case 266:
		return TR("key_dpad_up", "DPAD_UP");
	case 267:
		return TR("key_dpad_down", "DPAD_DOWN");
	case 268:
		return TR("key_dpad_left", "DPAD_LEFT");
	case 269:
		return TR("key_dpad_right", "DPAD_RIGHT");
	case 270:
		return TR("key_start", "START");
	case 271:
		return TR("key_back", "BACK");
	case 272:
		return TR("key_left_thumb", "LEFT_THUMB");
	case 273:
		return TR("key_right_thumb", "RIGHT_THUMB");
	case 274:
		return TR("key_left_shoulder", "LEFT_SHOULDER");
	case 275:
		return TR("key_right_shoulder", "RIGHT_SHOULDER");
	case 276:
		return "A";
	case 277:
		return "B";
	case 278:
		return "X";
	case 279:
		return "Y";
	case 280:
		return TR("key_lt", "LT");
	case 281:
		return TR("key_rt", "RT");
	default:
		return TR("key_unknown", "Unknown Key");
	}
}
