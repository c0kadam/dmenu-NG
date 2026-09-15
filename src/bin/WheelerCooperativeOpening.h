#pragma once

#include "PCH.h"

#include <cstdint>

namespace WheelerCooperativeOpening
{
	inline constexpr std::uint64_t kOwnerId = 0x444D454E55000001ULL;

	// Two bounded discovery attempts are made: during dMenu initialization and
	// once at SKSE post-load after all normal plugins are resident.
	void Initialize();
	void RetryInitializationAfterPluginsLoaded();
	void PublishCurrentBindings();
	void ClearRegistration();

	[[nodiscard]] bool IsCurrentEventMatched(std::uintptr_t eventIdentity);

	class ConsumerScope
	{
	public:
		explicit ConsumerScope(RE::InputEvent** events);
		~ConsumerScope();

		ConsumerScope(const ConsumerScope&) = delete;
		ConsumerScope& operator=(const ConsumerScope&) = delete;
		ConsumerScope(ConsumerScope&&) = delete;
		ConsumerScope& operator=(ConsumerScope&&) = delete;

		void SuppressOwnedEvents(RE::InputEvent** events) const;

	private:
		std::uint64_t _token{ 0 };
	};
}
