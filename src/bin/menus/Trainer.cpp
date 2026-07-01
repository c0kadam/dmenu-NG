#include "imgui.h"
#include "imgui_internal.h"
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "bin/Utils.h"
#include "Trainer.h"
#include "Translator.h"

namespace World
{
	namespace Time
	{
		bool _sliderActive = false;

		void show()
		{
			auto calendar = RE::Calendar::GetSingleton();
			float* ptr = nullptr;
			if (calendar) {
				ptr = &(calendar->gameHour->value);
				if (ptr) {
					ImGui::SliderFloat(TR("trainer_time_slider", "Time"), ptr, 0.0f, 24.0f);
					_sliderActive = ImGui::IsItemActive();
					return;
				}
			}

			ImGui::Text("%s", TR("trainer_time_not_found", "Time address not found"));
		}
	}  // namespace Time

	namespace Weather
	{
		// after ForceWeather, `region` field of sky may get reset. restore from this.
		RE::TESRegion* _currRegionCache = nullptr;

		RE::TESRegion* getCurrentRegion()
		{
			if (auto sky = RE::Sky::GetSingleton()) {
				auto ret = sky->region;
				if (ret) {
					_currRegionCache = ret;
					return ret;
				}
			}
			return _currRegionCache;
		}

		// maps to store formEditorID as names for regions and weathers as they're discarded by Bethesda
		std::unordered_map<RE::TESRegion*, std::string> _regionNames;
		std::unordered_map<RE::TESWeather*, std::string> _weatherNames;

		std::vector<RE::TESWeather*> _weathersToSelect;
		static std::vector<RE::TESFile*> _mods;  // plugins containing weathers
		static uint8_t _mods_i = 0;              // selected weather plugin (deprecated)
		bool _cached = false;

		bool _showCurrRegionOnly = true;  // only show weather corresponding to current region
		bool _lockWeather = false;

		static std::vector<std::pair<std::string, bool>> _filters = {
			{ "Pleasant", false },
			{ "Cloudy", false },
			{ "Rainy", false },
			{ "Snow", false },
			{ "Permanent Aurora", false },
			{ "Aurora Follows Sun", false }
		};

		void cache()
		{
			_weathersToSelect.clear();

			auto dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) {
				return;
			}

			auto regionDataManager = dataHandler->regionDataManager;
			if (!regionDataManager) {
				return;
			}

			for (auto weather : dataHandler->GetFormArray<RE::TESWeather>()) {
				if (!weather) {
					continue;
				}

				auto flags = weather->data.flags;
				if (_filters[0].second && !flags.any(RE::TESWeather::WeatherDataFlag::kPleasant)) {
					continue;
				}
				if (_filters[1].second && !flags.any(RE::TESWeather::WeatherDataFlag::kCloudy)) {
					continue;
				}
				if (_filters[2].second && !flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
					continue;
				}
				if (_filters[3].second && !flags.any(RE::TESWeather::WeatherDataFlag::kSnow)) {
					continue;
				}
				if (_filters[4].second && !flags.any(RE::TESWeather::WeatherDataFlag::kPermAurora)) {
					continue;
				}
				if (_filters[5].second && !flags.any(RE::TESWeather::WeatherDataFlag::kAuroraFollowsSun)) {
					continue;
				}

				if (_showCurrRegionOnly) {
					bool belongsToCurrRegion = false;
					auto currRegion = getCurrentRegion();
					if (currRegion && currRegion->dataList) {
						// could cache regionData -> weathers at the cost of extra space,
						// but this runs fine in O(n^2) since there are limited weathers
						for (RE::TESRegionData* regionData : currRegion->dataList->regionDataList) {
							if (regionData->GetType() == RE::TESRegionData::Type::kWeather) {
								RE::TESRegionDataWeather* weatherData = regionDataManager->AsRegionDataWeather(regionData);
								for (auto t : weatherData->weatherTypes) {
									if (t->weather == weather) {
										belongsToCurrRegion = true;
									}
								}
							}
						}
					}
					if (!belongsToCurrRegion) {
						continue;
					}
				}

				_weathersToSelect.push_back(weather);
			}
		}

		void show()
		{
			RE::TESRegion* currRegion = nullptr;
			RE::TESWeather* currWeather = nullptr;

			if (auto sky = RE::Sky::GetSingleton()) {
				currRegion = getCurrentRegion();
				currWeather = sky->currentWeather;
			}

			// Build preview label (fallback to form ID if name is empty)
			std::string previewLabel;
			const char* previewPtr = "";
			if (currWeather) {
				std::string baseName;
				if (auto it = _weatherNames.find(currWeather);
					it != _weatherNames.end() && !it->second.empty()) {
					baseName = it->second;
				} else {
					baseName = fmt::format("{:08X}", currWeather->GetFormID());
				}

				if (const char* tr = Translator::Translate(baseName)) {
					previewLabel = tr;
				} else {
					previewLabel = baseName;
				}
				previewPtr = previewLabel.c_str();
			}

			// Determine current index in the cached weather list
			int currentIndex = -1;
			for (int i = 0; i < static_cast<int>(_weathersToSelect.size()); ++i) {
				if (_weathersToSelect[i] == currWeather) {
					currentIndex = i;
					break;
				}
			}

			bool comboOpen = ImGui::BeginCombo("##Weathers", previewPtr);
			if (comboOpen) {
				for (auto* weather : _weathersToSelect) {
					bool isSelected = (weather == currWeather);

					// Ensure every selectable has a unique ID even if multiple
					// weathers share the same (or empty) display name.
					std::string baseName;
					if (auto it = _weatherNames.find(weather);
						it != _weatherNames.end() && !it->second.empty()) {
						baseName = it->second;
					} else {
						baseName = fmt::format("{:08X}", weather->GetFormID());
					}

					std::string visibleName;
					if (const char* tr = Translator::Translate(baseName)) {
						visibleName = tr;
					} else {
						visibleName = baseName;
					}

					std::string label = fmt::format("{}##{}", visibleName, weather->GetFormID());

					if (ImGui::Selectable(label.c_str(), isSelected)) {
						if (!isSelected) {
							if (auto sky = RE::Sky::GetSingleton()) {
								sky->ForceWeather(weather, true);
								currWeather = weather;
							}
						}
					}
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			// Allow scrolling or using arrow keys over the closed combo to change weather
			ImVec2 comboMin = ImGui::GetItemRectMin();
			ImVec2 comboMax = ImGui::GetItemRectMax();
			ImVec2 mousePos = ImGui::GetIO().MousePos;
			bool   hovered  = mousePos.x >= comboMin.x && mousePos.x <= comboMax.x &&
                            mousePos.y >= comboMin.y && mousePos.y <= comboMax.y;

			auto& io = ImGui::GetIO();

			auto advanceWeather = [&](int direction) {
				if (direction == 0 || _weathersToSelect.empty()) {
					return;
				}

				int index = currentIndex;
				if (index < 0) {
					index = 0;
				}

				index += direction;
				if (index < 0 || index >= static_cast<int>(_weathersToSelect.size())) {
					return;
				}

				auto* nextWeather = _weathersToSelect[index];
				if (auto sky = RE::Sky::GetSingleton()) {
					sky->ForceWeather(nextWeather, true);
				}
			};

			if (!comboOpen && hovered) {
				// Mouse wheel
				if (io.MouseWheel != 0.0f) {
					int direction = io.MouseWheel > 0.0f ? -1 : 1;
					advanceWeather(direction);
				}

				// Arrow keys
				if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
					advanceWeather(-1);
				} else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
					advanceWeather(1);
				}
			}
			//ImGui::SameLine();
			//ImGui::Checkbox("Lock Weather", &_lockWeather);

			// Display filtering controls
			ImGui::Text("%s", TR("trainer_weather_flags", "Flags:"));

			for (int i = 0; i < static_cast<int>(_filters.size()); i++) {
				const std::string key = "trainer_flag_" + _filters[i].first;
				const char* label = TR(key.c_str(), _filters[i].first.c_str());
				if (ImGui::Checkbox(label, &_filters[i].second)) {
					_cached = false;
				}
				if (i < static_cast<int>(_filters.size()) - 1) {
					ImGui::SameLine();
				}
			}

			// Display current region
			if (currRegion) {
				ImGui::Text("%s", TR("trainer_current_region", "Current Region:"));
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", _regionNames[currRegion].c_str());
			} else {
				ImGui::Text("%s", TR("trainer_region_not_found", "Current region not found"));
			}

			ImGui::SameLine();

			ImGui::Checkbox(TR("trainer_only_current_region", "Only Current Region"), &_showCurrRegionOnly);
			if (_showCurrRegionOnly) {
				ImGui::SameLine();
				ImGui::TextDisabled("(?!)");
				if (ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					ImGui::Text(
						"%s",
						TR("trainer_only_current_region_tip", "Show only weathers valid for the current region."));
					ImGui::EndTooltip();
				}
			}

			if (!_cached) {
				cache();
				_cached = true;
			}
		}

		void init()
		{
			_cached = false;

			// load list of plugins with available weathers
			std::unordered_set<RE::TESFile*> plugins;
			Utils::loadUsefulPlugins<RE::TESWeather>(plugins);
			for (auto plugin : plugins) {
				_mods.push_back(plugin);
			}

			auto data = RE::TESDataHandler::GetSingleton();
			if (!data) {
				return;
			}

			// load region & weather names (EditorID or hex FormID)
			for (RE::TESWeather* weather : data->GetFormArray<RE::TESWeather>()) {
				std::string id = Utils::getFormEditorID(weather);
				if (id.empty()) {
					if (auto editorID = weather->GetFormEditorID(); editorID && *editorID) {
						id = editorID;
					}
				}
				if (id.empty()) {
					id = fmt::format("{:08X}", weather->GetFormID());
				}
				_weatherNames.insert({ weather, id });
			}

			for (RE::TESRegion* region : data->GetFormArray<RE::TESRegion>()) {
				std::string id = Utils::getFormEditorID(region);
				if (id.empty()) {
					if (auto editorID = region->GetFormEditorID(); editorID && *editorID) {
						id = editorID;
					}
				}
				if (id.empty()) {
					id = fmt::format("{:08X}", region->GetFormID());
				}
				_regionNames.insert({ region, id });
			}
		}
	}  // namespace Weather

	void show()
	{
		if (!RE::Sky::GetSingleton() || !RE::Sky::GetSingleton()->currentWeather) {
			ImGui::Text("%s", TR("trainer_world_not_loaded", "World not loaded"));
			return;
		}

		// Use consistent padding and alignment
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 5));

		// Indent the contents of the window
		ImGui::Indent();
		ImGui::Spacing();

		// Display time controls
		ImGui::Text("%s", TR("trainer_time_label", "Time:"));
		Time::show();
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Display weather controls
		ImGui::Text("%s", TR("trainer_weather_label", "Weather:"));
		Weather::show();

		// Unindent the contents of the window
		ImGui::Unindent();

		// Use consistent padding and alignment
		ImGui::PopStyleVar(2);
	}

	void init()
	{
		Weather::init();
	}
}  // namespace World

void Trainer::show()
{
	ImGui::PushID("World");
	if (ImGui::CollapsingHeader(TR("trainer_world_header", "World"))) {
		World::show();
	}
	ImGui::PopID();
}

void Trainer::init()
{
	// Weather
	World::init();

	INFO("Trainer initialized.");
}

bool Trainer::isWeatherLocked()
{
	return World::Time::_sliderActive;
}

// deprecated code
//if (ImGui::BeginCombo("WeatherMod", _mods[_mods_i]->GetFilename().data())) {
//	for (int i = 0; i < _mods.size(); i++) {
//		bool isSelected = (_mods[_mods_i] == _mods[i]);
//		if (ImGui::Selectable(_mods[i]->GetFilename().data(), isSelected)) {
//			_mods_i = i;
//			_cached = false;
//		}
//		if (isSelected) {
//			ImGui::SetItemDefaultFocus();
//		}
//	}
//	ImGui::EndCombo();
//}
