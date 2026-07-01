#pragma once

#include <Windows.h>

#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "IMEContext.h"

namespace IME
{
	class Manager
	{
	public:
		static Manager& Get();

		void Initialize(HWND hwnd);
		void SetMenuEnabled(bool enabled);
		void BeginFrame(bool menuEnabled);
		void EndFrame();
		void Reset(std::string_view reason = {});

		bool HandleWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result);
		void RegisterTextTarget(ImGuiID widgetId, std::string_view label, const ImRect& rect, bool active, bool focused, bool multiline);
		bool ApplyPendingCommit(ImGuiInputTextCallbackData* data, ImGuiID widgetId);
		bool ShouldSuppressInputCharacter(std::uint32_t codepoint);
		bool ShouldKeepTextTargetAlive(ImGuiID widgetId) const;

		bool IsImeEnabled() const;
		bool HasActiveTextTarget() const;

	private:
		struct PendingCommit
		{
			ImGuiID widgetId{ 0 };
			std::string utf8;
			std::u32string suppressionSequence;
			std::size_t ansiFallbackCount{ 0 };

			void ClearAll();
			void ClearInsert();
			void ClearSuppression();
			bool HasInsertPending() const;
			bool HasSuppressionPending() const;
		};

		struct CandidateState
		{
			std::vector<std::string> items;
			std::size_t selectedIndex{ 0 };
			std::size_t pageStart{ 0 };
			std::size_t pageSize{ 0 };

			void Clear();
			bool HasVisibleItems() const;
		};

		Manager() = default;

		bool HasImeMessageInterest() const;
		bool IsNativeImeModeActive() const;
		bool ShouldPreemptInputCharacter(std::uint32_t codepoint) const;
		void UpdateCandidateList(HIMC imeContext);
		bool ProcessCompositionMessage(HWND hwnd, LPARAM compositionFlags);
		void QueueCommittedText(std::wstring_view text);
		void UpdateCompositionWindow();
		void DrawCompositionOverlay();
		void CancelComposition();

		HWND hwnd_{ nullptr };
		bool menuEnabled_{ false };
		bool compositionActive_{ false };
		bool candidateOpen_{ false };
		int frameNumber_{ -1 };
		bool reopenImeOnNextTarget_{ false };
		bool imeSessionActive_{ false };

		Context activeContext_;
		PendingCommit pendingCommit_;
		CandidateState candidateState_;
		std::wstring compositionWide_;
		std::string compositionUtf8_;
	};
}
