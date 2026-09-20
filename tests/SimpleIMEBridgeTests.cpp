// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (c) 2026 C0kadam
// See LICENSE and EXCEPTIONS.md for the project terms.

#include "SimpleIMEBridgeSupport.h"

#include <array>
#include <iostream>

namespace
{
	int g_failures = 0;

	void Expect(bool a_condition, const char* a_name)
	{
		if (!a_condition) {
			std::cerr << "FAILED: " << a_name << '\n';
			++g_failures;
		}
	}
}

int main()
{
	using IME::SimpleIMECompat::BoundedUtf16Queue;
	using IME::SimpleIMECompat::GetTextInputTransition;
	using IME::SimpleIMECompat::GameInputKind;
	using IME::SimpleIMECompat::IsValidAcquireCount;
	using IME::SimpleIMECompat::IsValidReleaseCount;
	using IME::SimpleIMECompat::IsSupportedVersion;
	using IME::SimpleIMECompat::QueuePushResult;
	using IME::SimpleIMECompat::ShouldRequestTextInput;
	using IME::SimpleIMECompat::ShouldSuppressGameInput;
	using IME::SimpleIMECompat::TextInputTransition;
	using IME::SimpleIMECompat::TextWidgetActivity;
	using IME::SimpleIMECompat::Version;

	Expect(!IsSupportedVersion(Version{ 2, 2, 0, 0 }), "SimpleIME 2.2.0 is rejected");
	Expect(IsSupportedVersion(Version{ 2, 2, 1, 0 }), "SimpleIME 2.2.1 is accepted");
	Expect(IsSupportedVersion(Version{ 2, 2, 99, 0 }), "newer SimpleIME 2.2.x is accepted");
	Expect(!IsSupportedVersion(Version{ 2, 3, 0, 0 }), "unknown SimpleIME 2.3.x is rejected");
	Expect(!IsSupportedVersion(Version{ 3, 0, 0, 0 }), "unknown SimpleIME major version is rejected");
	Expect(GetTextInputTransition(false, false) == TextInputTransition::None, "inactive text input remains unowned");
	Expect(GetTextInputTransition(false, true) == TextInputTransition::Acquire, "text focus acquires once");
	Expect(GetTextInputTransition(true, true) == TextInputTransition::None, "held text input is not acquired every frame");
	Expect(GetTextInputTransition(true, false) == TextInputTransition::Release, "focus loss releases owned text input");
	Expect(!ShouldSuppressGameInput(GameInputKind::Character, false, true, true),
		"inactive SimpleIME leaves normal character input unchanged");
	Expect(!ShouldSuppressGameInput(GameInputKind::Character, true, false, true),
		"detected SimpleIME without dMenu ownership leaves character input unchanged");
	Expect(ShouldSuppressGameInput(GameInputKind::Character, true, true, true),
		"owned active SimpleIME session suppresses raw character injection");
	Expect(!ShouldSuppressGameInput(GameInputKind::Character, true, true, false),
		"inactive dMenu text session leaves character input unchanged");
	Expect(!ShouldSuppressGameInput(GameInputKind::Key, true, true, true),
		"SimpleIME character suppression never suppresses key events");

	TextWidgetActivity widgetActivity;
	Expect(!widgetActivity.IsActive(), "widget aggregation begins inactive");
	widgetActivity.Report(false);
	Expect(!widgetActivity.IsActive(), "inactive widgets do not request text input");
	widgetActivity.Report(true);
	widgetActivity.Report(false);
	Expect(widgetActivity.IsActive(), "another inactive widget cannot erase an active widget in the same frame");
	widgetActivity.BeginFrame();
	Expect(!widgetActivity.IsActive(), "widget aggregation resets at frame start");

	Expect(ShouldRequestTextInput(true, false, true, false, true, false),
		"explicit active widget requests external text input");
	Expect(ShouldRequestTextInput(true, true, false, false, true, false),
		"ImGui WantTextInput remains a fallback");
	Expect(!ShouldRequestTextInput(true, true, true, true, true, false),
		"screen keyboard suppresses external text input");
	Expect(!ShouldRequestTextInput(false, true, true, false, true, false),
		"closed menu cannot request external text input");
	constexpr bool gameHwndHasKeyboardFocus = false;
	const bool simpleIMEChildFocusedRequest = ShouldRequestTextInput(true, true, true, false, true, false);
	Expect(!gameHwndHasKeyboardFocus && simpleIMEChildFocusedRequest,
		"SimpleIME child HWND focus keeps same-process text input ownership");

	const bool altTabRequest = ShouldRequestTextInput(true, true, true, false, false, false);
	Expect(GetTextInputTransition(true, altTabRequest) == TextInputTransition::Release,
		"application deactivation releases owned text input");
	Expect(GetTextInputTransition(false, altTabRequest) == TextInputTransition::None,
		"application deactivation releases only once");

	const bool inactiveFieldRequest = ShouldRequestTextInput(true, false, false, false, true, false);
	Expect(GetTextInputTransition(true, inactiveFieldRequest) == TextInputTransition::Release,
		"inactive text field releases owned text input");
	Expect(GetTextInputTransition(false, inactiveFieldRequest) == TextInputTransition::None,
		"inactive text field releases only once");

	const bool closedMenuRequest = ShouldRequestTextInput(false, true, true, false, true, false);
	Expect(GetTextInputTransition(true, closedMenuRequest) == TextInputTransition::Release,
		"closed menu releases owned text input");
	Expect(GetTextInputTransition(false, closedMenuRequest) == TextInputTransition::None,
		"closed menu releases only once");

	const bool screenKeyboardRequest = ShouldRequestTextInput(true, true, true, true, true, false);
	Expect(GetTextInputTransition(true, screenKeyboardRequest) == TextInputTransition::Release,
		"screen keyboard releases owned external text input");
	Expect(GetTextInputTransition(false, screenKeyboardRequest) == TextInputTransition::None,
		"screen keyboard releases only once");

	const bool stableFieldRequest = ShouldRequestTextInput(true, true, true, false, true, false);
	Expect(GetTextInputTransition(false, stableFieldRequest) == TextInputTransition::Acquire,
		"stable active field acquires once");
	bool stableOwnership = true;
	for (std::size_t frame = 0; frame < 120; ++frame) {
		stableOwnership = stableOwnership &&
			GetTextInputTransition(true, stableFieldRequest) == TextInputTransition::None;
	}
	Expect(stableOwnership, "stable active field causes no repeated acquire or release");
	Expect(IsValidAcquireCount(0, 240, 1), "acquire accepts a zero-to-one transition despite a meaningless return byte");
	Expect(IsValidAcquireCount(1, 0, 2), "acquire preserves an external reference despite a meaningless return byte");
	Expect(IsValidAcquireCount(4, 5, 5), "acquire count preserves other owners");
	Expect(!IsValidAcquireCount(0, 0, 0), "acquire count rejects no increment");
	Expect(!IsValidAcquireCount(255, 255, 255), "acquire count rejects saturation");
	Expect(!IsValidAcquireCount(0, 1, 0), "acquire rejects a missing zero-to-one counter transition");
	Expect(!IsValidAcquireCount(1, 2, 1), "acquire rejects a missing one-to-two counter transition");
	Expect(IsValidReleaseCount(1, 240, 0), "release accepts a one-to-zero transition despite a meaningless return byte");
	Expect(IsValidReleaseCount(2, 0, 1), "release returns to an external baseline despite a meaningless return byte");
	Expect(IsValidReleaseCount(5, 4, 4), "release count preserves other owners");
	Expect(!IsValidReleaseCount(0, 0, 0), "release count rejects an unowned underflow");
	Expect(!IsValidReleaseCount(0, 240, 255), "release cannot wrap an unowned zero counter");
	Expect(!IsValidReleaseCount(1, 0, 1), "release rejects a missing decrement");

	std::uint8_t externalBaselineCount = 1;
	bool repeatedExternalBaselinePreserved = true;
	for (std::size_t session = 0; session < 8; ++session) {
		const std::uint8_t acquireBefore = externalBaselineCount;
		const std::uint8_t acquireAfter = static_cast<std::uint8_t>(acquireBefore + 1);
		repeatedExternalBaselinePreserved = repeatedExternalBaselinePreserved &&
			IsValidAcquireCount(acquireBefore, acquireAfter, acquireAfter);
		externalBaselineCount = acquireAfter;

		const std::uint8_t releaseBefore = externalBaselineCount;
		const std::uint8_t releaseAfter = static_cast<std::uint8_t>(releaseBefore - 1);
		repeatedExternalBaselinePreserved = repeatedExternalBaselinePreserved &&
			IsValidReleaseCount(releaseBefore, releaseAfter, releaseAfter);
		externalBaselineCount = releaseAfter;
	}
	Expect(repeatedExternalBaselinePreserved && externalBaselineCount == 1,
		"repeated dMenu sessions preserve a one-reference external baseline");

	BoundedUtf16Queue<3> queue;
	Expect(queue.Push(u'A') == QueuePushResult::Ignored, "inactive queue ignores input");
	queue.SetAccepting(true);
	Expect(queue.Push(static_cast<char16_t>(0x4E2D)) == QueuePushResult::Queued, "BMP UTF-16 unit is queued");
	Expect(queue.Push(static_cast<char16_t>(0xD83D)) == QueuePushResult::Queued, "high surrogate is queued");
	Expect(queue.Push(static_cast<char16_t>(0xDE00)) == QueuePushResult::Queued, "low surrogate is queued");
	Expect(queue.Push(u'X') == QueuePushResult::Full, "bounded queue rejects overflow");

	std::array<char16_t, 3> drained{};
	Expect(queue.Drain(drained) == 3, "all queued UTF-16 units are drained");
	Expect(drained[0] == static_cast<char16_t>(0x4E2D), "BMP unit order is preserved");
	Expect(drained[1] == static_cast<char16_t>(0xD83D) && drained[2] == static_cast<char16_t>(0xDE00),
		"surrogate pair order is preserved");
	Expect(queue.Size() == 0, "drain empties the queue");

	queue.SetAccepting(true);
	Expect(queue.Push(u'B') == QueuePushResult::Queued, "queue accepts a new session");
	queue.SetAccepting(false);
	Expect(queue.Size() == 0, "ending a session clears pending input");
	Expect(queue.Push(u'C') == QueuePushResult::Ignored, "ended session ignores late input");

	if (g_failures == 0) {
		std::cout << "All SimpleIME bridge helper tests passed.\n";
	}
	return g_failures == 0 ? 0 : 1;
}
