#include "RuntimeCompatibility.h"
#include "HookValidation.h"

#include <array>

extern "C" IMAGE_DOS_HEADER __ImageBase;

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
			// Each current thunk invokes the previous same-signature CALL target.
			Validation::ChainPolicy chainPolicy;
		};

		constexpr HookDefinition GetHookDefinition(Hook a_hook) noexcept
		{
			switch (a_hook) {
			case Hook::D3DInit:
				return { 75595, 77226, 77396, Validation::ChainPolicy::ExecutableChainAllowed };
			case Hook::DXGIPresent:
				return { 75461, 77246, 109135, Validation::ChainPolicy::ExecutableChainAllowed };
			case Hook::Weather:
				return { 25682, 26229, 26231, Validation::ChainPolicy::ExecutableChainAllowed };
			case Hook::InputEventDispatch:
				return { 67315, 68617, 68655, Validation::ChainPolicy::ExecutableChainAllowed };
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
			const bool callSiteInText = IsInTextSegment(address, 5);
			std::uint8_t opcode = 0;
			if (callSiteInText) {
				std::memcpy(std::addressof(opcode), reinterpret_cast<const void*>(address), sizeof(opcode));
			}
			const bool contextMatches = callSiteInText && opcode == 0xE8 &&
			                            MatchesCallContext(a_hook, address, REL::Module::IsAE());
			switch (Validation::ClassifyCallSite(callSiteInText, opcode, contextMatches)) {
			case Validation::CallSiteResult::OutsideExecutableText:
				logger::error("{}: resolved address 0x{:X} is outside Skyrim's executable section for runtime {}"sv,
					hookName,
					address,
					a_version.string("."));
				return std::nullopt;
			case Validation::CallSiteResult::WrongOpcode:
				logger::error("{}: expected CALL instruction not found at 0x{:X} for runtime {} (found 0x{:02X})"sv,
					hookName,
					address,
					a_version.string("."),
					opcode);
				return std::nullopt;
			case Validation::CallSiteResult::ContextMismatch:
				logger::error("{}: expected CALL context not found at 0x{:X} for runtime {}"sv,
					hookName,
					address,
					a_version.string("."));
				return std::nullopt;
			case Validation::CallSiteResult::Valid:
				break;
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
			std::uintptr_t expectedTarget = 0;
			bool expectedSkyrimTarget = true;
			if (targetIsInSkyrim && REL::Module::IsAE()) {
				expectedTarget = REL::ID(definition.expectedAETargetID).address();
				expectedSkyrimTarget = expectedTarget != 0 && originalTarget == expectedTarget;
			}

			Validation::ExternalTargetInspection externalInspection{
				Validation::ExecutableMemoryStatus::QueryFailed,
				false
			};
			if (!targetIsInSkyrim) {
				externalInspection = Validation::InspectExternalTarget(
					originalTarget,
					address,
					reinterpret_cast<std::uintptr_t>(&__ImageBase));
			}
			const bool callSiteSelfTarget = originalTarget >= address && originalTarget - address < 5;
			const Validation::TargetFacts targetFacts{
				targetIsInSkyrim,
				expectedSkyrimTarget,
				externalInspection.memoryStatus == Validation::ExecutableMemoryStatus::Executable,
				callSiteSelfTarget || externalInspection.duplicateOrSelfTarget
			};
			const auto targetResult = Validation::ClassifyTarget(definition.chainPolicy, targetFacts);
			switch (targetResult) {
			case Validation::TargetResult::UnexpectedSkyrimTarget:
				logger::error("{}: CALL target mismatch at 0x{:X} for runtime {} (resolved 0x{:X}, expected Address Library ID {} at 0x{:X})"sv,
					hookName,
					address,
					a_version.string("."),
					originalTarget,
					definition.expectedAETargetID,
					expectedTarget);
				return std::nullopt;
			case Validation::TargetResult::ExternalTargetNotExecutable:
				logger::error("{}: external CALL target 0x{:X} rejected for runtime {} ({})"sv,
					hookName,
					originalTarget,
					a_version.string("."),
					Validation::GetExecutableMemoryStatusName(externalInspection.memoryStatus));
				return std::nullopt;
			case Validation::TargetResult::ExternalChainNotAllowed:
				logger::error("{}: external CALL target 0x{:X} is executable, but this hook does not allow chaining for runtime {}"sv,
					hookName,
					originalTarget,
					a_version.string("."));
				return std::nullopt;
			case Validation::TargetResult::DuplicateOrSelfTarget:
				logger::error("{}: CALL target 0x{:X} would create a duplicate or self-referential hook chain for runtime {}"sv,
					hookName,
					originalTarget,
					a_version.string("."));
				return std::nullopt;
			case Validation::TargetResult::VanillaTargetAccepted:
			case Validation::TargetResult::ExternalChainAccepted:
				break;
			}

			if (targetResult == Validation::TargetResult::ExternalChainAccepted) {
				logger::warn("{}: accepted pre-existing executable hook chain for runtime {}: relocation ID {}, offset 0x{:X}, call 0x{:X} -> 0x{:X}"sv,
					hookName,
					a_version.string("."),
					relocation.id(),
					offset,
					address,
					originalTarget);
			} else {
				logger::info("{}: validated Skyrim CALL target for runtime {}: relocation ID {}, offset 0x{:X}, call 0x{:X} -> 0x{:X}"sv,
					hookName,
					a_version.string("."),
					relocation.id(),
					offset,
					address,
					originalTarget);
			}
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
