#pragma once

#include "PCH.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

class ScreenKeyboardBridge
{
public:
	using ResultCallback = std::function<void(std::string)>;
	struct RequestOptions
	{
		bool digitsOnly = false;
		bool allowSpace = true;
	};

	enum class Layout
	{
		Latin,
		Hiragana,
		Katakana,
		Hangul
	};

	static ScreenKeyboardBridge& GetSingleton();

	bool RequestTextBox(
		const std::string& initialValue,
		ResultCallback callback,
		RequestOptions options = {});
	void CancelActiveRequest();

	void Draw();

	bool IsAwaitingResult() const;

private:
	ScreenKeyboardBridge() = default;

	std::atomic<bool> awaitingResult_{ false };
	std::mutex stateLock_;
	ResultCallback pendingCallback_;
	bool internalKeyboardOpen_{ false };
	bool internalPopupRequested_{ false };
	bool internalFocusTextField_{ false };
	bool internalFocusFirstKey_{ false };
	bool openedWithGamepad_{ false };
	int deferredDefaultFocusFrames_{ 0 };
	bool uppercase_{ false };
	Layout currentLayout_{ Layout::Latin };
	RequestOptions activeOptions_{};
	std::string internalBuffer_;
};
