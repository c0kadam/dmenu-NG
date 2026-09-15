#include "HookValidation.h"

#include <Windows.h>

#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>

namespace RuntimeCompatibility::Validation
{
	namespace
	{
		constexpr std::size_t kMaxRelayDepth = 8;

		struct RelayDecodeResult
		{
			bool recognized;
			std::optional<std::uintptr_t> destination;
		};

		[[nodiscard]] ExecutableMemoryStatus GetExecutableMemoryStatus(
			std::uintptr_t a_address,
			MEMORY_BASIC_INFORMATION* a_memoryInfo = nullptr) noexcept
		{
			MEMORY_BASIC_INFORMATION memoryInfo{};
			if (a_address == 0 ||
			    ::VirtualQuery(reinterpret_cast<const void*>(a_address), std::addressof(memoryInfo), sizeof(memoryInfo)) == 0) {
				return ExecutableMemoryStatus::QueryFailed;
			}
			if (a_memoryInfo) {
				*a_memoryInfo = memoryInfo;
			}
			if (memoryInfo.State != MEM_COMMIT) {
				return ExecutableMemoryStatus::NotCommitted;
			}
			if ((memoryInfo.Protect & PAGE_GUARD) != 0) {
				return ExecutableMemoryStatus::Guarded;
			}
			if ((memoryInfo.Protect & PAGE_NOACCESS) != 0) {
				return ExecutableMemoryStatus::NoAccess;
			}

			switch (memoryInfo.Protect & 0xFF) {
			case PAGE_EXECUTE:
			case PAGE_EXECUTE_READ:
			case PAGE_EXECUTE_READWRITE:
			case PAGE_EXECUTE_WRITECOPY:
				return ExecutableMemoryStatus::Executable;
			default:
				return ExecutableMemoryStatus::NotExecutable;
			}
		}

		[[nodiscard]] bool IsReadableProtection(DWORD a_protection) noexcept
		{
			switch (a_protection & 0xFF) {
			case PAGE_READONLY:
			case PAGE_READWRITE:
			case PAGE_WRITECOPY:
			case PAGE_EXECUTE_READ:
			case PAGE_EXECUTE_READWRITE:
			case PAGE_EXECUTE_WRITECOPY:
				return true;
			default:
				return false;
			}
		}

		[[nodiscard]] bool IsReadableRange(std::uintptr_t a_address, std::size_t a_size) noexcept
		{
			MEMORY_BASIC_INFORMATION memoryInfo{};
			if (a_address == 0 || a_size == 0 ||
			    ::VirtualQuery(reinterpret_cast<const void*>(a_address), std::addressof(memoryInfo), sizeof(memoryInfo)) == 0 ||
			    memoryInfo.State != MEM_COMMIT ||
			    (memoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
			    !IsReadableProtection(memoryInfo.Protect)) {
				return false;
			}

			const auto regionStart = reinterpret_cast<std::uintptr_t>(memoryInfo.BaseAddress);
			if (memoryInfo.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - regionStart) {
				return false;
			}
			const auto regionEnd = regionStart + memoryInfo.RegionSize;
			return a_address >= regionStart && a_address <= regionEnd && a_size <= regionEnd - a_address;
		}

		template <class T>
		[[nodiscard]] std::optional<T> ReadValue(std::uintptr_t a_address) noexcept
		{
			if (!IsReadableRange(a_address, sizeof(T))) {
				return std::nullopt;
			}
			T value{};
			std::memcpy(std::addressof(value), reinterpret_cast<const void*>(a_address), sizeof(value));
			return value;
		}

		[[nodiscard]] std::optional<std::uintptr_t> AddDisplacement(
			std::uintptr_t a_base,
			std::int64_t a_displacement) noexcept
		{
			if (a_displacement >= 0) {
				const auto displacement = static_cast<std::uintptr_t>(a_displacement);
				if (displacement > (std::numeric_limits<std::uintptr_t>::max)() - a_base) {
					return std::nullopt;
				}
				return a_base + displacement;
			}

			const auto magnitude = static_cast<std::uint64_t>(-(a_displacement + 1)) + 1;
			if (magnitude > a_base) {
				return std::nullopt;
			}
			return a_base - static_cast<std::uintptr_t>(magnitude);
		}

		[[nodiscard]] RelayDecodeResult DecodeUnconditionalRelay(std::uintptr_t a_address) noexcept
		{
			const auto first = ReadValue<std::uint8_t>(a_address);
			if (!first) {
				return { false, std::nullopt };
			}

			if (*first == 0xE9) {
				const auto displacement = ReadValue<std::int32_t>(a_address + 1);
				return { true, displacement ? AddDisplacement(a_address + 5, *displacement) : std::nullopt };
			}
			if (*first == 0xEB) {
				const auto displacement = ReadValue<std::int8_t>(a_address + 1);
				return { true, displacement ? AddDisplacement(a_address + 2, *displacement) : std::nullopt };
			}
			if (*first == 0xFF) {
				const auto second = ReadValue<std::uint8_t>(a_address + 1);
				if (!second || *second != 0x25) {
					return { false, std::nullopt };
				}
				const auto displacement = ReadValue<std::int32_t>(a_address + 2);
				if (!displacement) {
					return { true, std::nullopt };
				}
				const auto pointerAddress = AddDisplacement(a_address + 6, *displacement);
				return { true, pointerAddress ? ReadValue<std::uintptr_t>(*pointerAddress) : std::nullopt };
			}
			if (*first == 0x48 && IsReadableRange(a_address, 12)) {
				std::array<std::uint8_t, 12> bytes{};
				std::memcpy(bytes.data(), reinterpret_cast<const void*>(a_address), bytes.size());
				if (bytes[1] == 0xB8 && bytes[10] == 0xFF && bytes[11] == 0xE0) {
					std::uintptr_t destination = 0;
					std::memcpy(std::addressof(destination), bytes.data() + 2, sizeof(destination));
					return { true, destination };
				}
			}
			return { false, std::nullopt };
		}

		[[nodiscard]] bool IsCallSiteTarget(std::uintptr_t a_target, std::uintptr_t a_callSite) noexcept
		{
			return a_target >= a_callSite && a_target - a_callSite < 5;
		}

		[[nodiscard]] bool IsInCurrentModule(std::uintptr_t a_address, std::uintptr_t a_currentModuleBase) noexcept
		{
			MEMORY_BASIC_INFORMATION memoryInfo{};
			return a_currentModuleBase != 0 &&
			       ::VirtualQuery(reinterpret_cast<const void*>(a_address), std::addressof(memoryInfo), sizeof(memoryInfo)) != 0 &&
			       reinterpret_cast<std::uintptr_t>(memoryInfo.AllocationBase) == a_currentModuleBase;
		}
	}

	ExternalTargetInspection InspectExternalTarget(
		std::uintptr_t a_target,
		std::uintptr_t a_callSite,
		std::uintptr_t a_currentModuleBase) noexcept
	{
		const auto initialStatus = GetExecutableMemoryStatus(a_target);
		if (initialStatus != ExecutableMemoryStatus::Executable) {
			return { initialStatus, false };
		}

		std::array<std::uintptr_t, kMaxRelayDepth> visited{};
		auto current = a_target;
		for (std::size_t depth = 0; depth < visited.size(); ++depth) {
			if (IsCallSiteTarget(current, a_callSite) || IsInCurrentModule(current, a_currentModuleBase)) {
				return { ExecutableMemoryStatus::Executable, true };
			}
			for (std::size_t index = 0; index < depth; ++index) {
				if (visited[index] == current) {
					return { ExecutableMemoryStatus::Executable, true };
				}
			}
			visited[depth] = current;

			const auto relay = DecodeUnconditionalRelay(current);
			if (!relay.recognized) {
				break;
			}
			if (!relay.destination) {
				return { ExecutableMemoryStatus::InvalidRelayDestination, false };
			}
			const auto destinationStatus = GetExecutableMemoryStatus(*relay.destination);
			if (destinationStatus != ExecutableMemoryStatus::Executable) {
				return { ExecutableMemoryStatus::InvalidRelayDestination, false };
			}
			current = *relay.destination;
		}

		return { ExecutableMemoryStatus::Executable, false };
	}

	const char* GetExecutableMemoryStatusName(ExecutableMemoryStatus a_status) noexcept
	{
		switch (a_status) {
		case ExecutableMemoryStatus::Executable:
			return "executable";
		case ExecutableMemoryStatus::QueryFailed:
			return "VirtualQuery failed";
		case ExecutableMemoryStatus::NotCommitted:
			return "memory is not committed";
		case ExecutableMemoryStatus::Guarded:
			return "memory is guarded";
		case ExecutableMemoryStatus::NoAccess:
			return "memory is inaccessible";
		case ExecutableMemoryStatus::NotExecutable:
			return "memory is not executable";
		case ExecutableMemoryStatus::InvalidRelayDestination:
			return "recognized relay destination is not executable";
		default:
			return "unknown memory state";
		}
	}
}
