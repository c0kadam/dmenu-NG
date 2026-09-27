// SPDX-License-Identifier: GPL-3.0-or-later
#include "CallSiteDiscovery.h"

#include <hde64.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace RuntimeCompatibility::Validation
{
	namespace
	{
		template <std::size_t N>
		bool Matches(std::span<const std::uint8_t> a_bytes, std::size_t a_offset,
			const std::array<std::uint8_t, N>& a_expected) noexcept
		{
			return a_offset <= a_bytes.size() && N <= a_bytes.size() - a_offset &&
			       std::equal(a_expected.begin(), a_expected.end(), a_bytes.begin() + a_offset);
		}
	}

	bool MatchesHookContext(Hook a_hook, bool a_isAE,
		std::span<const std::uint8_t> a_bytes, std::size_t a_callOffset) noexcept
	{
		if (a_callOffset > a_bytes.size() || a_bytes.size() - a_callOffset < 5) {
			return false;
		}
		switch (a_hook) {
		case Hook::D3DInit:
			return !a_isAE || Matches(a_bytes, a_callOffset + 5, std::array<std::uint8_t, 4>{ 0x44, 0x39, 0x6E, 0x2C });
		case Hook::DXGIPresent:
			return a_callOffset >= 5 &&
			       Matches(a_bytes, a_callOffset - 5, std::array<std::uint8_t, 5>{ 0xB9, 1, 0, 0, 0 }) &&
			       Matches(a_bytes, a_callOffset + 5, std::array<std::uint8_t, 2>{ 0x80, 0x3D });
		case Hook::Weather: {
			if (a_callOffset < 3 || a_bytes.size() - a_callOffset < 8) {
				return false;
			}
			WeatherCallContext before{}, after{};
			std::copy_n(a_bytes.begin() + a_callOffset - 3, 3, before.begin());
			std::copy_n(a_bytes.begin() + a_callOffset + 5, 3, after.begin());
			return MatchesWeatherCallContext(a_isAE, before, after);
		}
		case Hook::InputEventDispatch:
			return a_callOffset >= 3 &&
			       Matches(a_bytes, a_callOffset - 3, std::array<std::uint8_t, 3>{ 0x48, 0x8B, 0xCE }) &&
			       Matches(a_bytes, a_callOffset + 5, std::array<std::uint8_t, 3>{ 0x48, 0x8B, 0x0D });
		default:
			return false;
		}
	}

	DiscoveryResult DiscoverCallSites(Hook a_hook, bool a_isAE,
		std::span<const std::uint8_t> a_function, std::uintptr_t a_base,
		const TargetValidator& a_validateTarget)
	{
		DiscoveryResult result;
		if (!a_isAE || a_hook >= Hook::Count || a_function.empty() || !a_validateTarget ||
		    a_function.size() > kMaxDiscoveryFunctionBytes || a_base == 0 ||
		    a_base > (std::numeric_limits<std::uintptr_t>::max)() - a_function.size()) {
			return result;
		}
		std::size_t previousInstruction = 0;
		for (std::size_t offset = 0; offset < a_function.size();) {
			// HDE may read past a short instruction: decode a padded local copy only.
			std::array<std::uint8_t, 32> instruction{};
			std::copy_n(a_function.begin() + offset,
				(std::min)(instruction.size(), a_function.size() - offset), instruction.begin());
			hde64s decoded{};
			const auto length = hde64_disasm(instruction.data(), &decoded);
			if (length == 0 || length > 15 || (decoded.flags & F_ERROR) != 0 || length > a_function.size() - offset) {
				result.status = DiscoveryStatus::DecodeFailure;
				result.failureOffset = offset;
				result.selected.reset();
				return result;
			}
			if (instruction[0] == 0xE8 && length == 5) {
				DiscoveryCandidate candidate{ a_base + offset, 0, offset };
				std::int32_t displacement{};
				std::memcpy(&displacement, instruction.data() + 1, sizeof(displacement));
				const auto next = candidate.address + 5;
				const auto magnitude = static_cast<std::uint64_t>(displacement < 0 ? -static_cast<std::int64_t>(displacement) : displacement);
				if (displacement < 0 && magnitude < next) {
					candidate.target = next - magnitude;
				} else if (displacement >= 0 && magnitude <= (std::numeric_limits<std::uintptr_t>::max)() - next) {
					candidate.target = next + magnitude;
				}
				const std::size_t precedingLength = a_hook == Hook::D3DInit ? 0 : a_hook == Hook::DXGIPresent ? 5 : 3;
				const bool precedingBoundary = precedingLength == 0 ||
					(offset >= precedingLength && previousInstruction == offset - precedingLength);
				candidate.contextResult = ClassifyCallSite(true, instruction[0], precedingBoundary &&
					MatchesHookContext(a_hook, a_isAE, a_function, offset));
				if (candidate.contextResult == CallSiteResult::Valid) {
					candidate.targetResult = candidate.target == 0 ? TargetResult::ExternalTargetNotExecutable :
						a_validateTarget(candidate.address, candidate.target);
					if (*candidate.targetResult == TargetResult::VanillaTargetAccepted ||
					    *candidate.targetResult == TargetResult::ExternalChainAccepted) {
						++result.validCount;
						result.selected = candidate;
					}
				}
				result.candidates.push_back(candidate);
			}
			previousInstruction = offset;
			offset += length;
		}
		result.status = result.validCount == 1 ? DiscoveryStatus::Unique :
		                result.validCount == 0 ? DiscoveryStatus::NoMatch : DiscoveryStatus::Ambiguous;
		if (result.status != DiscoveryStatus::Unique) {
			result.selected.reset();
		}
		return result;
	}

	const char* GetTargetResultName(TargetResult a_result) noexcept
	{
		switch (a_result) {
		case TargetResult::VanillaTargetAccepted: return "expected Skyrim target";
		case TargetResult::ExternalChainAccepted: return "allowed executable chain";
		case TargetResult::UnexpectedSkyrimTarget: return "unexpected Skyrim target";
		case TargetResult::ExternalTargetNotExecutable: return "non-executable or invalid target";
		case TargetResult::ExternalChainNotAllowed: return "external chain disallowed";
		case TargetResult::DuplicateOrSelfTarget: return "duplicate/self target";
		default: return "unknown target result";
		}
	}

	const char* GetDiscoveryStatusName(DiscoveryStatus a_status) noexcept
	{
		switch (a_status) {
		case DiscoveryStatus::Unique: return "unique";
		case DiscoveryStatus::NoMatch: return "no valid candidates";
		case DiscoveryStatus::Ambiguous: return "ambiguous";
		case DiscoveryStatus::InvalidRange: return "invalid/oversized function range";
		case DiscoveryStatus::DecodeFailure: return "instruction decode failed";
		default: return "unknown discovery result";
		}
	}
}
