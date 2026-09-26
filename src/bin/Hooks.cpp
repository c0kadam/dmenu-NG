#include "Hooks.h"
#include "menus/Trainer.h"
#include "Renderer.h"
#include "InputListener.h"
#include "RuntimeCompatibility.h"
#include "WheelerCooperativeOpening.h"

#include <atomic>
#include <mutex>

namespace
{
	std::mutex s_installLock;
	bool s_weatherHookInstalled = false;
	bool s_inputDispatchAttempted = false;
	std::atomic_bool s_inputDispatchInstalled = false;
}

bool Hooks::InstallWeatherHook()
{
	std::lock_guard lock(s_installLock);
	if (s_weatherHookInstalled) {
		return true;
	}

	const auto* weather = RuntimeCompatibility::GetResolvedCallSite(RuntimeCompatibility::Hook::Weather);
	if (!weather) {
		logger::error("Weather hook preflight was not completed; weather hook was not installed"sv);
		return false;
	}

	if (!onWeatherChange::install(weather->address)) {
		return false;
	}
	s_weatherHookInstalled = true;
	return true;
}

bool Hooks::InstallInputDispatch()
{
	std::lock_guard lock(s_installLock);
	if (s_inputDispatchInstalled.load(std::memory_order_acquire)) {
		return true;
	}
	// Never re-hook this thunk, even if a previous patch attempt failed.
	if (s_inputDispatchAttempted) {
		return false;
	}
	s_inputDispatchAttempted = true;

	const auto* input = RuntimeCompatibility::GetResolvedCallSite(RuntimeCompatibility::Hook::InputEventDispatch);
	if (!input || !RuntimeCompatibility::RevalidateCallSite(RuntimeCompatibility::Hook::InputEventDispatch)) {
		logger::error("Input-dispatch callsite validation failed; input hook was not installed"sv);
		return false;
	}
	if (!OnInputEventDispatch::Install(input->address)) {
		return false;
	}
	s_inputDispatchInstalled.store(true, std::memory_order_release);
	return true;
}

bool Hooks::IsInputDispatchInstalled() noexcept
{
	return s_inputDispatchInstalled.load(std::memory_order_acquire);
}

bool Hooks::onWeatherChange::install(std::uintptr_t a_callSite)
{
	auto& trampoline = SKSE::GetTrampoline();
	const auto original = trampoline.write_call<5>(a_callSite, updateWeather);
	if (original == 0) {
		logger::error("Failed to install weather hook"sv);
		return false;
	}
	_updateWeather = original;
	return true;
}

bool Hooks::OnInputEventDispatch::Install(std::uintptr_t a_callSite)
{
	auto& trampoline = SKSE::GetTrampoline();
	const auto original = trampoline.write_call<5>(a_callSite, DispatchInputEvent);
	if (original == 0) {
		logger::error("Failed to install input-dispatch hook"sv);
		return false;
	}
	_DispatchInputEvent = original;
	return true;
}

void Hooks::onWeatherChange::updateWeather(RE::Sky* a_sky)
{
	if (Trainer::isWeatherLocked()) {
		return;
	}
	_updateWeather(a_sky);
}

void Hooks::OnInputEventDispatch::DispatchInputEvent(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent** a_evns)
{
	static RE::InputEvent* dummy[] = { nullptr };
	if (!a_evns) {
		_DispatchInputEvent(a_dispatcher, a_evns);
		return;
	}
	WheelerCooperativeOpening::ConsumerScope cooperativeOpeningScope(a_evns);
	InputListener::GetSingleton()->ProcessEvent(a_evns);
	cooperativeOpeningScope.SuppressOwnedEvents(a_evns);
	if (Renderer::IsEnabled()) {
		_DispatchInputEvent(a_dispatcher, dummy);
		return;
	} else {
		_DispatchInputEvent(a_dispatcher, a_evns);
	}
}
