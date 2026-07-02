#include "Hooks.h"
#include "menus/Trainer.h"
#include "Renderer.h"
#include "InputListener.h"
void Hooks::Install()
{
	SKSE::AllocTrampoline(1 << 5);
	onWeatherChange::install();
	OnInputEventDispatch::Install();
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
	// Capture the menu state BEFORE processing: if the menu was open when this input arrived, the
	// game must not see it — even if ProcessEvent closes the menu this frame (e.g. Escape or the
	// toggle key). Otherwise the closing keypress leaks through and, for Escape, also opens the
	// game's pause menu.
	const bool menuWasOpen = Renderer::IsEnabled();
	InputListener::GetSingleton()->ProcessEvent(a_evns);
	if (menuWasOpen || Renderer::IsEnabled()) {
		_DispatchInputEvent(a_dispatcher, dummy);
		return;
	} else {
		_DispatchInputEvent(a_dispatcher, a_evns);
	}
}
