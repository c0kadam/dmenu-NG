#include "WheelerCooperativeOpening.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <limits>
#include <mutex>
#include <type_traits>
#include <vector>

#include "Hooks.h"
#include "menus/Settings.h"

namespace
{
	inline constexpr std::uint32_t kApiVersion = 1;
	inline constexpr std::uint32_t kBindingSetVersion = 1;
	inline constexpr std::uint32_t kBindingVersion = 1;
	inline constexpr std::uint32_t kEventVersion = 1;
	inline constexpr std::int32_t kDMenuOpeningPriority = 100;
	inline constexpr std::uint32_t kGamepadOffset = 266;
	inline constexpr std::uint32_t kInvalidMappedKey = (std::numeric_limits<std::uint32_t>::max)();

	enum class Device : std::uint32_t
	{
		kMKB = 0,
		kGamepad = 1
	};

	enum class Edge : std::uint32_t
	{
		kUnknown = 0,
		kPrimaryDown = 1,
		kPrimaryUp = 2
	};

	inline constexpr std::uint32_t kConsumeOnGrant = 1u << 0;

	enum class ReplaceResult : std::uint32_t
	{
		kSuccess = 0
	};

	enum class Attestation : std::uint32_t
	{
		kMatchedForThisOwner = 0,
		kObservedButNotMatched = 1,
		kNoUpstreamAttestationAvailable = 2
	};

	enum class Reason : std::uint32_t
	{
		kNone = 0
	};

	struct Binding
	{
		std::uint32_t structSize;
		std::uint32_t descriptorVersion;
		Device device;
		std::uint32_t primaryMappedKey;
		std::uint32_t modifierMappedKey;
		Edge triggerEdge;
		std::int32_t priority;
		std::uint32_t semanticFlags;
	};

	struct BindingSet
	{
		std::uint32_t structSize;
		std::uint32_t structVersion;
		std::uint64_t ownerId;
		std::uint64_t bindingGeneration;
		std::uint32_t bindingCount;
		std::uint32_t bindingStride;
		const void* bindings;
	};

	struct EventObservation
	{
		std::uint32_t structSize;
		std::uint32_t structVersion;
		std::uint64_t consumerScopeToken;
		std::uintptr_t eventIdentity;
		std::uint64_t ownerId;
		std::uint64_t expectedBindingGeneration;
		Device device;
		std::uint32_t mappedKey;
		Edge edge;
	};

	struct EventDisposition
	{
		std::uint32_t structSize;
		std::uint32_t structVersion;
		Attestation attestation;
		Reason reason;
		std::uint64_t dispatchGeneration;
		std::uint64_t bindingGeneration;
		std::uint64_t opaqueGrantToken;
		std::uint32_t mustSuppressDownstream;
		std::uint32_t reserved;
	};

	struct API
	{
		std::uint32_t apiVersion;
		std::uint32_t structSize;
		ReplaceResult (*ReplaceCooperativeOpeningBindings)(const BindingSet* bindingSet);
		std::uint64_t (*BeginCooperativeOpeningConsumerScope)(std::uint64_t ownerId, std::uint64_t expectedBindingGeneration);
		Attestation (*ObserveAndClaimCooperativeOpeningEvent)(const EventObservation* observation, EventDisposition* disposition);
		Attestation (*GetCooperativeOpeningEventDisposition)(
			std::uint64_t consumerScopeToken,
			std::uintptr_t eventIdentity,
			std::uint64_t ownerId,
			EventDisposition* disposition);
		void (*EndCooperativeOpeningConsumerScope)(std::uint64_t consumerScopeToken);
	};

	using GetAPI = const API* (*)(std::uint32_t requestedVersion);

	static_assert(std::is_standard_layout_v<Binding> && std::is_trivially_copyable_v<Binding>);
	static_assert(std::is_standard_layout_v<BindingSet> && std::is_trivially_copyable_v<BindingSet>);
	static_assert(std::is_standard_layout_v<EventObservation> && std::is_trivially_copyable_v<EventObservation>);
	static_assert(std::is_standard_layout_v<EventDisposition> && std::is_trivially_copyable_v<EventDisposition>);
	static_assert(std::is_standard_layout_v<API> && std::is_trivially_copyable_v<API>);
	static_assert(sizeof(Binding) == 32);
	static_assert(sizeof(BindingSet) == 40);
	static_assert(sizeof(EventObservation) == 56);
	static_assert(sizeof(EventDisposition) == 48);
	static_assert(sizeof(API) == 48);

	std::atomic<const API*> s_api{ nullptr };
	std::atomic<std::uint64_t> s_publishedGeneration{ 0 };
	std::atomic_bool s_registrationActive{ false };
	std::mutex s_publishLock;
	bool s_permanentlyUnavailable = false;
	thread_local std::vector<std::uint64_t> s_activeConsumerTokens;

	bool IsPrimaryInDomain(Device device, std::uint32_t key)
	{
		return device == Device::kMKB ?
		           key >= 1 && key <= 265 :
		       device == Device::kGamepad ?
		           key >= 266 && key <= 281 :
		           false;
	}

	bool IsModifierInDomain(Device device, std::uint32_t key)
	{
		if (key == 0) {
			return true;
		}
		return device == Device::kMKB ?
		           key >= 1 && key <= 263 :
		       device == Device::kGamepad ?
		           key >= 266 && key <= 281 :
		           false;
	}

	bool AddBindingIfValid(
		std::array<Binding, 2>& bindings,
		std::uint32_t& count,
		Device device,
		std::uint32_t primary,
		std::uint32_t modifier)
	{
		if (primary == 0) {
			return true;
		}
		if (!IsPrimaryInDomain(device, primary) ||
		    !IsModifierInDomain(device, modifier) ||
		    primary == modifier) {
			logger::warn(
				"[CooperativeOpening] dMenu binding not published: device={} primary={} modifier={} reason=invalid_descriptor",
				device == Device::kGamepad ? "Gamepad" : "MKB",
				primary,
				modifier);
			return false;
		}
		bindings[count++] = Binding{
			.structSize = sizeof(Binding),
			.descriptorVersion = kBindingVersion,
			.device = device,
			.primaryMappedKey = primary,
			.modifierMappedKey = modifier,
			.triggerEdge = Edge::kPrimaryDown,
			.priority = kDMenuOpeningPriority,
			.semanticFlags = kConsumeOnGrant
		};
		return true;
	}

	bool IsValidAPI(const API* api)
	{
		return api &&
		       api->apiVersion == kApiVersion &&
		       api->structSize == sizeof(API) &&
		       api->ReplaceCooperativeOpeningBindings &&
		       api->BeginCooperativeOpeningConsumerScope &&
		       api->ObserveAndClaimCooperativeOpeningEvent &&
		       api->GetCooperativeOpeningEventDisposition &&
		       api->EndCooperativeOpeningConsumerScope;
	}

	bool TryResolveAPI()
	{
		if (s_api.load(std::memory_order_acquire)) {
			return true;
		}
		if (s_permanentlyUnavailable) {
			return false;
		}

		const HMODULE module = ::GetModuleHandleW(L"wheeler.dll");
		if (!module) {
			return false;
		}
		const auto getter = reinterpret_cast<GetAPI>(::GetProcAddress(module, "GetCooperativeOpeningAPI"));
		if (!getter) {
			s_permanentlyUnavailable = true;
			logger::info("[CooperativeOpening] Wheeler extension unavailable; dMenu standalone input remains active");
			return false;
		}
		const API* api = getter(kApiVersion);
		if (!IsValidAPI(api)) {
			s_permanentlyUnavailable = true;
			logger::warn("[CooperativeOpening] Wheeler extension ABI validation failed; integration disabled");
			return false;
		}
		s_api.store(api, std::memory_order_release);
		logger::info("[CooperativeOpening] dMenu connected to Wheeler extension v{}", kApiVersion);
		return true;
	}

	std::uint32_t MapGamepadKey(RE::BSWin32GamepadDevice::Key key)
	{
		using Key = RE::BSWin32GamepadDevice::Key;
		std::uint32_t index = kInvalidMappedKey;
		switch (key) {
		case Key::kUp: index = 0; break;
		case Key::kDown: index = 1; break;
		case Key::kLeft: index = 2; break;
		case Key::kRight: index = 3; break;
		case Key::kStart: index = 4; break;
		case Key::kBack: index = 5; break;
		case Key::kLeftThumb: index = 6; break;
		case Key::kRightThumb: index = 7; break;
		case Key::kLeftShoulder: index = 8; break;
		case Key::kRightShoulder: index = 9; break;
		case Key::kA: index = 10; break;
		case Key::kB: index = 11; break;
		case Key::kX: index = 12; break;
		case Key::kY: index = 13; break;
		case Key::kLeftTrigger: index = 14; break;
		case Key::kRightTrigger: index = 15; break;
		default: break;
		}
		return index == kInvalidMappedKey ? kInvalidMappedKey : index + kGamepadOffset;
	}

	EventObservation DescribeEvent(
		std::uint64_t token,
		std::uint64_t generation,
		RE::InputEvent* event)
	{
		EventObservation observation{
			.structSize = sizeof(EventObservation),
			.structVersion = kEventVersion,
			.consumerScopeToken = token,
			.eventIdentity = reinterpret_cast<std::uintptr_t>(event),
			.ownerId = WheelerCooperativeOpening::kOwnerId,
			.expectedBindingGeneration = generation,
			.device = Device::kMKB,
			.mappedKey = 0,
			.edge = Edge::kUnknown
		};
		if (!event) {
			return observation;
		}
		const auto* button = event->AsButtonEvent();
		if (!button) {
			return observation;
		}

		observation.mappedKey = button->GetIDCode();
		switch (button->GetDevice()) {
		case RE::INPUT_DEVICE::kKeyboard:
			observation.device = Device::kMKB;
			break;
		case RE::INPUT_DEVICE::kMouse:
			observation.device = Device::kMKB;
			observation.mappedKey += 256;
			break;
		case RE::INPUT_DEVICE::kGamepad:
			observation.device = Device::kGamepad;
			observation.mappedKey = MapGamepadKey(
				static_cast<RE::BSWin32GamepadDevice::Key>(button->GetIDCode()));
			break;
		default:
			observation.mappedKey = kInvalidMappedKey;
			break;
		}
		if (button->IsDown()) {
			observation.edge = Edge::kPrimaryDown;
		} else if (button->IsUp()) {
			observation.edge = Edge::kPrimaryUp;
		}
		return observation;
	}

	bool GetDisposition(
		std::uint64_t token,
		std::uintptr_t eventIdentity,
		EventDisposition& disposition)
	{
		const auto* api = s_api.load(std::memory_order_acquire);
		if (!api || token == 0 || eventIdentity == 0) {
			return false;
		}
		disposition = EventDisposition{
			.structSize = sizeof(EventDisposition),
			.structVersion = kEventVersion
		};
		api->GetCooperativeOpeningEventDisposition(
			token,
			eventIdentity,
			WheelerCooperativeOpening::kOwnerId,
			std::addressof(disposition));
		return true;
	}
}

void WheelerCooperativeOpening::Initialize()
{
	if (!Hooks::IsInputDispatchInstalled()) {
		ClearRegistration();
		return;
	}
	if (TryResolveAPI()) {
		PublishCurrentBindings();
	}
}

void WheelerCooperativeOpening::RetryInitializationAfterPluginsLoaded()
{
	if (!Hooks::IsInputDispatchInstalled()) {
		ClearRegistration();
		return;
	}
	if (s_api.load(std::memory_order_acquire)) {
		return;
	}
	if (TryResolveAPI()) {
		PublishCurrentBindings();
	}
}

void WheelerCooperativeOpening::PublishCurrentBindings()
{
	if (!Hooks::IsInputDispatchInstalled()) {
		ClearRegistration();
		return;
	}
	const auto* api = s_api.load(std::memory_order_acquire);
	if (!api) {
		return;
	}

	std::lock_guard lock(s_publishLock);
	std::array<Binding, 2> bindings{};
	std::uint32_t bindingCount = 0;
	AddBindingIfValid(
		bindings,
		bindingCount,
		Device::kMKB,
		Settings::key_toggle_dmenu_mkb,
		Settings::key_toggle_modifier_mkb);
	AddBindingIfValid(
		bindings,
		bindingCount,
		Device::kGamepad,
		Settings::key_toggle_dmenu_gamepad,
		Settings::key_toggle_modifier_gamepad);

	const auto currentGeneration = s_publishedGeneration.load(std::memory_order_relaxed);
	const auto nextGeneration = currentGeneration + 1;
	if (nextGeneration == 0) {
		logger::error("[CooperativeOpening] dMenu binding generation exhausted; publication disabled");
		return;
	}
	const BindingSet bindingSet{
		.structSize = sizeof(BindingSet),
		.structVersion = kBindingSetVersion,
		.ownerId = kOwnerId,
		.bindingGeneration = nextGeneration,
		.bindingCount = bindingCount,
		.bindingStride = sizeof(Binding),
		.bindings = bindingCount == 0 ? nullptr : bindings.data()
	};
	const auto result = api->ReplaceCooperativeOpeningBindings(std::addressof(bindingSet));
	if (result == ReplaceResult::kSuccess) {
		s_publishedGeneration.store(nextGeneration, std::memory_order_release);
		s_registrationActive.store(bindingCount != 0, std::memory_order_release);
		logger::info(
			"[CooperativeOpening] dMenu published generation={} bindings={}",
			nextGeneration,
			bindingCount);
	} else {
		logger::warn(
			"[CooperativeOpening] dMenu publication rejected result={} generation={} previous_generation_preserved={}",
			static_cast<std::uint32_t>(result),
			nextGeneration,
			currentGeneration);
	}
}

void WheelerCooperativeOpening::ClearRegistration()
{
	const auto* api = s_api.load(std::memory_order_acquire);
	if (!api) {
		return;
	}

	std::lock_guard lock(s_publishLock);
	const auto currentGeneration = s_publishedGeneration.load(std::memory_order_relaxed);
	if (currentGeneration == 0 || !s_registrationActive.load(std::memory_order_relaxed)) {
		return;
	}
	const auto nextGeneration = currentGeneration + 1;
	if (nextGeneration == 0) {
		logger::error("[CooperativeOpening] dMenu binding generation exhausted; registration could not be cleared");
		return;
	}
	const BindingSet bindingSet{
		.structSize = sizeof(BindingSet),
		.structVersion = kBindingSetVersion,
		.ownerId = kOwnerId,
		.bindingGeneration = nextGeneration,
		.bindingCount = 0,
		.bindingStride = sizeof(Binding),
		.bindings = nullptr
	};
	const auto result = api->ReplaceCooperativeOpeningBindings(std::addressof(bindingSet));
	if (result == ReplaceResult::kSuccess) {
		s_publishedGeneration.store(nextGeneration, std::memory_order_release);
		s_registrationActive.store(false, std::memory_order_release);
		logger::info("[CooperativeOpening] dMenu registration cleared generation={}", nextGeneration);
	} else {
		logger::error(
			"[CooperativeOpening] dMenu registration clear rejected result={} generation={}",
			static_cast<std::uint32_t>(result),
			nextGeneration);
	}
}

WheelerCooperativeOpening::ConsumerScope::ConsumerScope(RE::InputEvent** events)
{
	const auto* api = s_api.load(std::memory_order_acquire);
	const auto generation = s_publishedGeneration.load(std::memory_order_acquire);
	if (!api || generation == 0 || !events) {
		return;
	}
	_token = api->BeginCooperativeOpeningConsumerScope(kOwnerId, generation);
	if (_token == 0) {
		return;
	}

	for (auto* event = *events; event; event = event->next) {
		const auto observation = DescribeEvent(_token, generation, event);
		EventDisposition disposition{
			.structSize = sizeof(EventDisposition),
			.structVersion = kEventVersion
		};
		api->ObserveAndClaimCooperativeOpeningEvent(
			std::addressof(observation),
			std::addressof(disposition));
	}
	s_activeConsumerTokens.push_back(_token);
}

WheelerCooperativeOpening::ConsumerScope::~ConsumerScope()
{
	if (_token == 0) {
		return;
	}
	const auto* api = s_api.load(std::memory_order_acquire);
	if (api) {
		api->EndCooperativeOpeningConsumerScope(_token);
	}
	if (!s_activeConsumerTokens.empty() && s_activeConsumerTokens.back() == _token) {
		s_activeConsumerTokens.pop_back();
	} else {
		logger::error("[CooperativeOpening] dMenu consumer scope stack mismatch token={}", _token);
	}
}

void WheelerCooperativeOpening::ConsumerScope::SuppressOwnedEvents(RE::InputEvent** events) const
{
	if (_token == 0 || !events) {
		return;
	}
	RE::InputEvent* previous = nullptr;
	RE::InputEvent* event = *events;
	while (event) {
		RE::InputEvent* next = event->next;
		EventDisposition disposition{};
		const bool hasDisposition = GetDisposition(
			_token,
			reinterpret_cast<std::uintptr_t>(event),
			disposition);
		if (hasDisposition && disposition.mustSuppressDownstream != 0) {
			if (previous) {
				previous->next = next;
			} else {
				*events = next;
			}
		} else {
			previous = event;
		}
		event = next;
	}
}

bool WheelerCooperativeOpening::IsCurrentEventMatched(std::uintptr_t eventIdentity)
{
	if (s_activeConsumerTokens.empty()) {
		return false;
	}
	EventDisposition disposition{};
	if (!GetDisposition(s_activeConsumerTokens.back(), eventIdentity, disposition)) {
		return false;
	}
	return disposition.attestation == Attestation::kMatchedForThisOwner;
}
