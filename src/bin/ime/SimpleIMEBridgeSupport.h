#pragma once

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>

namespace IME::SimpleIMECompat
{
	struct Version
	{
		std::uint32_t major{ 0 };
		std::uint32_t minor{ 0 };
		std::uint32_t patch{ 0 };
		std::uint32_t revision{ 0 };
	};

	[[nodiscard]] constexpr bool IsSupportedVersion(const Version& a_version) noexcept
	{
		return a_version.major == 2 &&
		       a_version.minor == 2 &&
		       a_version.patch >= 1;
	}

	enum class TextInputTransition
	{
		None,
		Acquire,
		Release
	};

	enum class GameInputKind
	{
		Character,
		Key
	};

	[[nodiscard]] constexpr bool ShouldSuppressGameInput(
		GameInputKind a_kind,
		bool a_bridgeActive,
		bool a_ownsTextInput,
		bool a_textSessionActive) noexcept
	{
		return a_kind == GameInputKind::Character &&
		       a_bridgeActive &&
		       a_ownsTextInput &&
		       a_textSessionActive;
	}

	[[nodiscard]] constexpr TextInputTransition GetTextInputTransition(bool a_ownsTextInput, bool a_wantsTextInput) noexcept
	{
		if (a_wantsTextInput && !a_ownsTextInput) {
			return TextInputTransition::Acquire;
		}
		if (!a_wantsTextInput && a_ownsTextInput) {
			return TextInputTransition::Release;
		}
		return TextInputTransition::None;
	}

	class TextWidgetActivity
	{
	public:
		void BeginFrame() noexcept
		{
			activeThisFrame_ = false;
		}

		void Report(bool a_active) noexcept
		{
			activeThisFrame_ = activeThisFrame_ || a_active;
		}

		[[nodiscard]] bool IsActive() const noexcept
		{
			return activeThisFrame_;
		}

	private:
		bool activeThisFrame_{ false };
	};

	[[nodiscard]] constexpr bool ShouldRequestTextInput(
		bool a_menuEnabled,
		bool a_wantsTextInput,
		bool a_textWidgetActive,
		bool a_screenKeyboardActive,
		bool a_applicationActive,
		bool a_releaseRequested) noexcept
	{
		return a_menuEnabled &&
		       (a_textWidgetActive || a_wantsTextInput) &&
		       !a_screenKeyboardActive &&
		       a_applicationActive &&
		       !a_releaseRequested;
	}

	[[nodiscard]] constexpr bool IsValidAcquireCount(
		std::uint8_t a_before,
		std::uint8_t,
		std::uint8_t a_after) noexcept
	{
		return a_before < (std::numeric_limits<std::uint8_t>::max)() &&
		       a_after == static_cast<std::uint8_t>(a_before + 1);
	}

	[[nodiscard]] constexpr bool IsValidReleaseCount(
		std::uint8_t a_before,
		std::uint8_t,
		std::uint8_t a_after) noexcept
	{
		return a_before > 0 &&
		       a_after == static_cast<std::uint8_t>(a_before - 1);
	}

	enum class QueuePushResult
	{
		Ignored,
		Queued,
		Full
	};

	template <std::size_t Capacity>
	class BoundedUtf16Queue
	{
	public:
		static_assert(Capacity > 0);

		BoundedUtf16Queue() noexcept = default;
		BoundedUtf16Queue(const BoundedUtf16Queue&) = delete;
		BoundedUtf16Queue& operator=(const BoundedUtf16Queue&) = delete;

		void SetAccepting(bool a_accepting) noexcept
		{
			AcquireSRWLockExclusive(std::addressof(lock_));
			accepting_ = a_accepting;
			if (!accepting_) {
				size_ = 0;
			}
			ReleaseSRWLockExclusive(std::addressof(lock_));
		}

		[[nodiscard]] QueuePushResult Push(char16_t a_codeUnit) noexcept
		{
			AcquireSRWLockExclusive(std::addressof(lock_));
			QueuePushResult result = QueuePushResult::Ignored;
			if (accepting_) {
				if (size_ < Capacity) {
					storage_[size_++] = a_codeUnit;
					result = QueuePushResult::Queued;
				} else {
					result = QueuePushResult::Full;
				}
			}
			ReleaseSRWLockExclusive(std::addressof(lock_));
			return result;
		}

		[[nodiscard]] std::size_t Drain(std::span<char16_t> a_output) noexcept
		{
			AcquireSRWLockExclusive(std::addressof(lock_));
			const std::size_t count = (std::min)(size_, a_output.size());
			std::copy_n(storage_.begin(), count, a_output.begin());
			if (count < size_) {
				for (std::size_t index = count; index < size_; ++index) {
					storage_[index - count] = storage_[index];
				}
			}
			size_ -= count;
			ReleaseSRWLockExclusive(std::addressof(lock_));
			return count;
		}

		void Clear() noexcept
		{
			AcquireSRWLockExclusive(std::addressof(lock_));
			size_ = 0;
			ReleaseSRWLockExclusive(std::addressof(lock_));
		}

		[[nodiscard]] std::size_t Size() const noexcept
		{
			AcquireSRWLockShared(std::addressof(lock_));
			const std::size_t result = size_;
			ReleaseSRWLockShared(std::addressof(lock_));
			return result;
		}

	private:
		mutable SRWLOCK lock_ = SRWLOCK_INIT;
		std::array<char16_t, Capacity> storage_{};
		std::size_t size_{ 0 };
		bool accepting_{ false };
	};
}
