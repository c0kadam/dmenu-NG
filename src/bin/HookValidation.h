// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (c) 2026 C0kadam
// See LICENSE and EXCEPTIONS.md for the project terms.

#pragma once

#include <array>
#include <cstdint>

namespace RuntimeCompatibility::Validation
{
	enum class ChainPolicy : std::uint8_t
	{
		VanillaTargetRequired,
		ExecutableChainAllowed
	};

	enum class CallSiteResult : std::uint8_t
	{
		Valid,
		OutsideExecutableText,
		WrongOpcode,
		ContextMismatch
	};

	enum class TargetResult : std::uint8_t
	{
		VanillaTargetAccepted,
		ExternalChainAccepted,
		UnexpectedSkyrimTarget,
		ExternalTargetNotExecutable,
		ExternalChainNotAllowed,
		DuplicateOrSelfTarget
	};

	enum class ExecutableMemoryStatus : std::uint8_t
	{
		Executable,
		QueryFailed,
		NotCommitted,
		Guarded,
		NoAccess,
		NotExecutable,
		InvalidRelayDestination
	};

	struct TargetFacts
	{
		bool targetInSkyrim;
		bool expectedSkyrimTarget;
		bool externalTargetExecutable;
		bool duplicateOrSelfTarget;
	};

	struct ExternalTargetInspection
	{
		ExecutableMemoryStatus memoryStatus;
		bool duplicateOrSelfTarget;
	};

	using WeatherCallContext = std::array<std::uint8_t, 3>;

	[[nodiscard]] constexpr bool MatchesWeatherCallContext(
		bool a_isAE,
		const WeatherCallContext& a_before,
		const WeatherCallContext& a_after) noexcept
	{
		const WeatherCallContext expected{ 0x48, 0x8B, static_cast<std::uint8_t>(a_isAE ? 0xCF : 0xCE) };
		return a_before == expected && a_after == expected;
	}

	[[nodiscard]] constexpr CallSiteResult ClassifyCallSite(
		bool a_inExecutableText,
		std::uint8_t a_opcode,
		bool a_contextMatches) noexcept
	{
		if (!a_inExecutableText) {
			return CallSiteResult::OutsideExecutableText;
		}
		if (a_opcode != 0xE8) {
			return CallSiteResult::WrongOpcode;
		}
		return a_contextMatches ? CallSiteResult::Valid : CallSiteResult::ContextMismatch;
	}

	[[nodiscard]] constexpr TargetResult ClassifyTarget(
		ChainPolicy a_chainPolicy,
		const TargetFacts& a_facts) noexcept
	{
		if (a_facts.duplicateOrSelfTarget) {
			return TargetResult::DuplicateOrSelfTarget;
		}
		if (a_facts.targetInSkyrim) {
			return a_facts.expectedSkyrimTarget ?
			           TargetResult::VanillaTargetAccepted :
			           TargetResult::UnexpectedSkyrimTarget;
		}
		if (!a_facts.externalTargetExecutable) {
			return TargetResult::ExternalTargetNotExecutable;
		}
		return a_chainPolicy == ChainPolicy::ExecutableChainAllowed ?
		           TargetResult::ExternalChainAccepted :
		           TargetResult::ExternalChainNotAllowed;
	}

	[[nodiscard]] ExternalTargetInspection InspectExternalTarget(
		std::uintptr_t a_target,
		std::uintptr_t a_callSite,
		std::uintptr_t a_currentModuleBase) noexcept;

	[[nodiscard]] const char* GetExecutableMemoryStatusName(ExecutableMemoryStatus a_status) noexcept;
}
