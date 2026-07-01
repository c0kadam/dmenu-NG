#include "Renderer.h"
#include "InputListener.h"
#include "Hooks.h"
#include "DMenuAPI.h"
#include "menus/ModSettings.h"
#include "menus/Settings.h"

void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		// Load Interface\Translations\dmenu_<LANG>.txt once Scaleform is ready.
		SKSE::Translation::ParseTranslation(std::string(Plugin::NAME));

		Hooks::Install();
		ModSettings::save_all_game_setting();  // in case some .esp overwrite the game setting // fixme
		ModSettings::SendAllSettingsUpdateEvent(); // notify all mods to update their settings
		break;
	case SKSE::MessagingInterface::kPostLoad:
		DMenuAPI::DispatchInterface();
		break;
	case SKSE::MessagingInterface::kPostLoadGame:
		break;
	}
}

void onSKSEInit()
{
	Renderer::Install();
	Settings::init();
	ModSettings::init(); // init modsetting before everyone else
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
		logger::info("Runtime {}"sv, a_skse->RuntimeVersion().string());
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
	if (ver < SKSE::RUNTIME_SSE_1_5_39) {
		logger::critical(FMT_STRING("Unsupported runtime version {}"), ver.string());
		return false;
	}

	return true;
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;

	v.PluginVersion(Plugin::VERSION);
	v.PluginName(Plugin::NAME);

	v.UsesAddressLibrary(true);
	v.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST });
	v.HasNoStructUse(true);

	return v;
}();


extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	LogStartupBanner(a_skse);

	SKSE::Init(a_skse);

	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		return false;
	}
	
	onSKSEInit();

	return true;
}
