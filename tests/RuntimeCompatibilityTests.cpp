// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (c) 2026 C0kadam
// See LICENSE and EXCEPTIONS.md for the project terms.

#include "HookValidation.h"
#include "CallSiteDiscovery.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
	using namespace RuntimeCompatibility::Validation;

	int g_failures = 0;

	void Expect(bool a_condition, const char* a_name)
	{
		if (!a_condition) {
			std::cerr << "FAILED: " << a_name << '\n';
			++g_failures;
		}
	}

	void SelfTarget()
	{}

	[[nodiscard]] std::uintptr_t GetAllocationBase(std::uintptr_t a_address)
	{
		MEMORY_BASIC_INFORMATION memoryInfo{};
		if (::VirtualQuery(reinterpret_cast<const void*>(a_address), &memoryInfo, sizeof(memoryInfo)) == 0) {
			return 0;
		}
		return reinterpret_cast<std::uintptr_t>(memoryInfo.AllocationBase);
	}

	void WriteAbsoluteRelay(void* a_relay, std::uintptr_t a_destination)
	{
		const std::uint8_t relayPrefix[]{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
		std::memcpy(a_relay, relayPrefix, sizeof(relayPrefix));
		std::memcpy(static_cast<std::uint8_t*>(a_relay) + sizeof(relayPrefix), &a_destination, sizeof(a_destination));
	}

	class VirtualPage
	{
	public:
		explicit VirtualPage(DWORD a_protection) :
			_memory(::VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, a_protection))
		{}

		~VirtualPage()
		{
			if (_memory) {
				::VirtualFree(_memory, 0, MEM_RELEASE);
			}
		}

		VirtualPage(const VirtualPage&) = delete;
		VirtualPage& operator=(const VirtualPage&) = delete;

		[[nodiscard]] void* get() const noexcept { return _memory; }
		[[nodiscard]] std::uintptr_t address() const noexcept { return reinterpret_cast<std::uintptr_t>(_memory); }

	private:
		void* _memory;
	};

	using RuntimeCompatibility::Hook;
	constexpr std::uintptr_t kFixtureBase = 0x100000;
	constexpr std::uintptr_t kFixtureTarget = 0x200000;

	std::vector<std::uint8_t> CallFixture(Hook a_hook, std::uintptr_t a_base = kFixtureBase,
		std::uintptr_t a_target = kFixtureTarget)
	{
		std::vector<std::uint8_t> bytes(64, 0x90);
		constexpr std::size_t callOffset = 16;
		if (a_hook == Hook::DXGIPresent) {
			const std::uint8_t before[]{ 0xB9, 1, 0, 0, 0 };
			std::memcpy(bytes.data() + callOffset - sizeof(before), before, sizeof(before));
			const std::uint8_t after[]{ 0x80, 0x3D, 0, 0, 0, 0, 0 };
			std::memcpy(bytes.data() + callOffset + 5, after, sizeof(after));
		} else if (a_hook == Hook::D3DInit) {
			const std::uint8_t after[]{ 0x44, 0x39, 0x6E, 0x2C };
			std::memcpy(bytes.data() + callOffset + 5, after, sizeof(after));
		} else {
			const std::uint8_t before[]{ 0x48, 0x8B, static_cast<std::uint8_t>(a_hook == Hook::Weather ? 0xCF : 0xCE) };
			std::memcpy(bytes.data() + callOffset - sizeof(before), before, sizeof(before));
			const std::uint8_t after[]{ 0x48, 0x8B, static_cast<std::uint8_t>(a_hook == Hook::Weather ? 0xCF : 0x0D), 0, 0, 0, 0 };
			std::memcpy(bytes.data() + callOffset + 5, after, a_hook == Hook::Weather ? 3 : sizeof(after));
		}
		bytes[callOffset] = 0xE8;
		const auto displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(a_target) - (a_base + callOffset + 5));
		std::memcpy(bytes.data() + callOffset + 1, &displacement, sizeof(displacement));
		return bytes;
	}

	void TestDiscovery()
	{
		using namespace RuntimeCompatibility;
		constexpr std::array<RuntimeVersion, 4> known{
			RuntimeVersion{ 1, 5, 97, 0 }, RuntimeVersion{ 1, 6, 1170, 0 },
			RuntimeVersion{ 1, 7, 99, 0 }, RuntimeVersion{ 1, 7, 104, 0 }
		};
		for (std::size_t index = 0; index < known.size(); ++index) {
			const auto* offsets = FindRuntimeOffsets(known[index]);
			Expect(SelectResolutionPath(known[index]) == ResolutionPath::Audited,
				"known runtime selects audited path, never discovery");
			Expect(offsets && offsets->d3dInit == (index == 0 ? 0x9 : 0x275) && offsets->dxgiPresent == 0x9 &&
				offsets->weather == (index == 0 ? 0x29C : 0x3E6) && offsets->inputEventDispatch == 0x7B,
				"all known audited offsets are preserved");
		}
		Expect(SelectResolutionPath({ 1, 6, 1179, 0 }) == ResolutionPath::Discovery && !FindRuntimeOffsets({ 1, 6, 1179, 0 }),
			"GOG example is unverified and selects discovery");
		Expect(SelectResolutionPath({ 1, 6, 1179, 1 }) == ResolutionPath::Discovery, "storefront component does not block discovery");
		Expect(SelectResolutionPath({ 1, 6, 1170, 1 }) == ResolutionPath::Discovery, "different fourth component is not silently treated as audited");
		Expect(SelectResolutionPath({ 1, 4, 15, 0 }) == ResolutionPath::Unsupported &&
			SelectResolutionPath({ 1, 5, 98, 0 }) == ResolutionPath::Unsupported, "unknown SE/VR fail closed");
		const TargetValidator expectedTarget = [](auto, auto target) {
			return ClassifyTarget(ChainPolicy::ExecutableChainAllowed, { true, target == kFixtureTarget, false, false });
		};
		for (const auto hook : { Hook::D3DInit, Hook::DXGIPresent, Hook::Weather, Hook::InputEventDispatch }) {
			auto bytes = CallFixture(hook);
			auto result = DiscoverCallSites(hook, true, bytes, kFixtureBase, expectedTarget);
			Expect(result.status == DiscoveryStatus::Unique && result.selected && result.selected->relativeOffset == 16 &&
				result.selected->target == kFixtureTarget, "one fully validated candidate succeeds for each hook");
			auto second = CallFixture(hook, kFixtureBase + bytes.size());
			bytes.insert(bytes.end(), second.begin(), second.end());
			result = DiscoverCallSites(hook, true, bytes, kFixtureBase, expectedTarget);
			Expect(result.status == DiscoveryStatus::Ambiguous && result.validCount == 2 && !result.selected,
				"multiple valid candidates fail instead of choosing first/nearest");
			bytes = CallFixture(hook);
			bytes[16] = 0xE9;
			Expect(!DiscoverCallSites(hook, true, bytes, kFixtureBase, expectedTarget).selected, "wrong opcode rejected by discovery");
			bytes = CallFixture(hook);
			bytes[23] ^= 1;
			if (hook == Hook::DXGIPresent) { bytes[21] = 0x81; }
			Expect(!DiscoverCallSites(hook, true, bytes, kFixtureBase, expectedTarget).selected, "wrong surrounding context rejected");
			bytes = CallFixture(hook, kFixtureBase, kFixtureTarget + 1);
			Expect(!DiscoverCallSites(hook, true, bytes, kFixtureBase, expectedTarget).selected, "right context but wrong target rejected");
		}
		std::vector<std::uint8_t> noCalls(64, 0x90);
		Expect(DiscoverCallSites(Hook::Weather, true, noCalls, kFixtureBase, expectedTarget).status == DiscoveryStatus::NoMatch,
			"zero candidates fails");
		noCalls.resize(kMaxDiscoveryFunctionBytes + 1);
		Expect(DiscoverCallSites(Hook::Weather, true, noCalls, kFixtureBase, expectedTarget).status == DiscoveryStatus::InvalidRange,
			"oversized function is not partially scanned");
		auto truncated = CallFixture(Hook::Weather);
		truncated.back() = 0xE8;
		Expect(DiscoverCallSites(Hook::Weather, true, truncated, kFixtureBase, expectedTarget).status == DiscoveryStatus::DecodeFailure &&
			!DiscoverCallSites(Hook::Weather, true, truncated, kFixtureBase, expectedTarget).selected,
			"truncation after a valid match still fails closed");
		// A complete D3D signature hidden in MOV r64, imm64 must not be scanned as CALL.
		std::vector<std::uint8_t> embedded{ 0x48, 0xB8, 0xE8, 0, 0, 0, 0, 0x44, 0x39, 0x6E, 0x2C, 0x90 };
		Expect(DiscoverCallSites(Hook::D3DInit, true, embedded, kFixtureBase, expectedTarget).candidates.empty(),
			"CALL opcode inside immediate is not a candidate");

		VirtualPage executable(PAGE_EXECUTE_READWRITE), writable(PAGE_READWRITE), relay(PAGE_EXECUTE_READWRITE);
		Expect(executable.get() && writable.get() && relay.get(), "discovery chain fixtures allocate");
		if (executable.get() && writable.get() && relay.get()) {
			*static_cast<std::uint8_t*>(executable.get()) = 0xC3;
			const auto currentModule = GetAllocationBase(reinterpret_cast<std::uintptr_t>(&SelfTarget));
			const TargetValidator inspect = [=](auto address, auto target) {
				const auto value = InspectExternalTarget(target, address, currentModule);
				return ClassifyTarget(ChainPolicy::ExecutableChainAllowed, { false, false,
					value.memoryStatus == ExecutableMemoryStatus::Executable, value.duplicateOrSelfTarget });
			};
			auto discoverTarget = [&](std::uintptr_t target) {
				const auto base = target - 0x1000;
				return DiscoverCallSites(Hook::Weather, true, CallFixture(Hook::Weather, base, target), base, inspect);
			};
			Expect(discoverTarget(executable.address()).selected.has_value(), "allowed executable external chain accepted by discovery");
			Expect(!discoverTarget(writable.address()).selected, "non-executable external target rejected by discovery");
			WriteAbsoluteRelay(relay.get(), reinterpret_cast<std::uintptr_t>(&SelfTarget));
			Expect(!discoverTarget(relay.address()).selected, "relay to current module rejected by discovery");
			WriteAbsoluteRelay(relay.get(), relay.address());
			Expect(!discoverTarget(relay.address()).selected, "cyclic/duplicate relay rejected by discovery");
			WriteAbsoluteRelay(relay.get(), writable.address());
			Expect(!discoverTarget(relay.address()).selected, "relay to non-executable target rejected by discovery");
			WriteAbsoluteRelay(relay.get(), executable.address());
			Expect(discoverTarget(relay.address()).selected.has_value(), "valid executable relay remains accepted by discovery");
		}
	}
}

int main()
{
	TestDiscovery();
	constexpr WeatherCallContext seWeatherContext{ 0x48, 0x8B, 0xCE };
	constexpr WeatherCallContext aeWeatherContext{ 0x48, 0x8B, 0xCF };
	constexpr WeatherCallContext wrongWeatherContext{ 0x48, 0x8B, 0xCD };
	Expect(MatchesWeatherCallContext(false, seWeatherContext, seWeatherContext),
		"Weather A: verified Skyrim 1.5.97 context is accepted");
	Expect(MatchesWeatherCallContext(true, aeWeatherContext, aeWeatherContext),
		"Weather B: existing AE context remains accepted");
	Expect(!MatchesWeatherCallContext(false, wrongWeatherContext, seWeatherContext),
		"Weather C: wrong Skyrim 1.5.97 context is rejected");
	Expect(
		ClassifyCallSite(true, 0x90, MatchesWeatherCallContext(false, seWeatherContext, seWeatherContext)) ==
			CallSiteResult::WrongOpcode,
		"Weather D: wrong opcode is still rejected");

	Expect(
		ClassifyTarget(ChainPolicy::ExecutableChainAllowed, { true, true, false, false }) ==
			TargetResult::VanillaTargetAccepted,
		"A: expected Skyrim target is accepted");

	VirtualPage executablePage(PAGE_EXECUTE_READWRITE);
	Expect(executablePage.get() != nullptr, "B: executable page allocation");
	if (executablePage.get()) {
		*static_cast<std::uint8_t*>(executablePage.get()) = 0xC3;
		const auto inspection = InspectExternalTarget(executablePage.address(), 0x1000, GetAllocationBase(reinterpret_cast<std::uintptr_t>(&SelfTarget)));
		Expect(inspection.memoryStatus == ExecutableMemoryStatus::Executable && !inspection.duplicateOrSelfTarget,
			"B: committed executable external target inspection");
		Expect(
			ClassifyTarget(ChainPolicy::ExecutableChainAllowed, { false, false, true, false }) ==
				TargetResult::ExternalChainAccepted,
			"B: chainable hook accepts executable external target");
	}

	VirtualPage externalTargets(PAGE_EXECUTE_READWRITE);
	VirtualPage adjacentRelays(PAGE_EXECUTE_READWRITE);
	Expect(externalTargets.get() != nullptr && adjacentRelays.get() != nullptr,
		"B: adjacent relay allocation");
	if (externalTargets.get() && adjacentRelays.get()) {
		auto* targets = static_cast<std::uint8_t*>(externalTargets.get());
		auto* relays = static_cast<std::uint8_t*>(adjacentRelays.get());
		targets[0] = 0xC3;
		targets[1] = 0xC3;
		WriteAbsoluteRelay(relays, reinterpret_cast<std::uintptr_t>(targets));
		WriteAbsoluteRelay(relays + 0xE, reinterpret_cast<std::uintptr_t>(targets + 1));

		const auto firstRelay = InspectExternalTarget(
			reinterpret_cast<std::uintptr_t>(relays),
			0x1000,
			GetAllocationBase(reinterpret_cast<std::uintptr_t>(&SelfTarget)));
		const auto secondRelay = InspectExternalTarget(
			reinterpret_cast<std::uintptr_t>(relays + 0xE),
			0x2000,
			GetAllocationBase(reinterpret_cast<std::uintptr_t>(&SelfTarget)));
		Expect(firstRelay.memoryStatus == ExecutableMemoryStatus::Executable && !firstRelay.duplicateOrSelfTarget,
			"B: first CommonLib-style relay is accepted");
		Expect(secondRelay.memoryStatus == ExecutableMemoryStatus::Executable && !secondRelay.duplicateOrSelfTarget,
			"B: second CommonLib-style relay at +0xE is accepted");
	}

	VirtualPage writablePage(PAGE_READWRITE);
	Expect(writablePage.get() != nullptr, "C: writable page allocation");
	if (writablePage.get()) {
		const auto inspection = InspectExternalTarget(writablePage.address(), 0x1000, 0);
		Expect(inspection.memoryStatus == ExecutableMemoryStatus::NotExecutable,
			"C: external non-executable target inspection");
		Expect(
			ClassifyTarget(ChainPolicy::ExecutableChainAllowed, { false, false, false, false }) ==
				TargetResult::ExternalTargetNotExecutable,
			"C: external non-executable target is rejected");
	}

	VirtualPage guardedPage(PAGE_EXECUTE_READWRITE | PAGE_GUARD);
	Expect(guardedPage.get() != nullptr, "C: guarded page allocation");
	if (guardedPage.get()) {
		const auto inspection = InspectExternalTarget(guardedPage.address(), 0x1000, 0);
		Expect(inspection.memoryStatus == ExecutableMemoryStatus::Guarded,
			"C: guarded executable target is rejected");
	}

	VirtualPage invalidRelay(PAGE_EXECUTE_READWRITE);
	Expect(invalidRelay.get() != nullptr, "C: invalid relay allocation");
	if (invalidRelay.get() && writablePage.get()) {
		WriteAbsoluteRelay(invalidRelay.get(), writablePage.address());
		const auto inspection = InspectExternalTarget(invalidRelay.address(), 0x1000, 0);
		Expect(inspection.memoryStatus == ExecutableMemoryStatus::InvalidRelayDestination,
			"C: relay to non-executable target is rejected");
	}

	Expect(ClassifyCallSite(true, 0x90, true) == CallSiteResult::WrongOpcode,
		"D: wrong opcode is rejected");
	Expect(ClassifyCallSite(true, 0xE8, false) == CallSiteResult::ContextMismatch,
		"E: wrong instruction context is rejected");
	Expect(
		ClassifyTarget(ChainPolicy::ExecutableChainAllowed, { true, false, false, false }) ==
			TargetResult::UnexpectedSkyrimTarget,
		"F: wrong Skyrim target is rejected");
	Expect(
		ClassifyTarget(ChainPolicy::VanillaTargetRequired, { false, false, true, false }) ==
			TargetResult::ExternalChainNotAllowed,
		"G: non-chainable policy rejects executable external target");

	const auto currentModuleBase = GetAllocationBase(reinterpret_cast<std::uintptr_t>(&SelfTarget));
	const auto directSelf = InspectExternalTarget(reinterpret_cast<std::uintptr_t>(&SelfTarget), 0x1000, currentModuleBase);
	Expect(directSelf.duplicateOrSelfTarget, "H: direct self target is rejected");

	VirtualPage relayPage(PAGE_EXECUTE_READWRITE);
	Expect(relayPage.get() != nullptr, "H: relay page allocation");
	if (relayPage.get()) {
		const auto selfAddress = reinterpret_cast<std::uintptr_t>(&SelfTarget);
		WriteAbsoluteRelay(relayPage.get(), selfAddress);
		const auto relaySelf = InspectExternalTarget(relayPage.address(), 0x1000, currentModuleBase);
		Expect(relaySelf.duplicateOrSelfTarget, "H: relay to self target is rejected");
	}

	Expect(ClassifyCallSite(false, 0xE8, true) == CallSiteResult::OutsideExecutableText,
		"callsite outside executable text is rejected");
	Expect(ClassifyCallSite(true, 0xE8, true) == CallSiteResult::Valid,
		"valid callsite is accepted");

	if (g_failures == 0) {
		std::cout << "All runtime compatibility regression tests passed.\n";
	}
	return g_failures == 0 ? 0 : 1;
}
