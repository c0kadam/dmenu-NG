#include "PCH.h"
namespace Hooks
{
	class onWeatherChange
	{
	public:
		[[nodiscard]] static bool install(std::uintptr_t a_callSite);

	private:
		static void updateWeather(RE::Sky* a_sky);

		static inline REL::Relocation<decltype(updateWeather)> _updateWeather;
	};

	class OnInputEventDispatch
	{
	public:
		[[nodiscard]] static bool Install(std::uintptr_t a_callSite);

	private:
		static void DispatchInputEvent(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent** a_evns);
		static inline REL::Relocation<decltype(DispatchInputEvent)> _DispatchInputEvent;
	};
	[[nodiscard]] bool InstallWeatherHook();
	// Install once at kDataLoaded, after wheel plugins' input hooks.
	[[nodiscard]] bool InstallInputDispatch();
	[[nodiscard]] bool IsInputDispatchInstalled() noexcept;
}

