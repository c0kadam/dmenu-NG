#include "HookValidation.h"

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
}

int main()
{
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
