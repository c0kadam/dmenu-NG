#pragma once

#include <cstddef>
#include <cstdint>

#include "menus/ModSettings.h"

struct ID3D11Device;
struct ID3D11DeviceContext;

class HintMediaManager
{
public:
	static HintMediaManager& Get();

	void OnD3DReady(ID3D11Device* a_device, ID3D11DeviceContext* a_context);
	void Tick(float a_deltaSeconds);

	void SetEnabled(bool a_enabled);
	void SetCacheBudgetMB(std::size_t a_cacheBudgetMB);
	void SetMaxDecodePixels(std::uint64_t a_maxPixels);
	void SetLogLevel(int a_logLevel);
	void SetPreviewScale(float a_previewScale);
	void SetWebmMaxBufferedFrames(std::size_t a_frames);
	void SetWebmMaxClipSeconds(std::uint32_t a_seconds);
	void SetWebmDecodeBudgetMs(std::uint32_t a_ms);
	void SetWebmDecodeMaxFramesPerTick(std::size_t a_frames);

	void TryPreload(const ModSettings::entry_base& a_entry);
	bool DrawHintTooltip(
		const ModSettings::entry_base& a_entry,
		const char* a_descText,
		bool a_hoveredControl,
		bool a_hoveredNote,
		bool a_forceShow = false,
		const ImVec2* a_forcedPos = nullptr);
};
