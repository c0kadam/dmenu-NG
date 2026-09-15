#include "RuntimeCompatibility.h"

#include <array>

namespace RuntimeCompatibility
{
	namespace
	{
		struct RuntimeOffsets
		{
			REL::Version version;
			std::ptrdiff_t d3dInit;
			std::ptrdiff_t dxgiPresent;
			std::ptrdiff_t weather;
			std::ptrdiff_t inputEventDispatch;
		};

		// Verified against the matching Address Library database and executable for
		// 1.6.1170, 1.7.99, and 1.7.104. The 1.5.97 values are the established
		// dMenu call sites and are still guarded by instruction/context validation.
		constexpr std::array RUNTIME_OFFSETS{
			RuntimeOffsets{ SKYRIM_1_5_97, 0x9, 0x9, 0x29C, 0x7B },
			RuntimeOffsets{ SKYRIM_1_6_1170, 0x275, 0x9, 0x3E6, 0x7B },
			RuntimeOffsets{ SKYRIM_1_7_99, 0x275, 0x9, 0x3E6, 0x7B },
			RuntimeOffsets{ SKYRIM_1_7_104, 0x275, 0x9, 0x3E6, 0x7B }
		};

		struct HookDefinition
		{
			std::uint64_t seID;
			std::uint64_t aeID;
			std::uint64_t expectedAETargetID;
		};

		constexpr HookDefinition GetHookDefinition(Hook a_hook) noexcept
		{
			switch (a_hook) {
			case Hook::D3DInit:
				return { 75595, 77226, 77396 };
			case Hook::DXGIPresent:
				return { 75461, 77246, 109135 };
			case Hook::Weather:
				return { 25682, 26229, 26231 };
			case Hook::InputEventDispatch:
				return { 67315, 68617, 68655 };
			default:
				return {};
			}
		}

		std::array<std::optional<ResolvedCallSite>, static_cast<std::size_t>(Hook::Count)> g_resolvedSites;

		[[nodiscard]] const RuntimeOffsets* FindRuntimeOffsets(REL::Version a_version) noexcept
		{
			for (const auto& entry : RUNTIME_OFFSETS) {
				if (entry.version == a_version) {
					return std::addressof(entry);
				}
			}
			return nullptr;
		}

		[[nodiscard]] std::ptrdiff_t GetOffset(const RuntimeOffsets& a_offsets, Hook a_hook) noexcept
		{
			switch (a_hook) {
			case Hook::D3DInit:
				return a_offsets.d3dInit;
			case Hook::DXGIPresent:
				return a_offsets.dxgiPresent;
			case Hook::Weather:
				return a_offsets.weather;
			case Hook::InputEventDispatch:
				return a_offsets.inputEventDispatch;
			default:
				return 0;
			}
		}

		[[nodiscard]] bool IsInTextSegment(std::uintptr_t a_address, std::size_t a_size) noexcept
		{
			const auto text = REL::Module::get().segment(REL::Segment::textx);
			if (text.address() == 0 || text.size() == 0 || a_address < text.address()) {
				return false;
			}

			const auto offset = a_address - text.address();
			return offset <= text.size() && a_size <= text.size() - offset;
		}

		[[nodiscard]] bool IsExecutableMemory(std::uintptr_t a_address) noexcept
		{
			MEMORY_BASIC_INFORMATION memoryInfo{};
			if (::VirtualQuery(reinterpret_cast<const void*>(a_address), std::addressof(memoryInfo), sizeof(memoryInfo)) == 0 ||
			    memoryInfo.State != MEM_COMMIT || (memoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
				return false;
			}

			switch (memoryInfo.Protect & 0xFF) {
			case PAGE_EXECUTE:
			case PAGE_EXECUTE_READ:
			case PAGE_EXECUTE_READWRITE:
			case PAGE_EXECUTE_WRITECOPY:
				return true;
			default:
				return false;
			}
		}

		template <std::size_t N>
		[[nodiscard]] bool MatchesBytes(std::uintptr_t a_address, const std::array<std::uint8_t, N>& a_expected) noexcept
		{
			return IsInTextSegment(a_address, N) &&
			       std::memcmp(reinterpret_cast<const void*>(a_address), a_expected.data(), N) == 0;
		}

		[[nodiscard]] bool MatchesCallContext(Hook a_hook, std::uintptr_t a_address, bool a_isAE) noexcept
		{
			switch (a_hook) {
			case Hook::D3DInit:
				// The older SE call is near the function entry and has a different context.
				return !a_isAE || MatchesBytes(a_address + 5, std::array<std::uint8_t, 4>{ 0x44, 0x39, 0x6E, 0x2C });
			case Hook::DXGIPresent:
				return MatchesBytes(a_address - 5, std::array<std::uint8_t, 5>{ 0xB9, 0x01, 0x00, 0x00, 0x00 }) &&
			       MatchesBytes(a_address + 5, std::array<std::uint8_t, 2>{ 0x80, 0x3D });
			case Hook::Weather:
				return MatchesBytes(a_address - 3, std::array<std::uint8_t, 3>{ 0x48, 0x8B, 0xCF }) &&
			       MatchesBytes(a_address + 5, std::array<std::uint8_t, 3>{ 0x48, 0x8B, 0xCF });
			case Hook::InputEventDispatch:
				return MatchesBytes(a_address - 3, std::array<std::uint8_t, 3>{ 0x48, 0x8B, 0xCE }) &&
			       MatchesBytes(a_address + 5, std::array<std::uint8_t, 3>{ 0x48, 0x8B, 0x0D });
			default:
				return false;
			}
		}

		[[nodiscard]] std::optional<ResolvedCallSite> ResolveCallSite(Hook a_hook, REL::Version a_version)
		{
			const auto hookName = GetHookName(a_hook);
			const auto* offsets = FindRuntimeOffsets(a_version);
			if (!offsets) {
				logger::error("{}: runtime {} has no verified hook offsets"sv, hookName, a_version.string("."));
				return std::nullopt;
			}

			const auto definition = GetHookDefinition(a_hook);
			const REL::RelocationID relocation{ definition.seID, definition.aeID };
			const auto baseAddress = relocation.address();
			if (baseAddress == 0) {
				logger::error("{}: relocation ID {} resolved to zero for runtime {}"sv,
					hookName,
					relocation.id(),
					a_version.string("."));
				return std::nullopt;
			}

			const auto offset = GetOffset(*offsets, a_hook);
			if (offset < 0 || static_cast<std::uintptr_t>(offset) >
			                    (std::numeric_limits<std::uintptr_t>::max)() - baseAddress) {
				logger::error("{}: invalid hook offset 0x{:X} for runtime {}"sv,
					hookName,
					offset,
					a_version.string("."));
				return std::nullopt;
			}

			const auto address = baseAddress + static_cast<std::uintptr_t>(offset);
			if (!IsInTextSegment(address, 5)) {
				logger::error("{}: resolved address 0x{:X} is outside Skyrim's executable section for runtime {}"sv,
					hookName,
					address,
					a_version.string("."));
				return std::nullopt;
			}

			std::uint8_t opcode = 0;
			std::memcpy(std::addressof(opcode), reinterpret_cast<const void*>(address), sizeof(opcode));
			if (opcode != 0xE8) {
				logger::error("{}: expected CALL instruction not found at 0x{:X} for runtime {} (found 0x{:02X})"sv,
					hookName,
					address,
					a_version.string("."),
					opcode);
				return std::nullopt;
			}

			if (!MatchesCallContext(a_hook, address, REL::Module::IsAE())) {
				logger::error("{}: expected CALL context not found at 0x{:X} for runtime {}"sv,
					hookName,
					address,
					a_version.string("."));
				return std::nullopt;
			}

			std::int32_t displacement = 0;
			std::memcpy(std::addressof(displacement), reinterpret_cast<const void*>(address + 1), sizeof(displacement));
			const auto signedTarget = static_cast<std::int64_t>(address + 5) + displacement;
			if (signedTarget <= 0) {
				logger::error("{}: CALL at 0x{:X} has an invalid target for runtime {}"sv,
					hookName,
					address,
					a_version.string("."));
				return std::nullopt;
			}

			const auto originalTarget = static_cast<std::uintptr_t>(signedTarget);
			const bool targetIsInSkyrim = IsInTextSegment(originalTarget, 1);
			if (!targetIsInSkyrim) {
				// Input dispatch is a shared hook point. If another SKSE plugin (notably
				// Wheeler) installed first, CommonLib's write_call will safely return its
				// executable relay so dMenu can preserve the existing call chain.
				if (a_hook != Hook::InputEventDispatch || !IsExecutableMemory(originalTarget)) {
					logger::error("{}: CALL target 0x{:X} is not a valid Skyrim or executable chained target for runtime {}"sv,
						hookName,
						originalTarget,
						a_version.string("."));
					return std::nullopt;
				}

				logger::warn("{}: preserving pre-existing executable hook chain at 0x{:X} for runtime {}"sv,
					hookName,
					originalTarget,
					a_version.string("."));
			}

			if (targetIsInSkyrim && REL::Module::IsAE()) {
				const auto expectedTarget = REL::ID(definition.expectedAETargetID).address();
				if (expectedTarget == 0 || originalTarget != expectedTarget) {
					logger::error("{}: CALL target mismatch at 0x{:X} for runtime {} (resolved 0x{:X}, expected Address Library ID {} at 0x{:X})"sv,
						hookName,
						address,
						a_version.string("."),
						originalTarget,
						definition.expectedAETargetID,
						expectedTarget);
					return std::nullopt;
				}
			}

			logger::info("{}: runtime {}, relocation ID {}, offset 0x{:X}, call 0x{:X} -> 0x{:X}"sv,
				hookName,
				a_version.string("."),
				relocation.id(),
				offset,
				address,
				originalTarget);
			return ResolvedCallSite{ address, originalTarget };
		}
	}

	bool IsSupported(REL::Version a_version) noexcept
	{
		return FindRuntimeOffsets(a_version) != nullptr;
	}

	std::string_view GetHookName(Hook a_hook) noexcept
	{
		switch (a_hook) {
		case Hook::D3DInit:
			return "D3DInitHook"sv;
		case Hook::DXGIPresent:
			return "DXGIPresentHook"sv;
		case Hook::Weather:
			return "WeatherHook"sv;
		case Hook::InputEventDispatch:
			return "InputEventDispatchHook"sv;
		default:
			return "UnknownHook"sv;
		}
	}

	bool PreflightHooks()
	{
		const auto version = REL::Module::get().version();
		if (!IsSupported(version)) {
			logger::error("Unsupported Skyrim runtime {}; no hooks were installed"sv, version.string("."));
			return false;
		}

		auto log = spdlog::default_logger();
		const auto previousLevel = log ? log->level() : spdlog::level::off;
		if (log) {
			log->set_level(spdlog::level::info);
		}

		bool valid = true;
		for (std::size_t index = 0; index < static_cast<std::size_t>(Hook::Count); ++index) {
			const auto hook = static_cast<Hook>(index);
			g_resolvedSites[index] = ResolveCallSite(hook, version);
			valid = valid && g_resolvedSites[index].has_value();
		}

		if (!valid) {
			logger::error("Runtime hook preflight failed for Skyrim {}; no hooks were installed"sv, version.string("."));
		} else {
			logger::info("Runtime hook preflight passed for Skyrim {}"sv, version.string("."));
		}

		if (log) {
			log->flush();
			log->set_level(previousLevel);
		}
		return valid;
	}

	const ResolvedCallSite* GetResolvedCallSite(Hook a_hook) noexcept
	{
		const auto index = static_cast<std::size_t>(a_hook);
		if (index >= g_resolvedSites.size() || !g_resolvedSites[index]) {
			return nullptr;
		}
		return std::addressof(*g_resolvedSites[index]);
	}
}
