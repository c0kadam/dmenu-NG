#include "IMEManager.h"

#include <imm.h>

#include <algorithm>

#include "UtfUtils.h"
#include "bin/menus/Settings.h"

#define IME_LOG(...)            \
	do {                       \
		if (Settings::ime_debug_log) { \
			INFO(__VA_ARGS__);  \
		}                      \
	} while (false)

namespace
{
	std::wstring GetCompositionString(HIMC imeContext, LPARAM compositionFlags, DWORD flagToRead)
	{
		if ((compositionFlags & flagToRead) == 0 || imeContext == nullptr) {
			return {};
		}

		const LONG byteCount = ImmGetCompositionStringW(imeContext, flagToRead, nullptr, 0);
		if (byteCount <= 0) {
			return {};
		}

		std::wstring text(static_cast<std::size_t>(byteCount / sizeof(wchar_t)), L'\0');
		const LONG copied = ImmGetCompositionStringW(imeContext, flagToRead, text.data(), byteCount);
		if (copied <= 0) {
			return {};
		}

		return text;
	}
}

namespace IME
{
	Manager& Manager::Get()
	{
		static Manager instance;
		return instance;
	}

	void Manager::PendingCommit::ClearAll()
	{
		widgetId = 0;
		utf8.clear();
		suppressionSequence.clear();
		ansiFallbackCount = 0;
	}

	void Manager::PendingCommit::ClearInsert()
	{
		utf8.clear();
		if (!HasSuppressionPending()) {
			widgetId = 0;
		}
	}

	void Manager::PendingCommit::ClearSuppression()
	{
		suppressionSequence.clear();
		ansiFallbackCount = 0;
		if (!HasInsertPending()) {
			widgetId = 0;
		}
	}

	bool Manager::PendingCommit::HasInsertPending() const
	{
		return widgetId != 0 && !utf8.empty();
	}

	bool Manager::PendingCommit::HasSuppressionPending() const
	{
		return !suppressionSequence.empty();
	}

	void Manager::CandidateState::Clear()
	{
		items.clear();
		selectedIndex = 0;
		pageStart = 0;
		pageSize = 0;
	}

	bool Manager::CandidateState::HasVisibleItems() const
	{
		return !items.empty();
	}

	void Manager::Initialize(HWND hwnd)
	{
		hwnd_ = hwnd;
	}

	void Manager::SetMenuEnabled(bool enabled)
	{
		menuEnabled_ = enabled;
		if (!menuEnabled_) {
			Reset("menu closed");
		}
	}

	void Manager::BeginFrame(bool menuEnabled)
	{
		menuEnabled_ = menuEnabled;
		frameNumber_ = ImGui::GetFrameCount();

		if (!menuEnabled_ || !IsImeEnabled()) {
			Reset(!menuEnabled_ ? "menu hidden" : "feature disabled");
		}
	}

	void Manager::EndFrame()
	{
		if (!menuEnabled_ || !IsImeEnabled()) {
			return;
		}

		if (activeContext_.IsValid() && !activeContext_.IsFresh(frameNumber_)) {
			Reset("text target lost");
			return;
		}

		DrawCompositionOverlay();
	}

	void Manager::Reset(std::string_view reason)
	{
		if (compositionActive_) {
			CancelComposition();
		}

		if (!reason.empty() &&
		    (activeContext_.IsValid() || compositionActive_ || pendingCommit_.HasInsertPending() || pendingCommit_.HasSuppressionPending())) {
			IME_LOG("IME: reset ({})", reason);
		}

		reopenImeOnNextTarget_ = false;
		imeSessionActive_ = false;
		activeContext_.Clear();
		pendingCommit_.ClearAll();
		candidateState_.Clear();
		compositionWide_.clear();
		compositionUtf8_.clear();
		compositionActive_ = false;
		candidateOpen_ = false;
	}

	bool Manager::HandleWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result)
	{
		if (hwnd != nullptr) {
			hwnd_ = hwnd;
		}

		switch (message) {
		case WM_KILLFOCUS:
			Reset("focus lost");
			return false;
		case WM_ACTIVATEAPP:
			if (!wParam) {
				Reset("application deactivated");
			}
			return false;
		case WM_IME_STARTCOMPOSITION:
			if (!HasImeMessageInterest()) {
				return false;
			}
			compositionActive_ = true;
			candidateOpen_ = false;
			UpdateCompositionWindow();
			IME_LOG("IME: composition start target={} label='{}'", activeContext_.widgetId, activeContext_.label);
			return false;
		case WM_IME_COMPOSITION:
			if (!HasImeMessageInterest()) {
				return false;
			}
			compositionActive_ = true;
			UpdateCompositionWindow();
			ProcessCompositionMessage(hwnd_, lParam);
			return false;
		case WM_IME_ENDCOMPOSITION:
			if (!HasImeMessageInterest() && !compositionActive_) {
				return false;
			}
			compositionActive_ = false;
			candidateOpen_ = false;
			compositionWide_.clear();
			compositionUtf8_.clear();
			IME_LOG("IME: composition end");
			return false;
		case WM_IME_NOTIFY:
			if (!HasImeMessageInterest() && !compositionActive_) {
				return false;
			}
			switch (wParam) {
			case IMN_OPENCANDIDATE:
			case IMN_CHANGECANDIDATE:
			case IMN_SETCANDIDATEPOS:
			{
				const bool wasCandidateOpen = candidateOpen_;
				candidateOpen_ = true;
				if (hwnd_ != nullptr) {
					HIMC imeContext = ImmGetContext(hwnd_);
					if (imeContext != nullptr) {
						UpdateCandidateList(imeContext);
						ImmReleaseContext(hwnd_, imeContext);
					}
				}
				if (!wasCandidateOpen) {
					IME_LOG("IME: candidate open/update notify={:#x}", static_cast<unsigned>(wParam));
				}
				break;
			}
			case IMN_CLOSECANDIDATE:
				candidateOpen_ = false;
				candidateState_.Clear();
				IME_LOG("IME: candidate close");
				break;
			default:
				break;
			}
			return false;
		case WM_IME_SETCONTEXT:
			if (HasImeMessageInterest()) {
				UpdateCompositionWindow();
			}
			return false;
		case WM_CHAR:
		case WM_IME_CHAR:
			if (HasImeMessageInterest() &&
			    (compositionActive_ || pendingCommit_.HasSuppressionPending() || pendingCommit_.HasInsertPending())) {
				result = 0;
				return true;
			}
			return false;
		case WM_UNICHAR:
			if (!HasImeMessageInterest()) {
				return false;
			}
			if (wParam == UNICODE_NOCHAR) {
				result = TRUE;
				return true;
			}
			if (compositionActive_ || pendingCommit_.HasSuppressionPending() || pendingCommit_.HasInsertPending()) {
				result = 0;
				return true;
			}
			return false;
		default:
			return false;
		}
	}

	void Manager::RegisterTextTarget(ImGuiID widgetId, std::string_view label, const ImRect& rect, bool active, bool focused, bool multiline)
	{
		if (!menuEnabled_ || !IsImeEnabled() || widgetId == 0) {
			return;
		}
		imeSessionActive_ = true;

		if (activeContext_.widgetId != widgetId) {
			IME_LOG("IME: target registered id={} label='{}'", widgetId, label);
		}

		activeContext_.widgetId = widgetId;
		activeContext_.label.assign(label.begin(), label.end());
		activeContext_.rect = rect;
		activeContext_.active = active;
		activeContext_.focused = focused;
		activeContext_.multiline = multiline;
		activeContext_.frameSeen = frameNumber_;

		UpdateCompositionWindow();
	}

	bool Manager::ApplyPendingCommit(ImGuiInputTextCallbackData* data, ImGuiID widgetId)
	{
		if (!pendingCommit_.HasInsertPending() || pendingCommit_.widgetId != widgetId || data == nullptr) {
			return false;
		}

		int insertPos = data->CursorPos;
		if (data->HasSelection()) {
			const int selectionStart = (std::min)(data->SelectionStart, data->SelectionEnd);
			const int selectionEnd = (std::max)(data->SelectionStart, data->SelectionEnd);
			data->DeleteChars(selectionStart, selectionEnd - selectionStart);
			insertPos = selectionStart;
		}

		data->InsertChars(insertPos, pendingCommit_.utf8.c_str());
		data->BufDirty = true;
		IME_LOG("IME: commit applied bytes={} target={}", pendingCommit_.utf8.size(), widgetId);
		pendingCommit_.ClearInsert();
		return true;
	}

	bool Manager::ShouldSuppressInputCharacter(std::uint32_t codepoint)
	{
		if (!menuEnabled_ || !IsImeEnabled()) {
			return false;
		}

		if (ShouldPreemptInputCharacter(codepoint)) {
			IME_LOG("IME: preemptively suppressed composition key cp={:#x}", codepoint);
			return true;
		}

		if (compositionActive_ && activeContext_.IsValid()) {
			IME_LOG("IME: suppressed char during composition cp={:#x}", codepoint);
			return true;
		}

		if (!pendingCommit_.HasSuppressionPending()) {
			return false;
		}

		const char32_t expected = pendingCommit_.suppressionSequence.front();
		if (expected == static_cast<char32_t>(codepoint)) {
			pendingCommit_.suppressionSequence.erase(pendingCommit_.suppressionSequence.begin());
			IME_LOG("IME: suppressed duplicate committed char cp={:#x}", codepoint);
			if (!pendingCommit_.HasSuppressionPending()) {
				pendingCommit_.ClearSuppression();
			}
			return true;
		}

		if (pendingCommit_.ansiFallbackCount > 0 && codepoint == '?') {
			--pendingCommit_.ansiFallbackCount;
			pendingCommit_.suppressionSequence.erase(pendingCommit_.suppressionSequence.begin());
			IME_LOG("IME: suppressed ANSI fallback char for committed Unicode text");
			if (!pendingCommit_.HasSuppressionPending()) {
				pendingCommit_.ClearSuppression();
			}
			return true;
		}

		IME_LOG(
			"IME: suppression mismatch expected={:#x} actual={:#x}, clearing pending suppression",
			static_cast<std::uint32_t>(expected),
			codepoint);
		pendingCommit_.ClearSuppression();
		return false;
	}

	bool Manager::ShouldKeepTextTargetAlive(ImGuiID widgetId) const
	{
		if (!menuEnabled_ || !IsImeEnabled() || widgetId == 0) {
			return false;
		}

		if (activeContext_.widgetId != widgetId) {
			return false;
		}

		return compositionActive_ ||
		       candidateOpen_ ||
		       (pendingCommit_.widgetId == widgetId &&
		        (pendingCommit_.HasInsertPending() || pendingCommit_.HasSuppressionPending()));
	}

	bool Manager::IsImeEnabled() const
	{
		return Settings::enable_ime_support;
	}

	bool Manager::HasActiveTextTarget() const
	{
		return activeContext_.IsValid();
	}

	bool Manager::HasImeMessageInterest() const
	{
		return IsImeEnabled() &&
		       menuEnabled_ &&
		       (activeContext_.IsValid() || compositionActive_ || pendingCommit_.HasInsertPending() || pendingCommit_.HasSuppressionPending());
	}

	bool Manager::IsNativeImeModeActive() const
	{
		if (!IsImeEnabled() || !menuEnabled_ || hwnd_ == nullptr || !activeContext_.IsValid()) {
			return false;
		}

		HIMC imeContext = ImmGetContext(hwnd_);
		if (imeContext == nullptr) {
			return false;
		}

		const BOOL open = ImmGetOpenStatus(imeContext);
		DWORD conversion = 0;
		DWORD sentence = 0;
		const BOOL hasConversionState = ImmGetConversionStatus(imeContext, &conversion, &sentence);
		ImmReleaseContext(hwnd_, imeContext);

		if (!open || !hasConversionState) {
			return false;
		}

		return (conversion & IME_CMODE_NATIVE) != 0;
	}

	bool Manager::ShouldPreemptInputCharacter(std::uint32_t codepoint) const
	{
		if (!IsNativeImeModeActive()) {
			return false;
		}

		return (codepoint >= 'a' && codepoint <= 'z') ||
		       (codepoint >= 'A' && codepoint <= 'Z');
	}

	void Manager::UpdateCandidateList(HIMC imeContext)
	{
		const auto previousItems = candidateState_.items;
		const auto previousSelection = candidateState_.selectedIndex;
		const auto previousPageStart = candidateState_.pageStart;
		const auto previousPageSize = candidateState_.pageSize;

		candidateState_.Clear();
		if (imeContext == nullptr) {
			return;
		}

		const DWORD bufferBytes = ImmGetCandidateListW(imeContext, 0, nullptr, 0);
		if (bufferBytes == 0) {
			return;
		}

		std::vector<BYTE> buffer(bufferBytes, 0);
		auto* candidateList = reinterpret_cast<LPCANDIDATELIST>(buffer.data());
		if (ImmGetCandidateListW(imeContext, 0, candidateList, bufferBytes) == 0 || candidateList->dwCount == 0) {
			return;
		}

		const DWORD pageStart = candidateList->dwPageStart;
		const DWORD pageSize = candidateList->dwPageSize != 0 ? candidateList->dwPageSize : candidateList->dwCount;
		const DWORD pageEnd = (std::min)(candidateList->dwCount, pageStart + pageSize);
		const char* base = reinterpret_cast<const char*>(candidateList);

		for (DWORD index = pageStart; index < pageEnd; ++index) {
			const auto* candidateText = reinterpret_cast<const wchar_t*>(base + candidateList->dwOffset[index]);
			candidateState_.items.push_back(UtfUtils::WideToUtf8(candidateText != nullptr ? std::wstring_view(candidateText) : std::wstring_view()));
		}

		candidateState_.pageStart = pageStart;
		candidateState_.pageSize = pageSize;
		if (candidateList->dwSelection >= pageStart && !candidateState_.items.empty()) {
			candidateState_.selectedIndex =
				(std::min<std::size_t>)(candidateState_.items.size() - 1, candidateList->dwSelection - pageStart);
		}

		if (candidateState_.items != previousItems ||
		    candidateState_.selectedIndex != previousSelection ||
		    candidateState_.pageStart != previousPageStart ||
		    candidateState_.pageSize != previousPageSize) {
			IME_LOG(
				"IME: candidate list updated total={} pageStart={} pageSize={} visible={} selection={}",
				candidateList->dwCount,
				candidateState_.pageStart,
				candidateState_.pageSize,
				candidateState_.items.size(),
				candidateState_.selectedIndex);
		}
	}

	bool Manager::ProcessCompositionMessage(HWND hwnd, LPARAM compositionFlags)
	{
		if (hwnd == nullptr) {
			return false;
		}

		HIMC imeContext = ImmGetContext(hwnd);
		if (imeContext == nullptr) {
			IME_LOG("IME: ImmGetContext failed for composition message");
			return false;
		}

		const std::wstring resultText = GetCompositionString(imeContext, compositionFlags, GCS_RESULTSTR);
		const std::wstring compositionText = GetCompositionString(imeContext, compositionFlags, GCS_COMPSTR);

		if (!resultText.empty()) {
			QueueCommittedText(resultText);
			compositionWide_.clear();
			compositionUtf8_.clear();
		}

		if (!compositionText.empty()) {
			compositionWide_ = compositionText;
			compositionUtf8_ = UtfUtils::WideToUtf8(compositionWide_);
			IME_LOG("IME: composition update chars={}", compositionWide_.size());
		} else if ((compositionFlags & GCS_COMPSTR) != 0) {
			compositionWide_.clear();
			compositionUtf8_.clear();
		}

		if (candidateOpen_) {
			UpdateCandidateList(imeContext);
		}

		ImmReleaseContext(hwnd, imeContext);
		return true;
	}

	void Manager::QueueCommittedText(std::wstring_view text)
	{
		if (!activeContext_.IsValid()) {
			IME_LOG("IME: commit dropped because there is no active text target");
			return;
		}

		const std::string utf8 = UtfUtils::WideToUtf8(text);
		if (utf8.empty()) {
			IME_LOG("IME: commit dropped because UTF-8 conversion failed");
			return;
		}

		pendingCommit_.widgetId = activeContext_.widgetId;
		pendingCommit_.utf8 = utf8;
		pendingCommit_.suppressionSequence = UtfUtils::WideToCodepoints(text);
		pendingCommit_.ansiFallbackCount = std::count_if(
			pendingCommit_.suppressionSequence.begin(),
			pendingCommit_.suppressionSequence.end(),
			[](char32_t codepoint) {
				return codepoint > 0x7F;
			});
		IME_LOG(
			"IME: queued commit target={} codepoints={} bytes={} ansiFallbacks={}",
			activeContext_.widgetId,
			pendingCommit_.suppressionSequence.size(),
			pendingCommit_.utf8.size(),
			pendingCommit_.ansiFallbackCount);
	}

	void Manager::UpdateCompositionWindow()
	{
		if (!menuEnabled_ || !IsImeEnabled() || hwnd_ == nullptr || !activeContext_.IsValid()) {
			return;
		}

		HIMC imeContext = ImmGetContext(hwnd_);
		if (imeContext == nullptr) {
			return;
		}

		const ImVec2 anchor = activeContext_.GetOverlayAnchor();
		const POINT point = {
			static_cast<LONG>(anchor.x),
			static_cast<LONG>(anchor.y)
		};

		COMPOSITIONFORM compositionForm{};
		compositionForm.dwStyle = CFS_FORCE_POSITION;
		compositionForm.ptCurrentPos = point;
		ImmSetCompositionWindow(imeContext, &compositionForm);

		CANDIDATEFORM candidateForm{};
		candidateForm.dwIndex = 0;
		candidateForm.dwStyle = CFS_EXCLUDE;
		candidateForm.ptCurrentPos = point;
		candidateForm.rcArea = RECT{
			static_cast<LONG>(activeContext_.rect.Min.x),
			static_cast<LONG>(activeContext_.rect.Min.y),
			static_cast<LONG>(activeContext_.rect.Max.x),
			static_cast<LONG>(activeContext_.rect.Max.y)
		};
		ImmSetCandidateWindow(imeContext, &candidateForm);

		ImmReleaseContext(hwnd_, imeContext);
	}

	void Manager::DrawCompositionOverlay()
	{
		if (!Settings::show_ime_composition_overlay || !activeContext_.IsFresh(frameNumber_)) {
			return;
		}

		std::vector<std::pair<std::string, bool>> lines;
		if (!compositionUtf8_.empty()) {
			std::string line = compositionUtf8_;
			if (candidateOpen_) {
				line += " [cand]";
			}
			lines.emplace_back(std::move(line), false);
		}

		for (std::size_t i = 0; i < candidateState_.items.size(); ++i) {
			std::string line = std::to_string(i + 1);
			line += ". ";
			line += candidateState_.items[i];
			lines.emplace_back(std::move(line), i == candidateState_.selectedIndex);
		}

		if (lines.empty()) {
			return;
		}

		ImGuiViewport* viewport = ImGui::GetMainViewport();
		if (viewport == nullptr) {
			return;
		}

		ImVec2 pos = activeContext_.GetOverlayAnchor();
		const ImVec2 padding(8.0f, 5.0f);
		float maxWidth = 0.0f;
		float totalHeight = padding.y * 2.0f;
		for (const auto& line : lines) {
			const ImVec2 textSize = ImGui::CalcTextSize(line.first.c_str());
			maxWidth = (std::max)(maxWidth, textSize.x);
			totalHeight += textSize.y;
			if (&line != &lines.back()) {
				totalHeight += 3.0f;
			}
		}

		const ImVec2 maxPos = ImVec2(
			viewport->Pos.x + viewport->Size.x - maxWidth - padding.x * 2.0f - 8.0f,
			viewport->Pos.y + viewport->Size.y - totalHeight - 8.0f);
		pos.x = (std::clamp)(pos.x, viewport->Pos.x + 8.0f, maxPos.x);
		pos.y = (std::clamp)(pos.y, viewport->Pos.y + 8.0f, maxPos.y);

		const ImVec2 rectMax(pos.x + maxWidth + padding.x * 2.0f, pos.y + totalHeight);
		ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);
		drawList->AddRectFilled(pos, rectMax, IM_COL32(20, 20, 20, 230), 5.0f);
		drawList->AddRect(pos, rectMax, IM_COL32(255, 200, 110, 255), 5.0f);

		ImVec2 textPos(pos.x + padding.x, pos.y + padding.y);
		for (const auto& line : lines) {
			const ImU32 textColor = line.second ? IM_COL32(255, 220, 120, 255) : IM_COL32(255, 255, 255, 255);
			drawList->AddText(textPos, textColor, line.first.c_str());
			textPos.y += ImGui::CalcTextSize(line.first.c_str()).y + 3.0f;
		}
	}

	void Manager::CancelComposition()
	{
		if (hwnd_ == nullptr) {
			return;
		}

		HIMC imeContext = ImmGetContext(hwnd_);
		if (imeContext == nullptr) {
			return;
		}

		ImmNotifyIME(imeContext, NI_CLOSECANDIDATE, 0, 0);
		ImmNotifyIME(imeContext, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
		ImmReleaseContext(hwnd_, imeContext);
	}
}
