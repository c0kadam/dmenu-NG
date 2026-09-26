#include "Renderer.h"
#include "InputListener.h"
#include "Hooks.h"
#include "DMenuAPI.h"
#include "integrations/FlickIntegration.h"
#include "integrations/SkseMenuFrameworkIntegration.h"
#include "menus/ModSettings.h"
#include "menus/Settings.h"
#include "menus/Trainer.h"
#include "RuntimeCompatibility.h"
#include "WheelerCooperativeOpening.h"
#include "Utils.h"
#include "ime/SimpleIMEBridge.h"

void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		// Load Interface\Translations\dmenu_<LANG>.txt once Scaleform is ready.
		SKSE::Translation::ParseTranslation(std::string(Plugin::NAME));
		Utils::InitializeFormEditorIDCache();
		Trainer::init();

		if (Hooks::InstallInputDispatch()) {
			WheelerCooperativeOpening::Initialize();
		} else {
			logger::error("Deferred input hook installation failed; dMenu physical input is unavailable"sv);
		}

		// Restore configured values after plugins have applied their game-setting overrides.
		ModSettings::save_all_game_setting();
		ModSettings::SendAllSettingsUpdateEvent();
		break;
	case SKSE::MessagingInterface::kPostLoad:
		IME::SimpleIMEBridge::Get().DetectAfterPluginsLoaded();
		FlickIntegration::InitializeAfterPluginsLoaded();
		SkseMenuFrameworkIntegration::InitializeAfterPluginsLoaded();
		DMenuAPI::DispatchInterface();
		break;
	case SKSE::MessagingInterface::kPostLoadGame:
		break;
	}
}

[[nodiscard]] bool onSKSEInit()
{
	Settings::init();
	ModSettings::init(); // init modsetting before everyone else
	SKSE::AllocTrampoline(14 * 4);
	if (!Renderer::Install() || !Hooks::InstallWeatherHook()) {
		return false;
	}
	return true;
}

namespace
{
	void InitializeLog()
	{
#ifndef NDEBUG
		auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
		auto path = logger::log_directory();
		if (!path) {
			util::report_and_fail("Failed to find standard logging directory"sv);
		}

		*path /= fmt::format("{}.log"sv, Plugin::NAME);
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

		const auto level = spdlog::level::err;

		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(level);
		log->flush_on(spdlog::level::err);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("%s(%#): [%^%l%$] %v"s);
	}

	void LogStartupBanner(const SKSE::LoadInterface* a_skse)
	{
		auto log = spdlog::default_logger();
		if (!log) {
			return;
		}

		const auto previousLevel = log->level();
		log->set_level(spdlog::level::info);
		logger::info("{} v{} by {}"sv, Plugin::NAME, Plugin::VERSION.string(), Plugin::AUTHOR);
		logger::info("Runtime {}"sv, a_skse->RuntimeVersion().string("."));
		logger::info("CommonLibSSE-NG {} ({})"sv, DMENU_COMMONLIBSSE_NG_VERSION, DMENU_COMMONLIBSSE_NG_REVISION);
		logger::info("Log mode: startup banner + errors only"sv);
		log->flush();
		log->set_level(previousLevel);
	}
}

std::string wstring2string(const std::wstring& wstr, UINT CodePage)

{

	std::string ret;

	int len = WideCharToMultiByte(CodePage, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);

	ret.resize((size_t)len, 0);

	WideCharToMultiByte(CodePage, 0, wstr.c_str(), (int)wstr.size(), &ret[0], len, NULL, NULL);

	return ret;

}


extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = Plugin::NAME.data();
	a_info->version = Plugin::VERSION[0];

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	const auto ver = a_skse->RuntimeVersion();
	if (!RuntimeCompatibility::IsSupported(ver)) {
		logger::critical(FMT_STRING("Unsupported runtime version {}"), ver.string());
		return false;
	}

	return true;
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;

	v.PluginVersion(Plugin::VERSION);
	v.PluginName(Plugin::NAME);
	v.AuthorName(Plugin::AUTHOR);

	// Explicit versions are intentional: these hooks use audited call-site offsets,
	// so Address Library availability alone does not make future runtimes safe.
	v.CompatibleVersions({
		RuntimeCompatibility::SKYRIM_1_5_97,
		RuntimeCompatibility::SKYRIM_1_6_1170,
		RuntimeCompatibility::SKYRIM_1_7_99,
		RuntimeCompatibility::SKYRIM_1_7_104
	});
	v.MinimumRequiredXSEVersion({ 2, 0, 20, 0 });
	// The runtime list above is authoritative. Do not advertise an Address
	// Library independence mode that this call-site-hooking plugin does not use.
	v.versionIndependenceEx = 0;

	return v;
}();


extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	LogStartupBanner(a_skse);

	if (!RuntimeCompatibility::IsSupported(a_skse->RuntimeVersion())) {
		logger::critical("Unsupported Skyrim runtime {}; plugin load aborted"sv, a_skse->RuntimeVersion().string("."));
		return false;
	}

	SKSE::Init(a_skse);
	if (!RuntimeCompatibility::PreflightHooks()) {
		return false;
	}

	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener("SKSE", MessageHandler)) {
		logger::critical("Failed to register the SKSE messaging listener"sv);
		return false;
	}
	
	if (!onSKSEInit()) {
		logger::critical("Hook installation failed; plugin load aborted"sv);
		return false;
	}

	return true;
}
