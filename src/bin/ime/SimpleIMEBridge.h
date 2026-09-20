#pragma once

#include "SimpleIMEBridgeSupport.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace IME
{
	class SimpleIMEBridge
	{
	public:
		enum class State
		{
			NotInitialized,
			NotDetected,
			AwaitingRenderer,
			Active,
			Unavailable
		};

		static SimpleIMEBridge& Get();

		void DetectAfterPluginsLoaded() noexcept;
		void PrepareFrame(bool a_menuEnabled, bool a_screenKeyboardActive);
		void UpdateTextInputState(bool a_menuEnabled, bool a_wantsTextInput, bool a_screenKeyboardActive);
		void ReportTextWidget(bool a_active) noexcept;
		void SetMenuEnabled(bool a_enabled);
		void OnApplicationActivationChanged(bool a_active) noexcept;

		[[nodiscard]] bool IsDetected() const noexcept;
		[[nodiscard]] bool IsCompatible() const noexcept;
		[[nodiscard]] bool IsActive() const noexcept;
		[[nodiscard]] bool ShouldSuppressGameCharEvent() const noexcept;
		[[nodiscard]] State GetState() const noexcept;
		[[nodiscard]] const SimpleIMECompat::Version& GetVersion() const noexcept;
		[[nodiscard]] const std::string& GetStatusMessage() const noexcept;

		// Called only by the UIMessageQueue hook after the foreign event was validated.
		void TryQueueCharacter(char16_t a_codeUnit) noexcept;

	private:
		static constexpr std::size_t kPendingCharacterCapacity = 4096;

		SimpleIMEBridge() = default;

		void TryActivateOnRenderThread() noexcept;
		void AcquireTextInput();
		void ReleaseTextInput();
		void SetUnavailable(std::string a_reason);
		void DrainPendingCharacters();

		std::atomic<State> state_{ State::NotInitialized };
		std::atomic_bool detected_{ false };
		std::atomic_bool compatible_{ false };
		std::atomic_bool active_{ false };
		std::atomic_bool applicationActive_{ true };
		std::atomic_bool suppressGameCharacters_{ false };
		std::atomic_bool releaseRequested_{ false };
		std::atomic_bool queueOverflowed_{ false };
		SimpleIMECompat::BoundedUtf16Queue<kPendingCharacterCapacity> pendingCharacters_;
		SimpleIMECompat::TextWidgetActivity textWidgetActivity_;
		SimpleIMECompat::Version version_{};
		std::string statusMessage_;
		bool activationAttempted_{ false };
		bool ownsTextInput_{ false };
		bool textInputRequested_{ false };
		bool menuEnabled_{ false };
		bool textWidgetActive_{ false };
		bool screenKeyboardActive_{ false };
		bool queueOverflowLogged_{ false };
	};
}
