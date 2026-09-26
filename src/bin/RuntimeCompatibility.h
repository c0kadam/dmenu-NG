#pragma once

namespace RuntimeCompatibility
{
	inline constexpr REL::Version SKYRIM_1_5_97{ 1, 5, 97, 0 };
	inline constexpr REL::Version SKYRIM_1_6_1170{ 1, 6, 1170, 0 };
	inline constexpr REL::Version SKYRIM_1_7_99{ 1, 7, 99, 0 };
	inline constexpr REL::Version SKYRIM_1_7_104{ 1, 7, 104, 0 };

	enum class Hook : std::size_t
	{
		D3DInit,
		DXGIPresent,
		Weather,
		InputEventDispatch,
		Count
	};

	struct ResolvedCallSite
	{
		std::uintptr_t address;
		std::uintptr_t originalTarget;
	};

	[[nodiscard]] bool IsSupported(REL::Version a_version) noexcept;
	[[nodiscard]] std::string_view GetHookName(Hook a_hook) noexcept;

	// Resolve and validate every runtime-sensitive call before patching any of them.
	[[nodiscard]] bool PreflightHooks();
	// Recheck a preflighted site and its current chain before deferred installation.
	[[nodiscard]] bool RevalidateCallSite(Hook a_hook);
	[[nodiscard]] const ResolvedCallSite* GetResolvedCallSite(Hook a_hook) noexcept;
}
