#include "RuntimeCompatibility.h"
#include "CallSiteDiscovery.h"

#include <REL/Offset2ID.h>
#include <array>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace RuntimeCompatibility
{
	namespace
	{
		RuntimeVersion GetVersionParts(REL::Version a_version) noexcept
		{
			return { a_version[0], a_version[1], a_version[2], a_version[3] };
		}

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
			return RuntimeCompatibility::FindRuntimeOffsets(GetVersionParts(a_version));
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

		[[nodiscard]] bool MatchesCallContext(Hook a_hook, std::uintptr_t a_address, bool a_isAE) noexcept
		{
			if (a_hook == Hook::D3DInit && !a_isAE) {
				return true;  // Preserve the audited SE D3D context rule.
			}
			const std::size_t before = a_hook == Hook::D3DInit ? 0 : a_hook == Hook::DXGIPresent ? 5 : 3;
			const std::size_t after = a_hook == Hook::D3DInit ? 4 : a_hook == Hook::DXGIPresent ? 2 : 3;
			if (a_address < before || !IsInTextSegment(a_address - before, before + 5 + after)) {
				return false;
			}
			return Validation::MatchesHookContext(a_hook, a_isAE,
				{ reinterpret_cast<const std::uint8_t*>(a_address - before), before + 5 + after }, before);
		}

		struct TargetValidation
		{
			Validation::TargetResult result;
			std::uintptr_t expectedTarget;
			Validation::ExternalTargetInspection external;
		};

		TargetValidation ValidateTarget(Hook a_hook, std::uintptr_t address, std::uintptr_t target,
			std::uintptr_t expectedTarget = 0)
		{
			const auto definition = GetHookDefinition(a_hook);
			const bool inSkyrim = IsInTextSegment(target, 1);
			bool expected = true;
			if (inSkyrim && REL::Module::IsAE()) {
				if (expectedTarget == 0) {
					expectedTarget = REL::ID(definition.expectedAETargetID).address();
				}
				expected = expectedTarget != 0 && target == expectedTarget;
			}
			Validation::ExternalTargetInspection external{ Validation::ExecutableMemoryStatus::QueryFailed, false };
			if (!inSkyrim) {
				external = Validation::InspectExternalTarget(target, address, reinterpret_cast<std::uintptr_t>(&__ImageBase));
			}
			return { Validation::ClassifyTarget(definition.chainPolicy, {
				inSkyrim, expected, external.memoryStatus == Validation::ExecutableMemoryStatus::Executable,
				(target >= address && target - address < 5) || external.duplicateOrSelfTarget }), expectedTarget, external };
		}

		[[nodiscard]] std::optional<ResolvedCallSite> ValidateCallSite(Hook a_hook, REL::Version a_version,
			std::uintptr_t address, std::uintptr_t a_expectedTarget = 0)
		{
			const auto hookName = GetHookName(a_hook);
			const auto definition = GetHookDefinition(a_hook);
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
			const auto validation = ValidateTarget(a_hook, address, originalTarget, a_expectedTarget);
			const auto targetResult = validation.result;
			const auto expectedTarget = validation.expectedTarget;
			const auto externalInspection = validation.external;
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
				logger::warn("{}: accepted pre-existing executable hook chain for runtime {}: call 0x{:X} -> 0x{:X}"sv,
					hookName,
					a_version.string("."),
					address,
					originalTarget);
			} else {
				logger::info("{}: validated Skyrim CALL target for runtime {}: call 0x{:X} -> 0x{:X}"sv,
					hookName,
					a_version.string("."),
					address,
					originalTarget);
			}
			return ResolvedCallSite{ address, originalTarget, a_expectedTarget };
		}

		std::optional<ResolvedCallSite> ResolveKnownCallSite(Hook a_hook, REL::Version a_version, const RuntimeOffsets& a_offsets)
		{
			const auto definition = GetHookDefinition(a_hook);
			const REL::RelocationID relocation{ definition.seID, definition.aeID };
			const auto base = relocation.address();
			const auto offset = GetOffset(a_offsets, a_hook);
			logger::info("{}: audited path, relocation ID {}, base 0x{:X}, offset +0x{:X}", GetHookName(a_hook), relocation.id(), base, offset);
			if (base == 0 || offset < 0 || static_cast<std::uintptr_t>(offset) > (std::numeric_limits<std::uintptr_t>::max)() - base) {
				logger::error("{}: invalid audited relocation/offset", GetHookName(a_hook));
				return std::nullopt;
			}
			return ValidateCallSite(a_hook, a_version, base + static_cast<std::uintptr_t>(offset));
		}

		std::uintptr_t FindLibraryAddress(const REL::Offset2ID& a_library, std::uint64_t a_id)
		{
			// ID::address() terminates for missing IDs. Enumerate so discovery can
			// report an absent mapping and return false without patching anything.
			const auto found = std::find_if(a_library.begin(), a_library.end(),
				[a_id](const auto& entry) { return entry.id == a_id; });
			const auto base = REL::Module::get().base();
			if (found == a_library.end() || found->offset == 0 || found->offset > (std::numeric_limits<std::uintptr_t>::max)() - base) {
				return 0;
			}
			return base + found->offset;
		}

		std::optional<std::span<const std::uint8_t>> GetDiscoveryFunction(std::uintptr_t a_base)
		{
			DWORD64 imageBase{};
			const auto* function = ::RtlLookupFunctionEntry(a_base, &imageBase, nullptr);
			if (!function || imageBase != REL::Module::get().base() ||
			    imageBase + function->BeginAddress != a_base || function->EndAddress <= function->BeginAddress) {
				return std::nullopt;
			}
			const auto length = static_cast<std::size_t>(function->EndAddress - function->BeginAddress);
			if (length > Validation::kMaxDiscoveryFunctionBytes || !IsInTextSegment(a_base, length)) {
				return std::nullopt;
			}
			for (auto cursor = a_base; cursor < a_base + length;) {
				MEMORY_BASIC_INFORMATION info{};
				if (!::VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) ||
				    info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
				    (info.Protect != PAGE_EXECUTE_READ && info.Protect != PAGE_EXECUTE_READWRITE && info.Protect != PAGE_EXECUTE_WRITECOPY)) {
					return std::nullopt;
				}
				const auto region = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
				if (info.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - region || region + info.RegionSize <= cursor) {
					return std::nullopt;
				}
				cursor = region + info.RegionSize;
			}
			return std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(a_base), length };
		}

		std::optional<ResolvedCallSite> ResolveUnknownCallSite(Hook a_hook, const REL::Offset2ID& a_library)
		{
			const auto definition = GetHookDefinition(a_hook);
			const auto base = FindLibraryAddress(a_library, definition.aeID);
			const auto expectedTarget = FindLibraryAddress(a_library, definition.expectedAETargetID);
			const auto name = GetHookName(a_hook);
			logger::info("{}: discovery relocation ID {}, base 0x{:X}; expected target ID {}, address 0x{:X}",
				name, definition.aeID, base, definition.expectedAETargetID, expectedTarget);
			if (base == 0 || expectedTarget == 0 || !IsInTextSegment(expectedTarget, 1)) {
				logger::error("{}: absent or invalid Address Library mapping; search not started", name);
				return std::nullopt;
			}
			const auto function = GetDiscoveryFunction(base);
			if (!function) {
				logger::error("{}: no safe function range at relocation base (requires unwind entry start, executable .text, size <= 0x{:X}); search not started",
					name, Validation::kMaxDiscoveryFunctionBytes);
				return std::nullopt;
			}
			logger::info("{}: search range [0x{:X}, 0x{:X}), function size 0x{:X}", name, base, base + function->size(), function->size());
			const auto result = Validation::DiscoverCallSites(a_hook, REL::Module::IsAE(), *function, base,
				[=](auto address, auto target) {
					const auto validation = ValidateTarget(a_hook, address, target, expectedTarget);
					if (validation.result == Validation::TargetResult::ExternalTargetNotExecutable) {
						logger::info("{}: external target 0x{:X}: {}", name, target, Validation::GetExecutableMemoryStatusName(validation.external.memoryStatus));
					}
					return validation.result;
				});
			for (const auto& candidate : result.candidates) {
				logger::info("{}: candidate +0x{:X}, CALL 0x{:X}, target 0x{:X}, context={}, target={}", name,
					candidate.relativeOffset, candidate.address, candidate.target,
					candidate.contextResult == Validation::CallSiteResult::Valid ? "valid" : "mismatch",
					candidate.targetResult ? Validation::GetTargetResultName(*candidate.targetResult) : "not checked: context rejected");
			}
			logger::info("{}: CALL candidates={}, fully validated={}, result={}, decode stop=+0x{:X}", name,
				result.candidates.size(), result.validCount, Validation::GetDiscoveryStatusName(result.status), result.failureOffset);
			if (!result.selected) {
				return std::nullopt;
			}
			logger::info("{} discovered and validated at +0x{:X}", name, result.selected->relativeOffset);
			return ResolvedCallSite{ result.selected->address, result.selected->target, expectedTarget };
		}
	}

	bool IsSupported(REL::Version a_version) noexcept
	{
		return FindRuntimeOffsets(a_version) != nullptr;
	}

	bool CanAttemptRuntime(REL::Version a_version) noexcept
	{
		return SelectResolutionPath(GetVersionParts(a_version)) != ResolutionPath::Unsupported;
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
		g_resolvedSites = {};
		const auto version = REL::Module::get().version();
		const auto path = SelectResolutionPath(GetVersionParts(version));

		auto log = spdlog::default_logger();
		const auto previousLevel = log ? log->level() : spdlog::level::off;
		if (log) {
			log->set_level(spdlog::level::info);
			log->flush_on(spdlog::level::info);
		}

		logger::info("REL/CommonLib runtime {}: {} path", version.string("."),
			path == ResolutionPath::Audited ? "known/audited" : path == ResolutionPath::Discovery ? "unknown/discovery" : "unsupported");
		bool valid = path != ResolutionPath::Unsupported;
		std::optional<REL::Offset2ID> library;
		if (path == ResolutionPath::Discovery) {
			logger::info("Runtime {} is not explicitly verified. EXPERIMENTAL RUNTIME DISCOVERY", version.string("."));
			valid = REL::Module::IsAE();
			if (valid) {
				library.emplace();
			}
		}
		decltype(g_resolvedSites) pending{};
		if (valid) {
			for (std::size_t index = 0; index < pending.size(); ++index) {
				const auto hook = static_cast<Hook>(index);
				pending[index] = path == ResolutionPath::Audited ?
					ResolveKnownCallSite(hook, version, *FindRuntimeOffsets(version)) : ResolveUnknownCallSite(hook, *library);
				if (!pending[index]) {
					logger::error("{} callsite resolution failed for {}", path == ResolutionPath::Discovery ? "Automatic" : "Audited", GetHookName(hook));
					valid = false;
				}
				if (path == ResolutionPath::Discovery && pending[index]) {
					for (std::size_t previous = 0; previous < index; ++previous) {
						if (pending[previous] && (pending[previous]->address == pending[index]->address ||
							pending[previous]->originalTarget == pending[index]->originalTarget)) {
							logger::error("{}: duplicate callsite/target across required hooks", GetHookName(hook));
							valid = false;
						}
					}
				}
			}
		}

		if (!valid) {
			logger::error("Runtime hook preflight failed for Skyrim {}; no hooks were installed"sv, version.string("."));
		} else {
			g_resolvedSites = pending;
			logger::info("{} runtime preflight passed for Skyrim {}"sv,
				path == ResolutionPath::Discovery ? "Experimental" : "Audited", version.string("."));
		}

		if (log) {
			log->flush();
			log->flush_on(spdlog::level::err);
			log->set_level(previousLevel);
		}
		return valid;
	}

	bool RevalidateCallSite(Hook a_hook)
	{
		const auto* preflighted = GetResolvedCallSite(a_hook);
		if (!preflighted) {
			return false;
		}
		// Revalidate the pinned site, never discover a new location after other
		// hooks have already been installed. The downstream chain may have changed.
		const auto version = REL::Module::get().version();
		const auto* offsets = FindRuntimeOffsets(version);
		const auto current = offsets ? ResolveKnownCallSite(a_hook, version, *offsets) :
			ValidateCallSite(a_hook, version, preflighted->address, preflighted->expectedTarget);
		if (!current) {
			return false;
		}
		if (current->address != preflighted->address) {
			logger::error("{}: callsite changed since preflight; deferred installation rejected"sv, GetHookName(a_hook));
			return false;
		}
		return true;
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
