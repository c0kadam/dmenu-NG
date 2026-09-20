#include "SimpleIMEBridge.h"

#include "IMEManager.h"
#include "bin/RuntimeCompatibility.h"

#include <detours/detours.h>

#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>
#include <winver.h>

namespace
{
	constexpr wchar_t kSimpleIMEModuleName[] = L"SimpleIME.dll";
	constexpr std::string_view kSimpleIMEMenuName = "SimpleImeMenu"sv;
	constexpr std::string_view kSimpleIMEToolWindowMenuName = "SimpleIME ToolWindow"sv;
	constexpr std::uint32_t kSimpleIMECharEventType = 0x80000001;

	struct SimpleIMECharEventView
	{
		std::uint32_t type;
		std::uint32_t wcharCode;
		std::uint8_t keyboardIndex;
		std::uint8_t padding[3];
	};

	static_assert(sizeof(RE::GFxEvent) == 0x4);
	static_assert(offsetof(SimpleIMECharEventView, wcharCode) == 0x4);
	static_assert(offsetof(SimpleIMECharEventView, keyboardIndex) == 0x8);
	static_assert(sizeof(SimpleIMECharEventView) == 0xC);

	using AddMessageFn = void (*)(
		RE::UIMessageQueue*,
		const RE::BSFixedString&,
		RE::UI_MESSAGE_TYPE,
		RE::IUIMessageData*);

	AddMessageFn g_originalAddMessage = nullptr;
	using AllowTextInputFn = std::uint8_t (*)(RE::ControlMap*, bool);

	[[nodiscard]] std::uint8_t GetTextEntryCount(const RE::ControlMap* a_controlMap) noexcept
	{
		return static_cast<std::uint8_t>(a_controlMap->GetRuntimeData().textEntryCount);
	}

	[[nodiscard]] std::uint8_t AllowTextInput(RE::ControlMap* a_controlMap, bool a_allow)
	{
		static REL::Relocation<AllowTextInputFn> allowTextInput{ REL::RelocationID(67252, 68552) };
		return allowTextInput(a_controlMap, a_allow);
	}

	[[nodiscard]] std::optional<IME::SimpleIMECompat::Version> GetModuleVersion(HMODULE a_module)
	{
		std::wstring modulePath(32768, L'\0');
		const DWORD pathLength = GetModuleFileNameW(a_module, modulePath.data(), static_cast<DWORD>(modulePath.size()));
		if (pathLength == 0 || pathLength >= static_cast<DWORD>(modulePath.size())) {
			return std::nullopt;
		}
		modulePath.resize(pathLength);

		DWORD unused = 0;
		const DWORD versionInfoSize = GetFileVersionInfoSizeW(modulePath.c_str(), std::addressof(unused));
		if (versionInfoSize == 0) {
			return std::nullopt;
		}

		std::vector<std::byte> versionInfo(versionInfoSize);
		if (!GetFileVersionInfoW(modulePath.c_str(), 0, versionInfoSize, versionInfo.data())) {
			return std::nullopt;
		}

		void* versionValue = nullptr;
		UINT fixedInfoSize = 0;
		if (!VerQueryValueW(versionInfo.data(), L"\\", std::addressof(versionValue), std::addressof(fixedInfoSize))) {
			return std::nullopt;
		}

		const auto* fixedInfo = static_cast<const VS_FIXEDFILEINFO*>(versionValue);
		if (
		    fixedInfo == nullptr || fixedInfoSize < sizeof(VS_FIXEDFILEINFO) || fixedInfo->dwSignature != 0xFEEF04BD) {
			return std::nullopt;
		}

		return IME::SimpleIMECompat::Version{
			HIWORD(fixedInfo->dwFileVersionMS),
			LOWORD(fixedInfo->dwFileVersionMS),
			HIWORD(fixedInfo->dwFileVersionLS),
			LOWORD(fixedInfo->dwFileVersionLS)
		};
	}

	[[nodiscard]] std::string VersionString(const IME::SimpleIMECompat::Version& a_version)
	{
		return fmt::format("{}.{}.{}.{}", a_version.major, a_version.minor, a_version.patch, a_version.revision);
	}

	[[nodiscard]] bool HasRegisteredMenu(RE::UI* a_ui, std::string_view a_name)
	{
		if (a_ui == nullptr) {
			return false;
		}

		const RE::BSFixedString menuName(a_name);
		const auto entry = a_ui->menuMap.find(menuName);
		return entry != a_ui->menuMap.end() && entry->second.create != nullptr;
	}

	void UIMessageQueueAddMessageHook(
		RE::UIMessageQueue* a_queue,
		const RE::BSFixedString& a_menuName,
		RE::UI_MESSAGE_TYPE a_type,
		RE::IUIMessageData* a_data) noexcept
	{
		if (a_type == RE::UI_MESSAGE_TYPE::kScaleformEvent && a_data != nullptr) {
			const auto* scaleformData = static_cast<const RE::BSUIScaleformData*>(a_data);
			const RE::GFxEvent* event = scaleformData->scaleformEvent;
			if (event != nullptr && static_cast<std::uint32_t>(event->type.get()) == kSimpleIMECharEventType) {
				// SimpleIME retains and later frees this event; only copy its verified fixed-layout payload.
				SimpleIMECharEventView eventCopy{};
				std::memcpy(std::addressof(eventCopy), event, sizeof(eventCopy));
				if (eventCopy.wcharCode != 0 && eventCopy.wcharCode <= 0xFFFF) {
					IME::SimpleIMEBridge::Get().TryQueueCharacter(static_cast<char16_t>(eventCopy.wcharCode));
				}
			}
		}

		g_originalAddMessage(a_queue, a_menuName, a_type, a_data);
	}

	[[nodiscard]] bool InstallAddMessageHook()
	{
		const auto runtime = REL::Module::get().version();
		if (!RuntimeCompatibility::IsSupported(runtime)) {
			logger::error("SimpleIME: UI message bridge is not verified for Skyrim {}"sv, runtime.string("."));
			return false;
		}

		REL::Relocation<std::uintptr_t> target{ REL::RelocationID(13530, 13631) };
		const auto targetAddress = target.address();
		const auto text = REL::Module::get().segment(REL::Segment::textx);
		const bool targetIsInSkyrimText =
			text.address() != 0 &&
			targetAddress >= text.address() &&
			targetAddress - text.address() < text.size();
		if (!targetIsInSkyrimText) {
			logger::error("SimpleIME: UIMessageQueue::AddMessage did not resolve inside Skyrim executable code"sv);
			return false;
		}

		g_originalAddMessage = reinterpret_cast<AddMessageFn>(targetAddress);
		LONG error = DetourTransactionBegin();
		if (error == NO_ERROR) {
			error = DetourUpdateThread(GetCurrentThread());
		}
		if (error == NO_ERROR) {
			error = DetourAttach(
				reinterpret_cast<PVOID*>(std::addressof(g_originalAddMessage)),
				reinterpret_cast<PVOID>(UIMessageQueueAddMessageHook));
		}
		if (error == NO_ERROR) {
			error = DetourTransactionCommit();
		} else {
			DetourTransactionAbort();
		}

		if (error != NO_ERROR) {
			g_originalAddMessage = nullptr;
			logger::error("SimpleIME: unable to install UI message bridge (Detours error {})"sv, error);
			return false;
		}
		return true;
	}
}

namespace IME
{
	SimpleIMEBridge& SimpleIMEBridge::Get()
	{
		static SimpleIMEBridge instance;
		return instance;
	}

	void SimpleIMEBridge::DetectAfterPluginsLoaded() noexcept
	{
		if (state_.load(std::memory_order_acquire) != State::NotInitialized) {
			return;
		}

		try {
			const HMODULE module = GetModuleHandleW(kSimpleIMEModuleName);
			if (module == nullptr) {
				statusMessage_.clear();
				state_.store(State::NotDetected, std::memory_order_release);
				return;
			}

			detected_.store(true, std::memory_order_release);
			const auto version = GetModuleVersion(module);
			if (!version) {
				SetUnavailable("SimpleIME version could not be verified. Built-in IME remains available.");
				logger::error("SimpleIME: module detected, but its file version could not be read"sv);
				return;
			}

			version_ = *version;
			if (!SimpleIMECompat::IsSupportedVersion(version_)) {
				SetUnavailable(fmt::format(
					"SimpleIME {} is not supported. dMenu requires a compatible 2.2.1 or newer 2.2.x release.",
					VersionString(version_)));
				logger::error("SimpleIME: unsupported version {}; using the built-in IME backend"sv, VersionString(version_));
				return;
			}

			compatible_.store(true, std::memory_order_release);
			statusMessage_ = fmt::format("SimpleIME {} detected; waiting for renderer initialization.", VersionString(version_));
			state_.store(State::AwaitingRenderer, std::memory_order_release);
			logger::info("SimpleIME: compatible module {} detected"sv, VersionString(version_));
		} catch (const std::exception& error) {
			SetUnavailable("SimpleIME compatibility initialization failed. Built-in IME remains available.");
			logger::error("SimpleIME: detection failed: {}"sv, error.what());
		} catch (...) {
			SetUnavailable("SimpleIME compatibility initialization failed. Built-in IME remains available.");
			logger::error("SimpleIME: detection failed with an unknown error"sv);
		}
	}

	void SimpleIMEBridge::PrepareFrame(bool a_menuEnabled, bool a_screenKeyboardActive)
	{
		textWidgetActivity_.BeginFrame();

		if (state_.load(std::memory_order_acquire) == State::AwaitingRenderer && !activationAttempted_) {
			TryActivateOnRenderThread();
		}

		if (!IsActive()) {
			screenKeyboardActive_ = a_screenKeyboardActive;
			return;
		}

		if (a_screenKeyboardActive != screenKeyboardActive_) {
			logger::info("SimpleIME: screen keyboard became {}"sv, a_screenKeyboardActive ? "active" : "inactive");
			screenKeyboardActive_ = a_screenKeyboardActive;
		}

		if (releaseRequested_.exchange(false, std::memory_order_acq_rel)) {
			ReleaseTextInput();
		}

		if (!a_menuEnabled || a_screenKeyboardActive || !applicationActive_.load(std::memory_order_acquire)) {
			suppressGameCharacters_.store(false, std::memory_order_release);
			pendingCharacters_.SetAccepting(false);
			pendingCharacters_.Clear();
			return;
		}

		DrainPendingCharacters();
	}

	void SimpleIMEBridge::UpdateTextInputState(bool a_menuEnabled, bool a_wantsTextInput, bool a_screenKeyboardActive)
	{
		if (!IsActive()) {
			return;
		}

		const bool textWidgetActive = textWidgetActivity_.IsActive();
		if (textWidgetActive != textWidgetActive_) {
			logger::info("SimpleIME: dMenu text widget became {}"sv, textWidgetActive ? "active" : "inactive");
			textWidgetActive_ = textWidgetActive;
		}
		const bool applicationActive = applicationActive_.load(std::memory_order_acquire);
		const bool releaseRequested = releaseRequested_.load(std::memory_order_acquire);
		// SimpleIME intentionally moves Win32 keyboard focus to its own same-process
		// HWND. Application activation, not focus on Skyrim's HWND, owns this session.
		const bool wantsExternalInput = SimpleIMECompat::ShouldRequestTextInput(
			a_menuEnabled,
			a_wantsTextInput,
			textWidgetActive,
			a_screenKeyboardActive,
			applicationActive,
			releaseRequested);

		if (wantsExternalInput != textInputRequested_) {
			logger::info(
				"SimpleIME: dMenu text input became {} (menu={} wantText={} widgetActive={} applicationActive={} screenKeyboard={} owns={})"sv,
				wantsExternalInput ? "active" : "inactive",
				a_menuEnabled,
				a_wantsTextInput,
				textWidgetActive,
				applicationActive,
				a_screenKeyboardActive,
				ownsTextInput_);
			textInputRequested_ = wantsExternalInput;
		}

		switch (SimpleIMECompat::GetTextInputTransition(ownsTextInput_, wantsExternalInput)) {
		case SimpleIMECompat::TextInputTransition::Acquire:
			AcquireTextInput();
			break;
		case SimpleIMECompat::TextInputTransition::Release:
			ReleaseTextInput();
			break;
		case SimpleIMECompat::TextInputTransition::None:
			if (ownsTextInput_ && wantsExternalInput) {
				pendingCharacters_.SetAccepting(true);
			}
			break;
		}

		const bool suppressGameCharacters = SimpleIMECompat::ShouldSuppressGameInput(
			SimpleIMECompat::GameInputKind::Character,
			IsActive(),
			ownsTextInput_,
			wantsExternalInput);
		suppressGameCharacters_.store(suppressGameCharacters, std::memory_order_release);
	}

	void SimpleIMEBridge::ReportTextWidget(bool a_active) noexcept
	{
		if (IsActive()) {
			textWidgetActivity_.Report(a_active);
		}
	}

	void SimpleIMEBridge::SetMenuEnabled(bool a_enabled)
	{
		if (a_enabled != menuEnabled_) {
			if (IsActive()) {
				logger::info("SimpleIME: dMenu menu {}"sv, a_enabled ? "opened" : "closed");
			}
			menuEnabled_ = a_enabled;
		}
		if (!a_enabled) {
			suppressGameCharacters_.store(false, std::memory_order_release);
			ReleaseTextInput();
			pendingCharacters_.SetAccepting(false);
		}
	}

	void SimpleIMEBridge::OnApplicationActivationChanged(bool a_active) noexcept
	{
		const bool wasActive = applicationActive_.exchange(a_active, std::memory_order_acq_rel);
		if (wasActive != a_active && IsActive()) {
			logger::info("SimpleIME: application became {}"sv, a_active ? "active" : "inactive");
		}
		if (!a_active) {
			suppressGameCharacters_.store(false, std::memory_order_release);
			pendingCharacters_.SetAccepting(false);
			releaseRequested_.store(true, std::memory_order_release);
		}
	}

	bool SimpleIMEBridge::IsDetected() const noexcept
	{
		return detected_.load(std::memory_order_acquire);
	}

	bool SimpleIMEBridge::IsCompatible() const noexcept
	{
		return compatible_.load(std::memory_order_acquire);
	}

	bool SimpleIMEBridge::IsActive() const noexcept
	{
		return active_.load(std::memory_order_acquire);
	}

	bool SimpleIMEBridge::ShouldSuppressGameCharEvent() const noexcept
	{
		return suppressGameCharacters_.load(std::memory_order_acquire);
	}

	SimpleIMEBridge::State SimpleIMEBridge::GetState() const noexcept
	{
		return state_.load(std::memory_order_acquire);
	}

	const SimpleIMECompat::Version& SimpleIMEBridge::GetVersion() const noexcept
	{
		return version_;
	}

	const std::string& SimpleIMEBridge::GetStatusMessage() const noexcept
	{
		return statusMessage_;
	}

	void SimpleIMEBridge::TryQueueCharacter(char16_t a_codeUnit) noexcept
	{
		if (!IsActive()) {
			return;
		}

		if (pendingCharacters_.Push(a_codeUnit) == SimpleIMECompat::QueuePushResult::Full) {
			queueOverflowed_.store(true, std::memory_order_release);
		}
	}

	void SimpleIMEBridge::TryActivateOnRenderThread() noexcept
	{
		activationAttempted_ = true;
		try {
			auto* ui = RE::UI::GetSingleton();
			if (!HasRegisteredMenu(ui, kSimpleIMEMenuName) || !HasRegisteredMenu(ui, kSimpleIMEToolWindowMenuName)) {
				SetUnavailable("SimpleIME was detected but did not finish initialization. Built-in IME remains available.");
				logger::error("SimpleIME: required menu registrations were not present after renderer initialization"sv);
				return;
			}

			if (RE::ControlMap::GetSingleton() == nullptr) {
				SetUnavailable("Skyrim text input could not be initialized. Built-in IME remains available.");
				logger::error("SimpleIME: ControlMap is unavailable; external bridge disabled"sv);
				return;
			}

			std::string activeStatus = fmt::format("SimpleIME {} is active.", VersionString(version_));
			if (!InstallAddMessageHook()) {
				SetUnavailable("The SimpleIME compatibility bridge could not be installed. Built-in IME remains available.");
				return;
			}

			Manager::Get().Reset("SimpleIME backend activated");
			statusMessage_ = std::move(activeStatus);
			active_.store(true, std::memory_order_release);
			state_.store(State::Active, std::memory_order_release);
			logger::info("SimpleIME: external IME backend selected"sv);
		} catch (const std::exception& error) {
			SetUnavailable("SimpleIME compatibility initialization failed. Built-in IME remains available.");
			logger::error("SimpleIME: renderer activation failed: {}"sv, error.what());
		} catch (...) {
			SetUnavailable("SimpleIME compatibility initialization failed. Built-in IME remains available.");
			logger::error("SimpleIME: renderer activation failed with an unknown error"sv);
		}
	}

	void SimpleIMEBridge::AcquireTextInput()
	{
		if (ownsTextInput_ || !IsActive()) {
			return;
		}

		auto* controlMap = RE::ControlMap::GetSingleton();
		if (controlMap == nullptr) {
			SetUnavailable("Skyrim text input became unavailable. Built-in IME has been restored.");
			logger::error("SimpleIME: ControlMap became unavailable while acquiring text input"sv);
			return;
		}

		const std::uint8_t before = GetTextEntryCount(controlMap);
		if (before == (std::numeric_limits<std::uint8_t>::max)()) {
			SetUnavailable("Skyrim text input count is saturated. Built-in IME has been restored.");
			logger::error("SimpleIME: refusing to acquire a saturated Skyrim text input counter"sv);
			return;
		}
		if (before > 0) {
			logger::warn(
				"SimpleIME: acquiring dMenu text input with pre-existing textEntryCount={}"sv,
				static_cast<unsigned>(before));
		}

		const std::uint8_t returned = AllowTextInput(controlMap, true);
		const std::uint8_t after = GetTextEntryCount(controlMap);

		const auto expected = static_cast<std::uint8_t>(before + 1);
		if (!SimpleIMECompat::IsValidAcquireCount(before, returned, after)) {
			SetUnavailable("Skyrim text input count did not advance correctly. Built-in IME has been restored.");
			logger::error(
				"SimpleIME: dMenu acquire failed (before={} returned={} after={} expected={} owns=false)"sv,
				static_cast<unsigned>(before),
				static_cast<unsigned>(returned),
				static_cast<unsigned>(after),
				static_cast<unsigned>(expected));
			return;
		}

		ownsTextInput_ = true;
		pendingCharacters_.SetAccepting(true);
		logger::info(
			"SimpleIME: dMenu acquired SimpleIME reference (before={} returned={} after={} owns=true)"sv,
			static_cast<unsigned>(before),
			static_cast<unsigned>(returned),
			static_cast<unsigned>(after));
	}

	void SimpleIMEBridge::ReleaseTextInput()
	{
		suppressGameCharacters_.store(false, std::memory_order_release);
		pendingCharacters_.SetAccepting(false);
		if (!ownsTextInput_) {
			return;
		}

		bool releaseValid = false;
		if (auto* controlMap = RE::ControlMap::GetSingleton(); controlMap != nullptr) {
			const std::uint8_t before = GetTextEntryCount(controlMap);
			if (before == 0) {
				ownsTextInput_ = false;
				logger::error("SimpleIME: refusing to decrement a zero Skyrim text input counter"sv);
			} else {
				const std::uint8_t returned = AllowTextInput(controlMap, false);
				const std::uint8_t after = GetTextEntryCount(controlMap);
				const auto expected = static_cast<std::uint8_t>(before - 1);
				releaseValid = SimpleIMECompat::IsValidReleaseCount(before, returned, after);
				ownsTextInput_ = false;
				logger::info(
					"SimpleIME: dMenu released SimpleIME reference (before={} returned={} after={} owns=false)"sv,
					static_cast<unsigned>(before),
					static_cast<unsigned>(returned),
					static_cast<unsigned>(after));
				if (!releaseValid) {
					logger::error(
						"SimpleIME: AllowTextInput(false) count validation failed (expected={})"sv,
						static_cast<unsigned>(expected));
				}
			}
		} else {
			logger::error("SimpleIME: ControlMap unavailable while releasing dMenu text input ownership"sv);
			ownsTextInput_ = false;
		}
		if (!releaseValid) {
			SetUnavailable("Skyrim text input count did not release correctly. Built-in IME has been restored.");
		}
	}

	void SimpleIMEBridge::SetUnavailable(std::string a_reason)
	{
		suppressGameCharacters_.store(false, std::memory_order_release);
		pendingCharacters_.SetAccepting(false);
		active_.store(false, std::memory_order_release);
		statusMessage_ = std::move(a_reason);
		state_.store(State::Unavailable, std::memory_order_release);
	}

	void SimpleIMEBridge::DrainPendingCharacters()
	{
		std::array<char16_t, kPendingCharacterCapacity> pending{};
		const std::size_t count = pendingCharacters_.Drain(pending);
		for (std::size_t index = 0; index < count; ++index) {
			ImGui::GetIO().AddInputCharacterUTF16(static_cast<ImWchar16>(pending[index]));
		}

		if (queueOverflowed_.exchange(false, std::memory_order_acq_rel) && !queueOverflowLogged_) {
			queueOverflowLogged_ = true;
			logger::error("SimpleIME: pending input queue overflowed; additional characters were dropped"sv);
		}
	}
}
