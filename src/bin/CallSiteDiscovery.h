// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "HookValidation.h"
#include "RuntimeLayout.h"

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace RuntimeCompatibility::Validation
{
	// At most 4 KiB of one unwind-described function, never a truncated prefix.
	// This bounds work without letting a search spill into adjacent functions.
	inline constexpr std::size_t kMaxDiscoveryFunctionBytes = 0x1000;

	struct DiscoveryCandidate
	{
		std::uintptr_t address{};
		std::uintptr_t target{};
		std::size_t relativeOffset{};
		CallSiteResult contextResult{ CallSiteResult::ContextMismatch };
		std::optional<TargetResult> targetResult;
	};

	enum class DiscoveryStatus { Unique, NoMatch, Ambiguous, InvalidRange, DecodeFailure };
	struct DiscoveryResult
	{
		DiscoveryStatus status{ DiscoveryStatus::InvalidRange };
		std::vector<DiscoveryCandidate> candidates;
		std::size_t validCount{};
		std::size_t failureOffset{};
		std::optional<DiscoveryCandidate> selected;
	};

	[[nodiscard]] bool MatchesHookContext(Hook a_hook, bool a_isAE,
		std::span<const std::uint8_t> a_bytes, std::size_t a_callOffset) noexcept;

	using TargetValidator = std::function<TargetResult(std::uintptr_t, std::uintptr_t)>;
	[[nodiscard]] DiscoveryResult DiscoverCallSites(Hook a_hook, bool a_isAE,
		std::span<const std::uint8_t> a_function, std::uintptr_t a_base,
		const TargetValidator& a_validateTarget);

	[[nodiscard]] const char* GetTargetResultName(TargetResult a_result) noexcept;
	[[nodiscard]] const char* GetDiscoveryStatusName(DiscoveryStatus a_status) noexcept;
}
