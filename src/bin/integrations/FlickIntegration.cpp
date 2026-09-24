#include "PCH.h"

#include "FlickIntegration.h"
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
			const auto changedMods = ModSettings::ForEachCheckbox(
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
