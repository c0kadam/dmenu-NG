// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace RuntimeCompatibility
{
	enum class Hook : std::size_t
	{
		D3DInit,
		DXGIPresent,
		Weather,
		InputEventDispatch,
		Count
	};

	using RuntimeVersion = std::array<std::uint16_t, 4>;
	struct RuntimeOffsets
	{
		RuntimeVersion version;
		std::ptrdiff_t d3dInit;
		std::ptrdiff_t dxgiPresent;
		std::ptrdiff_t weather;
		std::ptrdiff_t inputEventDispatch;
	};

	// Audited offsets: keep known runtimes on the exact-offset path.
	inline constexpr std::array RUNTIME_OFFSETS{
		RuntimeOffsets{ { 1, 5, 97, 0 }, 0x9, 0x9, 0x29C, 0x7B },
		RuntimeOffsets{ { 1, 6, 1170, 0 }, 0x275, 0x9, 0x3E6, 0x7B },
		RuntimeOffsets{ { 1, 7, 99, 0 }, 0x275, 0x9, 0x3E6, 0x7B },
		RuntimeOffsets{ { 1, 7, 104, 0 }, 0x275, 0x9, 0x3E6, 0x7B }
	};

	[[nodiscard]] constexpr const RuntimeOffsets* FindRuntimeOffsets(RuntimeVersion a_version) noexcept
	{
		for (const auto& entry : RUNTIME_OFFSETS) {
			if (entry.version == a_version) {
				return &entry;
			}
		}
		return nullptr;
	}

	enum class ResolutionPath { Audited, Discovery, Unsupported };

	[[nodiscard]] constexpr ResolutionPath SelectResolutionPath(RuntimeVersion a_version) noexcept
	{
		if (FindRuntimeOffsets(a_version)) {
			return ResolutionPath::Audited;
		}
		// Unknown SE/VR layouts lack audited target IDs and D3D context. Do not guess.
		return a_version[0] == 1 && a_version >= RuntimeVersion{ 1, 6, 629, 0 } ?
		           ResolutionPath::Discovery : ResolutionPath::Unsupported;
	}
}
