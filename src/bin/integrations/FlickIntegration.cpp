#include "PCH.h"

#include <cstdio>

#include "FlickIntegration.h"
#include "../Utils.h"
#include "../menus/ModSettings.h"

// The current FLICK public header also exposes helpers for a newer ImGui API.
// Keep its vendored copy unchanged while making those unused inline helpers
// parse with dMenu's pinned ImGui and Win32 headers.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#define LegacySize FontSize
#include "include/lib/FUCK_API.h"
#undef LegacySize
#pragma pop_macro("max")
#pragma pop_macro("min")

namespace
{
	class DMenuSettingsTool final : public FUCK::ITool
	{
	public:
		const char* Name() const override
		{
			return "dMenu Settings";
		}

		void Draw() override
		{
			const auto changedMods = ModSettings::ForEachSetting(
				[](std::string_view pageName) {
					FUCK::TextUnformatted(pageName.data(), pageName.data() + pageName.size());
					FUCK::Separator();
				},
				[](const ModSettings::CheckboxVisit& checkbox) -> std::optional<bool> {
					bool value = checkbox.value;
					FUCK::PushID(checkbox.identity);
					if (!checkbox.enabled) {
						FUCK::BeginDisabled();
					}
					const bool changed = FUCK::Checkbox(checkbox.label, &value);
					if (!checkbox.enabled) {
						FUCK::EndDisabled();
					}
					FUCK::PopID();
					return changed ? std::optional<bool>{ value } : std::nullopt;
				},
				[](const ModSettings::SliderVisit& slider) {
					const int stepCount = Utils::SliderStepIndex(slider.max, slider.min, slider.step);
					const int initialStepIndex = Utils::SliderStepIndex(slider.value, slider.min, slider.step);
					int stepIndex = initialStepIndex;
					char displayValue[64] = {};
					std::snprintf(displayValue, sizeof(displayValue), "%g", slider.value);
					FUCK::PushID(slider.identity);
					if (!slider.enabled) {
						FUCK::BeginDisabled();
					}
					FUCK::SliderInt(slider.label, &stepIndex, 0, stepCount, displayValue);
					const bool editCompleted = FUCK::IsItemDeactivatedAfterEdit();
					if (!slider.enabled) {
						FUCK::EndDisabled();
					}
					FUCK::PopID();
					return ModSettings::SliderUpdate{
						stepIndex != initialStepIndex ? std::optional<int>{ stepIndex } : std::nullopt,
						editCompleted
					};
				});

			for (auto* mod : changedMods) {
				ModSettings::CommitIniDirtyMod(mod);
			}
		}
	};
}

namespace FlickIntegration
{
	void InitializeAfterPluginsLoaded()
	{
		static bool initialized = false;
		if (initialized) {
			return;
		}
		initialized = true;

		if (!FUCK::Connect(Plugin::NAME.data())) {
			return;
		}

		static DMenuSettingsTool tool;
		FUCK::RegisterTool(&tool);
		logger::info("FLICK frontend registered");
	}
}
