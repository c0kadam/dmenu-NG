#include "PCH.h"

#include "HintMedia.h"

#include <imgui.h>

#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <objbase.h>
#include <propvarutil.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <vector>

#ifdef min
#	undef min
#endif
#ifdef max
#	undef max
#endif

#if __has_include(<webp/decode.h>) && __has_include(<webp/demux.h>)
#	include <webp/decode.h>
#	include <webp/demux.h>
#	define DMENU_HAS_WEBP_HEADERS 1
#else
#	define DMENU_HAS_WEBP_HEADERS 0
#endif

namespace
{
	namespace fs = std::filesystem;
	using Clock = std::chrono::steady_clock;
	using Microsoft::WRL::ComPtr;

	constexpr std::size_t ONE_MEGABYTE = 1024 * 1024;
	constexpr std::uint32_t DEFAULT_FRAME_MS = 100;
	constexpr std::uint32_t MIN_FRAME_MS = 10;

	struct MediaFrame
	{
		std::vector<std::uint8_t> rgba;
		std::uint32_t durationMs = DEFAULT_FRAME_MS;
	};

	struct PlaybackState
	{
		std::size_t frameIndex = 0;
		float accumulatedMs = 0.0f;
		std::uint64_t lastUsedTick = 0;
	};

	struct MediaAsset
	{
		ModSettings::entry_base::HintMediaConfig config;
		std::string key;

		std::vector<MediaFrame> frames;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::size_t memoryBytes = 0;

		bool ready = false;
		bool failed = false;
		std::uint64_t lastUsedTick = 0;
		std::uint64_t lastActiveHousekeepingTick = 0;
		int uploadedFrame = -1;

		Clock::time_point retryAt = Clock::time_point::min();
		Clock::time_point nextValidationAt = Clock::time_point::min();

		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11ShaderResourceView> srv;

		struct WebmStreamState
		{
			ComPtr<IMFSourceReader> reader;
			std::uint32_t sourceWidth = 0;
			std::uint32_t sourceHeight = 0;
			std::uint32_t fallbackFrameMs = DEFAULT_FRAME_MS;
			std::uint32_t decodedDurationMs = 0;
			LONGLONG previousTimestamp = -1;
			bool done = true;
			bool truncatedByBudget = false;
			bool truncatedByLimit = false;
		} webm;
	};

#if DMENU_HAS_WEBP_HEADERS
	struct WebpRuntimeApi
	{
		bool attemptedLoad = false;
		bool loaded = false;
		HMODULE module = nullptr;

		using FnOptionsInitInternal = int (*)(WebPAnimDecoderOptions*, int);
		using FnDecoderNewInternal = WebPAnimDecoder* (*)(const WebPData*, const WebPAnimDecoderOptions*, int);
		using FnGetInfo = int (*)(const WebPAnimDecoder*, WebPAnimInfo*);
		using FnHasMoreFrames = int (*)(const WebPAnimDecoder*);
		using FnGetNext = int (*)(WebPAnimDecoder*, std::uint8_t**, int*);
		using FnDelete = void (*)(WebPAnimDecoder*);

		FnOptionsInitInternal optionsInitInternal = nullptr;
		FnDecoderNewInternal decoderNewInternal = nullptr;
		FnGetInfo getInfo = nullptr;
		FnHasMoreFrames hasMoreFrames = nullptr;
		FnGetNext getNext = nullptr;
		FnDelete destroy = nullptr;
	};
#endif

	struct RuntimeState
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;

		bool enabled = true;
		std::size_t cacheBudgetBytes = 128 * ONE_MEGABYTE;
		std::uint64_t maxDecodePixels = 2048ull * 2048ull;
		int logLevel = 0;
		float previewScale = 1.0f;
		std::size_t webmMaxBufferedFrames = 120;
		std::uint32_t webmMaxClipMs = 30000;
		std::uint32_t webmDecodeBudgetMs = 2;
		std::size_t webmDecodeMaxFramesPerTick = 1;

		std::size_t usedBytes = 0;
		std::uint64_t usageTick = 0;
		std::uint64_t housekeepingTick = 0;

		bool comInitAttempted = false;
		bool comReady = false;
		bool wicInitAttempted = false;
		ComPtr<IWICImagingFactory> wicFactory;
		bool mfInitAttempted = false;
		bool mfReady = false;

		std::unordered_map<std::string, MediaAsset> assets;
		std::unordered_map<std::string, PlaybackState> playbacks;

#if DMENU_HAS_WEBP_HEADERS
		WebpRuntimeApi webp;
#endif
	};

	RuntimeState& State()
	{
		static RuntimeState state;
		return state;
	}

	std::string ToLowerASCII(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return value;
	}

	std::string GetMediaKey(const ModSettings::entry_base::HintMediaConfig& a_config)
	{
		if (!a_config.cacheKey.empty()) {
			return a_config.cacheKey;
		}
		if (!a_config.resolved_path.empty()) {
			return a_config.resolved_path;
		}
		return a_config.path;
	}

	bool NeedsAssetRebuild(
		const ModSettings::entry_base::HintMediaConfig& a_previous,
		const ModSettings::entry_base::HintMediaConfig& a_next)
	{
		// These fields affect decode dimensions/content and require rebuilding cached frames/texture.
		return a_previous.type != a_next.type ||
		       a_previous.path != a_next.path ||
		       a_previous.resolved_path != a_next.resolved_path ||
		       a_previous.fps != a_next.fps ||
		       a_previous.maxW != a_next.maxW ||
		       a_previous.maxH != a_next.maxH;
	}

	fs::path GetConfigPath(const ModSettings::entry_base::HintMediaConfig& a_config)
	{
		if (!a_config.resolved_path.empty()) {
			return fs::path(a_config.resolved_path);
		}
		return fs::path(a_config.path);
	}

	bool PathExists(const fs::path& a_path)
	{
		std::error_code ec;
		return fs::exists(a_path, ec) && !ec;
	}

	bool IsDirectory(const fs::path& a_path)
	{
		std::error_code ec;
		return fs::is_directory(a_path, ec) && !ec;
	}

	bool IsSupportedFlipbookFrame(const fs::path& a_path)
	{
		const std::string ext = ToLowerASCII(a_path.extension().string());
		return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tif" || ext == ".tiff";
	}

	std::vector<std::uint8_t> BlitToCanvas(
		const std::vector<std::uint8_t>& a_srcRGBA,
		std::uint32_t a_srcW,
		std::uint32_t a_srcH,
		std::uint32_t a_dstW,
		std::uint32_t a_dstH)
	{
		std::vector<std::uint8_t> dst(static_cast<std::size_t>(a_dstW) * a_dstH * 4u, 0);
		const std::uint32_t copyW = (std::min)(a_srcW, a_dstW);
		const std::uint32_t copyH = (std::min)(a_srcH, a_dstH);
		const std::size_t dstPitch = static_cast<std::size_t>(a_dstW) * 4u;
		const std::size_t srcPitch = static_cast<std::size_t>(a_srcW) * 4u;
		const std::size_t rowBytes = static_cast<std::size_t>(copyW) * 4u;
		for (std::uint32_t y = 0; y < copyH; ++y) {
			std::memcpy(dst.data() + y * dstPitch, a_srcRGBA.data() + y * srcPitch, rowBytes);
		}
		return dst;
	}

	std::vector<std::uint8_t> ResizeRGBA_Nearest(
		const std::vector<std::uint8_t>& a_srcRGBA,
		std::uint32_t a_srcW,
		std::uint32_t a_srcH,
		std::uint32_t a_dstW,
		std::uint32_t a_dstH)
	{
		if (a_srcW == 0 || a_srcH == 0 || a_dstW == 0 || a_dstH == 0) {
			return {};
		}
		if (a_srcW == a_dstW && a_srcH == a_dstH) {
			return a_srcRGBA;
		}

		std::vector<std::uint8_t> dst(static_cast<std::size_t>(a_dstW) * a_dstH * 4u);
		for (std::uint32_t y = 0; y < a_dstH; ++y) {
			const std::uint32_t srcY = static_cast<std::uint32_t>((static_cast<std::uint64_t>(y) * a_srcH) / a_dstH);
			for (std::uint32_t x = 0; x < a_dstW; ++x) {
				const std::uint32_t srcX = static_cast<std::uint32_t>((static_cast<std::uint64_t>(x) * a_srcW) / a_dstW);
				const std::size_t srcIdx = (static_cast<std::size_t>(srcY) * a_srcW + srcX) * 4u;
				const std::size_t dstIdx = (static_cast<std::size_t>(y) * a_dstW + x) * 4u;
				dst[dstIdx + 0] = a_srcRGBA[srcIdx + 0];
				dst[dstIdx + 1] = a_srcRGBA[srcIdx + 1];
				dst[dstIdx + 2] = a_srcRGBA[srcIdx + 2];
				dst[dstIdx + 3] = a_srcRGBA[srcIdx + 3];
			}
		}

		return dst;
	}

	bool IsDecodeSizeAllowed(std::uint32_t a_width, std::uint32_t a_height)
	{
		const auto& state = State();
		if (a_width == 0 || a_height == 0) {
			return false;
		}
		const std::uint64_t pixels = static_cast<std::uint64_t>(a_width) * static_cast<std::uint64_t>(a_height);
		return pixels <= state.maxDecodePixels;
	}

	void ReleaseAssetStorage(MediaAsset& a_asset)
	{
		auto& state = State();
		if (a_asset.ready && a_asset.memoryBytes <= state.usedBytes) {
			state.usedBytes -= a_asset.memoryBytes;
		}
		a_asset.frames.clear();
		a_asset.frames.shrink_to_fit();
		a_asset.texture.Reset();
		a_asset.srv.Reset();
		a_asset.width = 0;
		a_asset.height = 0;
		a_asset.memoryBytes = 0;
		a_asset.ready = false;
		a_asset.uploadedFrame = -1;
		a_asset.webm.reader.Reset();
		a_asset.webm.sourceWidth = 0;
		a_asset.webm.sourceHeight = 0;
		a_asset.webm.fallbackFrameMs = DEFAULT_FRAME_MS;
		a_asset.webm.decodedDurationMs = 0;
		a_asset.webm.previousTimestamp = -1;
		a_asset.webm.done = true;
		a_asset.webm.truncatedByBudget = false;
		a_asset.webm.truncatedByLimit = false;
	}

	void MarkAssetFailed(MediaAsset& a_asset, const std::string& a_reason)
	{
		ReleaseAssetStorage(a_asset);
		a_asset.failed = true;
		a_asset.retryAt = Clock::now() + std::chrono::seconds(2);
		if (State().logLevel >= 2) {
			INFO("Hint media '{}' unavailable: {}", a_asset.key, a_reason);
		}
	}

	void EvictIfNeeded(const std::string& a_protectedKey)
	{
		auto& state = State();
		if (state.usedBytes <= state.cacheBudgetBytes) {
			return;
		}

		while (state.usedBytes > state.cacheBudgetBytes) {
			auto lruIt = state.assets.end();
			for (auto it = state.assets.begin(); it != state.assets.end(); ++it) {
				if (it->first == a_protectedKey || !it->second.ready) {
					continue;
				}
				if (lruIt == state.assets.end() || it->second.lastUsedTick < lruIt->second.lastUsedTick) {
					lruIt = it;
				}
			}

			if (lruIt == state.assets.end()) {
				break;
			}

			if (state.logLevel >= 2) {
				INFO("Evicting hint media '{}' from cache", lruIt->first);
			}

			ReleaseAssetStorage(lruIt->second);
			state.playbacks.erase(lruIt->first);
			state.assets.erase(lruIt);
		}
	}

	bool EnsureCOMInitialized()
	{
		auto& state = State();
		if (state.comInitAttempted) {
			return state.comReady;
		}
		state.comInitAttempted = true;

		const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
			ERROR("Hint media: CoInitializeEx failed (hr={:08X})", static_cast<unsigned>(hr));
			state.comReady = false;
			return false;
		}
		state.comReady = true;
		return true;
	}

	bool EnsureMediaFoundation()
	{
		auto& state = State();
		if (state.mfInitAttempted) {
			return state.mfReady;
		}
		state.mfInitAttempted = true;

		if (!EnsureCOMInitialized()) {
			state.mfReady = false;
			return false;
		}

		const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
		if (FAILED(hr)) {
			if (state.logLevel >= 1) {
				INFO("Hint media: MFStartup failed (hr={:08X})", static_cast<unsigned>(hr));
			}
			state.mfReady = false;
			return false;
		}

		state.mfReady = true;
		return true;
	}

	bool EnsureWICFactory()
	{
		auto& state = State();
		if (!EnsureCOMInitialized()) {
			return false;
		}

		if (state.wicFactory) {
			return true;
		}
		if (state.wicInitAttempted) {
			return false;
		}
		state.wicInitAttempted = true;

		const HRESULT hr = CoCreateInstance(
			CLSID_WICImagingFactory,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(state.wicFactory.GetAddressOf()));
		if (FAILED(hr) || !state.wicFactory) {
			ERROR("Hint media: failed to create WIC factory (hr={:08X})", static_cast<unsigned>(hr));
			return false;
		}
		return true;
	}

	bool DecodeWICSourceRGBA(IWICBitmapSource* a_source, std::uint32_t& a_width, std::uint32_t& a_height, std::vector<std::uint8_t>& a_outRGBA)
	{
		if (!a_source || !EnsureWICFactory()) {
			return false;
		}

		UINT width = 0;
		UINT height = 0;
		if (FAILED(a_source->GetSize(&width, &height)) || width == 0 || height == 0) {
			return false;
		}
		if (!IsDecodeSizeAllowed(width, height)) {
			if (State().logLevel >= 1) {
				INFO("Hint media frame skipped: {}x{} exceeds max decode pixels", width, height);
			}
			return false;
		}

		ComPtr<IWICFormatConverter> converter;
		if (FAILED(State().wicFactory->CreateFormatConverter(converter.GetAddressOf())) || !converter) {
			return false;
		}
		const HRESULT initHr = converter->Initialize(
			a_source,
			GUID_WICPixelFormat32bppRGBA,
			WICBitmapDitherTypeNone,
			nullptr,
			0.0,
			WICBitmapPaletteTypeCustom);
		if (FAILED(initHr)) {
			return false;
		}

		const std::size_t stride = static_cast<std::size_t>(width) * 4u;
		const std::size_t bufferSize = stride * static_cast<std::size_t>(height);
		if (bufferSize == 0) {
			return false;
		}

		a_outRGBA.resize(bufferSize);
		const HRESULT copyHr = converter->CopyPixels(
			nullptr,
			static_cast<UINT>(stride),
			static_cast<UINT>(bufferSize),
			a_outRGBA.data());
		if (FAILED(copyHr)) {
			a_outRGBA.clear();
			return false;
		}

		a_width = width;
		a_height = height;
		return true;
	}

	bool DecodeImageFileWIC(const fs::path& a_path, std::uint32_t& a_width, std::uint32_t& a_height, std::vector<std::uint8_t>& a_outRGBA)
	{
		if (!EnsureWICFactory()) {
			return false;
		}

		ComPtr<IWICBitmapDecoder> decoder;
		const HRESULT hr = State().wicFactory->CreateDecoderFromFilename(
			a_path.wstring().c_str(),
			nullptr,
			GENERIC_READ,
			WICDecodeMetadataCacheOnLoad,
			decoder.GetAddressOf());
		if (FAILED(hr) || !decoder) {
			return false;
		}

		ComPtr<IWICBitmapFrameDecode> frame;
		if (FAILED(decoder->GetFrame(0, frame.GetAddressOf())) || !frame) {
			return false;
		}

		return DecodeWICSourceRGBA(frame.Get(), a_width, a_height, a_outRGBA);
	}

	std::uint32_t ReadGifDelayMs(IWICBitmapFrameDecode* a_frame)
	{
		if (!a_frame) {
			return DEFAULT_FRAME_MS;
		}

		ComPtr<IWICMetadataQueryReader> metadata;
		if (FAILED(a_frame->GetMetadataQueryReader(metadata.GetAddressOf())) || !metadata) {
			return DEFAULT_FRAME_MS;
		}

		PROPVARIANT prop{};
		PropVariantInit(&prop);
		std::uint32_t delayMs = DEFAULT_FRAME_MS;
		const HRESULT hr = metadata->GetMetadataByName(L"/grctlext/Delay", &prop);
		if (SUCCEEDED(hr)) {
			std::uint32_t delayCs = 0;
			if (prop.vt == VT_UI2) {
				delayCs = static_cast<std::uint32_t>(prop.uiVal);
			} else if (prop.vt == VT_UI4) {
				delayCs = static_cast<std::uint32_t>(prop.ulVal);
			}
			if (delayCs == 0) {
				delayCs = DEFAULT_FRAME_MS / 10u;
			}
			delayMs = delayCs * 10u;
		}
		PropVariantClear(&prop);
		return (std::max)(MIN_FRAME_MS, delayMs);
	}

	std::uint32_t GetFrameDurationFromMediaType(IMFMediaType* a_mediaType)
	{
		if (!a_mediaType) {
			return DEFAULT_FRAME_MS;
		}

		UINT32 numerator = 0;
		UINT32 denominator = 0;
		if (SUCCEEDED(MFGetAttributeRatio(a_mediaType, MF_MT_FRAME_RATE, &numerator, &denominator)) && numerator != 0 && denominator != 0) {
			const std::uint64_t frameMs = (1000ull * static_cast<std::uint64_t>(denominator)) / static_cast<std::uint64_t>(numerator);
			return static_cast<std::uint32_t>((std::clamp)(frameMs, static_cast<std::uint64_t>(MIN_FRAME_MS), static_cast<std::uint64_t>(1000)));
		}
		return DEFAULT_FRAME_MS;
	}

	std::uint32_t GetSampleDurationMs(IMFSample* a_sample, std::uint32_t a_fallbackMs)
	{
		const std::uint32_t fallback = (std::max)(MIN_FRAME_MS, a_fallbackMs);
		if (!a_sample) {
			return fallback;
		}

		LONGLONG sampleDuration100ns = 0;
		if (FAILED(a_sample->GetSampleDuration(&sampleDuration100ns)) || sampleDuration100ns <= 0) {
			return fallback;
		}

		const std::uint64_t durationMs = static_cast<std::uint64_t>(sampleDuration100ns) / 10000ull;
		if (durationMs == 0) {
			return fallback;
		}
		return static_cast<std::uint32_t>((std::clamp)(durationMs, static_cast<std::uint64_t>(MIN_FRAME_MS), static_cast<std::uint64_t>(1000)));
	}

	void ConvertBGRAtoRGBA(const std::uint8_t* a_src, std::uint8_t* a_dst, std::uint32_t a_width)
	{
		for (std::uint32_t x = 0; x < a_width; ++x) {
			const std::size_t i = static_cast<std::size_t>(x) * 4u;
			a_dst[i + 0] = a_src[i + 2];
			a_dst[i + 1] = a_src[i + 1];
			a_dst[i + 2] = a_src[i + 0];
			// MF RGB32 often provides undefined/zero alpha; force opaque for tooltip preview.
			a_dst[i + 3] = 255;
		}
	}

	bool ExtractRGB32SampleToRGBA(IMFSample* a_sample, std::uint32_t a_width, std::uint32_t a_height, std::vector<std::uint8_t>& a_outRGBA)
	{
		if (!a_sample || a_width == 0 || a_height == 0) {
			return false;
		}

		const std::size_t rowBytes = static_cast<std::size_t>(a_width) * 4u;
		const std::size_t totalBytes = rowBytes * static_cast<std::size_t>(a_height);
		if (totalBytes == 0) {
			return false;
		}
		a_outRGBA.resize(totalBytes);

		ComPtr<IMFMediaBuffer> buffer;
		if (FAILED(a_sample->ConvertToContiguousBuffer(buffer.GetAddressOf())) || !buffer) {
			return false;
		}

		ComPtr<IMF2DBuffer> buffer2D;
		if (SUCCEEDED(buffer.As(&buffer2D)) && buffer2D) {
			BYTE* scanline0 = nullptr;
			LONG pitch = 0;
			if (SUCCEEDED(buffer2D->Lock2D(&scanline0, &pitch)) && scanline0 != nullptr) {
				const LONG absPitch = pitch >= 0 ? pitch : -pitch;
				if (absPitch < static_cast<LONG>(rowBytes)) {
					buffer2D->Unlock2D();
					a_outRGBA.clear();
					return false;
				}

				for (std::uint32_t y = 0; y < a_height; ++y) {
					const std::uint32_t srcY = pitch >= 0 ? y : (a_height - 1u - y);
					const BYTE* srcRow = scanline0 + static_cast<std::size_t>(srcY) * static_cast<std::size_t>(absPitch);
					std::uint8_t* dstRow = a_outRGBA.data() + static_cast<std::size_t>(y) * rowBytes;
					ConvertBGRAtoRGBA(srcRow, dstRow, a_width);
				}
				buffer2D->Unlock2D();
				return true;
			}
		}

		BYTE* data = nullptr;
		DWORD maxLen = 0;
		DWORD curLen = 0;
		if (FAILED(buffer->Lock(&data, &maxLen, &curLen)) || data == nullptr) {
			a_outRGBA.clear();
			return false;
		}
		const bool enoughBytes = curLen >= totalBytes;
		if (enoughBytes) {
			for (std::uint32_t y = 0; y < a_height; ++y) {
				const std::uint8_t* srcRow = data + static_cast<std::size_t>(y) * rowBytes;
				std::uint8_t* dstRow = a_outRGBA.data() + static_cast<std::size_t>(y) * rowBytes;
				ConvertBGRAtoRGBA(srcRow, dstRow, a_width);
			}
		}
		buffer->Unlock();
		if (!enoughBytes) {
			a_outRGBA.clear();
			return false;
		}
		return true;
	}

	void PushFrame(MediaAsset& a_asset, std::vector<std::uint8_t>&& a_rgba, std::uint32_t a_durationMs)
	{
		MediaFrame frame;
		frame.durationMs = (std::max)(MIN_FRAME_MS, a_durationMs);
		frame.rgba = std::move(a_rgba);
		a_asset.memoryBytes += frame.rgba.size();
		a_asset.frames.push_back(std::move(frame));
	}

	void TrimConsumedWebmFrames(MediaAsset& a_asset, std::size_t a_keepBufferedFrames = 180)
	{
		if (a_asset.config.type != ModSettings::entry_base::HintMediaConfig::Type::Webm || a_asset.frames.empty()) {
			return;
		}

		auto& state = State();
		auto playbackIt = state.playbacks.find(a_asset.key);
		if (playbackIt == state.playbacks.end()) {
			return;
		}

		PlaybackState& playback = playbackIt->second;
		if (playback.frameIndex == 0 || a_asset.frames.size() <= a_keepBufferedFrames) {
			return;
		}

		const std::size_t maxTrimForBuffer = a_asset.frames.size() > a_keepBufferedFrames ? (a_asset.frames.size() - a_keepBufferedFrames) : 0;
		const std::size_t trimCount = (std::min)(playback.frameIndex, maxTrimForBuffer);
		if (trimCount == 0) {
			return;
		}

		std::size_t freedBytes = 0;
		for (std::size_t i = 0; i < trimCount; ++i) {
			freedBytes += a_asset.frames[i].rgba.size();
		}

		a_asset.frames.erase(a_asset.frames.begin(), a_asset.frames.begin() + static_cast<std::ptrdiff_t>(trimCount));
		playback.frameIndex -= trimCount;

		if (a_asset.uploadedFrame >= 0) {
			const int uploaded = a_asset.uploadedFrame - static_cast<int>(trimCount);
			a_asset.uploadedFrame = uploaded >= 0 ? uploaded : -1;
		}

		if (freedBytes > 0) {
			if (a_asset.memoryBytes >= freedBytes) {
				a_asset.memoryBytes -= freedBytes;
			} else {
				a_asset.memoryBytes = 0;
			}
			if (a_asset.ready) {
				if (state.usedBytes >= freedBytes) {
					state.usedBytes -= freedBytes;
				} else {
					state.usedBytes = 0;
				}
			}
		}
	}

	std::size_t GetWebmBufferedAheadFrames(const MediaAsset& a_asset)
	{
		if (a_asset.config.type != ModSettings::entry_base::HintMediaConfig::Type::Webm || a_asset.frames.empty()) {
			return 0;
		}

		const auto& state = State();
		auto playbackIt = state.playbacks.find(a_asset.key);
		if (playbackIt == state.playbacks.end()) {
			return a_asset.frames.size();
		}

		const PlaybackState& playback = playbackIt->second;
		if (playback.frameIndex >= a_asset.frames.size()) {
			return 0;
		}
		return a_asset.frames.size() - playback.frameIndex;
	}

	bool DecodeFlipbook(const ModSettings::entry_base::HintMediaConfig& a_config, MediaAsset& a_asset)
	{
		const fs::path directory = GetConfigPath(a_config);
		if (!PathExists(directory) || !IsDirectory(directory)) {
			return false;
		}

		std::vector<fs::path> files;
		std::error_code ec;
		for (fs::directory_iterator it(directory, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
			const auto& p = it->path();
			if (!it->is_regular_file(ec) || ec) {
				continue;
			}
			if (IsSupportedFlipbookFrame(p)) {
				files.push_back(p);
			}
		}
		if (files.empty()) {
			return false;
		}

		std::sort(files.begin(), files.end(), [](const fs::path& a_lhs, const fs::path& a_rhs) {
			return a_lhs.filename().string() < a_rhs.filename().string();
		});

		const int fps = (std::max)(1, a_config.fps);
		const std::uint32_t durationMs = (std::max)(MIN_FRAME_MS, static_cast<std::uint32_t>(1000 / fps));

		for (const auto& framePath : files) {
			std::uint32_t width = 0;
			std::uint32_t height = 0;
			std::vector<std::uint8_t> rgba;
			if (!DecodeImageFileWIC(framePath, width, height, rgba)) {
				continue;
			}

			if (a_asset.width == 0 || a_asset.height == 0) {
				a_asset.width = width;
				a_asset.height = height;
			}

			if (width != a_asset.width || height != a_asset.height) {
				rgba = BlitToCanvas(rgba, width, height, a_asset.width, a_asset.height);
			}

			PushFrame(a_asset, std::move(rgba), durationMs);
		}

		return !a_asset.frames.empty() && a_asset.width > 0 && a_asset.height > 0;
	}

	bool DecodeGif(const ModSettings::entry_base::HintMediaConfig& a_config, MediaAsset& a_asset)
	{
		if (!EnsureWICFactory()) {
			return false;
		}

		const fs::path file = GetConfigPath(a_config);
		if (!PathExists(file)) {
			return false;
		}

		ComPtr<IWICBitmapDecoder> decoder;
		const HRESULT decoderHr = State().wicFactory->CreateDecoderFromFilename(
			file.wstring().c_str(),
			nullptr,
			GENERIC_READ,
			WICDecodeMetadataCacheOnLoad,
			decoder.GetAddressOf());
		if (FAILED(decoderHr) || !decoder) {
			return false;
		}

		UINT frameCount = 0;
		if (FAILED(decoder->GetFrameCount(&frameCount)) || frameCount == 0) {
			return false;
		}

		for (UINT frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
			ComPtr<IWICBitmapFrameDecode> frame;
			if (FAILED(decoder->GetFrame(frameIndex, frame.GetAddressOf())) || !frame) {
				continue;
			}

			std::uint32_t width = 0;
			std::uint32_t height = 0;
			std::vector<std::uint8_t> rgba;
			if (!DecodeWICSourceRGBA(frame.Get(), width, height, rgba)) {
				continue;
			}

			if (a_asset.width == 0 || a_asset.height == 0) {
				a_asset.width = width;
				a_asset.height = height;
			}
			if (width != a_asset.width || height != a_asset.height) {
				rgba = BlitToCanvas(rgba, width, height, a_asset.width, a_asset.height);
			}

			PushFrame(a_asset, std::move(rgba), ReadGifDelayMs(frame.Get()));
		}

		return !a_asset.frames.empty() && a_asset.width > 0 && a_asset.height > 0;
	}

#if DMENU_HAS_WEBP_HEADERS
	bool EnsureWebpRuntimeLoaded()
	{
		auto& api = State().webp;
		if (api.loaded) {
			return true;
		}
		if (api.attemptedLoad) {
			return false;
		}
		api.attemptedLoad = true;

		api.module = LoadLibraryA("libwebpdemux.dll");
		if (!api.module) {
			api.module = LoadLibraryA("webpdemux.dll");
		}
		if (!api.module) {
			return false;
		}

		api.optionsInitInternal = reinterpret_cast<WebpRuntimeApi::FnOptionsInitInternal>(GetProcAddress(api.module, "WebPAnimDecoderOptionsInitInternal"));
		api.decoderNewInternal = reinterpret_cast<WebpRuntimeApi::FnDecoderNewInternal>(GetProcAddress(api.module, "WebPAnimDecoderNewInternal"));
		api.getInfo = reinterpret_cast<WebpRuntimeApi::FnGetInfo>(GetProcAddress(api.module, "WebPAnimDecoderGetInfo"));
		api.hasMoreFrames = reinterpret_cast<WebpRuntimeApi::FnHasMoreFrames>(GetProcAddress(api.module, "WebPAnimDecoderHasMoreFrames"));
		api.getNext = reinterpret_cast<WebpRuntimeApi::FnGetNext>(GetProcAddress(api.module, "WebPAnimDecoderGetNext"));
		api.destroy = reinterpret_cast<WebpRuntimeApi::FnDelete>(GetProcAddress(api.module, "WebPAnimDecoderDelete"));

		if (!api.optionsInitInternal || !api.decoderNewInternal || !api.getInfo || !api.hasMoreFrames || !api.getNext || !api.destroy) {
			FreeLibrary(api.module);
			api.module = nullptr;
			return false;
		}

		api.loaded = true;
		return true;
	}

	bool DecodeAnimatedWebp(const fs::path& a_path, MediaAsset& a_asset)
	{
		if (!EnsureWebpRuntimeLoaded()) {
			return false;
		}

		std::ifstream input(a_path, std::ios::binary);
		if (!input.is_open()) {
			return false;
		}
		std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (bytes.empty()) {
			return false;
		}

		WebPData webpData;
		webpData.bytes = bytes.data();
		webpData.size = bytes.size();

		WebPAnimDecoderOptions options;
		if (!State().webp.optionsInitInternal(&options, WEBP_DEMUX_ABI_VERSION)) {
			return false;
		}
		options.color_mode = MODE_RGBA;

		WebPAnimDecoder* decoder = State().webp.decoderNewInternal(&webpData, &options, WEBP_DEMUX_ABI_VERSION);
		if (!decoder) {
			return false;
		}

		WebPAnimInfo info{};
		const bool hasInfo = State().webp.getInfo(decoder, &info) != 0;
		if (!hasInfo || info.canvas_width <= 0 || info.canvas_height <= 0 || !IsDecodeSizeAllowed(info.canvas_width, info.canvas_height)) {
			State().webp.destroy(decoder);
			return false;
		}

		int previousTimestamp = 0;
		while (State().webp.hasMoreFrames(decoder)) {
			std::uint8_t* frameRGBA = nullptr;
			int timestampMs = 0;
			if (!State().webp.getNext(decoder, &frameRGBA, &timestampMs) || !frameRGBA) {
				break;
			}

			const std::size_t frameBytes = static_cast<std::size_t>(info.canvas_width) * static_cast<std::size_t>(info.canvas_height) * 4u;
			std::vector<std::uint8_t> rgba(frameBytes);
			std::memcpy(rgba.data(), frameRGBA, frameBytes);

			const int deltaMs = timestampMs - previousTimestamp;
			const std::uint32_t frameDuration = deltaMs > 0 ? static_cast<std::uint32_t>(deltaMs) : DEFAULT_FRAME_MS;
			PushFrame(a_asset, std::move(rgba), frameDuration);
			previousTimestamp = timestampMs;
		}

		State().webp.destroy(decoder);

		if (!a_asset.frames.empty()) {
			a_asset.width = static_cast<std::uint32_t>(info.canvas_width);
			a_asset.height = static_cast<std::uint32_t>(info.canvas_height);
			return true;
		}

		return false;
	}
#endif

	bool DecodeStaticImageAsSingleFrame(const fs::path& a_file, MediaAsset& a_asset)
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::vector<std::uint8_t> rgba;
		if (!DecodeImageFileWIC(a_file, width, height, rgba)) {
			return false;
		}
		a_asset.width = width;
		a_asset.height = height;
		PushFrame(a_asset, std::move(rgba), DEFAULT_FRAME_MS);
		return true;
	}

	bool DecodeWebp(const ModSettings::entry_base::HintMediaConfig& a_config, MediaAsset& a_asset)
	{
		const fs::path file = GetConfigPath(a_config);
		if (!PathExists(file)) {
			return false;
		}

#if DMENU_HAS_WEBP_HEADERS
		if (DecodeAnimatedWebp(file, a_asset)) {
			return true;
		}
#endif

		// Static WebP fallback via WIC when libwebp animated decode is unavailable.
		return DecodeStaticImageAsSingleFrame(file, a_asset);
	}

	bool DecodeWebmStreamStep(MediaAsset& a_asset, std::chrono::milliseconds a_budget, std::size_t a_maxFrames, bool a_assetReadyForGlobalBudget)
	{
		if (!a_asset.webm.reader || a_asset.webm.done || a_maxFrames == 0) {
			return false;
		}

		auto& state = State();
		const auto stepStart = Clock::now();
		constexpr std::size_t WEBM_MAX_TOTAL_FRAMES = 3600;
		constexpr std::size_t WEBM_MAX_READ_ATTEMPTS = 512;
		constexpr std::size_t WEBM_MAX_NO_PROGRESS_READS = 8;
		std::size_t readAttempts = 0;
		std::size_t noProgressReads = 0;
		std::size_t producedFrames = 0;
		const std::size_t maxBufferedAhead = (std::clamp)(state.webmMaxBufferedFrames, static_cast<std::size_t>(12), static_cast<std::size_t>(1440));
		const std::size_t estimatedFrameBytes = static_cast<std::size_t>(a_asset.width) * static_cast<std::size_t>(a_asset.height) * 4u;

		// If we were budget-truncated, avoid re-entering expensive reader/decode calls until there is room.
		if (a_assetReadyForGlobalBudget && a_asset.webm.truncatedByBudget && estimatedFrameBytes > 0) {
			TrimConsumedWebmFrames(a_asset, maxBufferedAhead);
			EvictIfNeeded(a_asset.key);
			if (state.usedBytes + estimatedFrameBytes > state.cacheBudgetBytes) {
				return false;
			}
			a_asset.webm.truncatedByBudget = false;
		}

		while (producedFrames < a_maxFrames && readAttempts < WEBM_MAX_READ_ATTEMPTS && !a_asset.webm.done) {
			++readAttempts;
			if ((Clock::now() - stepStart) > a_budget) {
				break;
			}
			if (GetWebmBufferedAheadFrames(a_asset) >= maxBufferedAhead) {
				break;
			}
			if (a_asset.frames.size() >= WEBM_MAX_TOTAL_FRAMES || a_asset.webm.decodedDurationMs >= state.webmMaxClipMs) {
				a_asset.webm.done = true;
				a_asset.webm.truncatedByLimit = true;
				a_asset.webm.reader.Reset();
				if (state.logLevel >= 1) {
					INFO("Hint media webm '{}' decode stopped by safety limit: frames={}, clip={}ms",
						a_asset.key,
						a_asset.frames.size(),
						a_asset.webm.decodedDurationMs);
				}
				break;
			}

			DWORD streamIndex = 0;
			DWORD flags = 0;
			LONGLONG timestamp = 0;
			ComPtr<IMFSample> sample;
			const HRESULT readHr = a_asset.webm.reader->ReadSample(
				MF_SOURCE_READER_FIRST_VIDEO_STREAM,
				0,
				&streamIndex,
				&flags,
				&timestamp,
				sample.GetAddressOf());
			if (FAILED(readHr)) {
				a_asset.webm.done = true;
				a_asset.webm.reader.Reset();
				if (state.logLevel >= 1) {
					INFO("Hint media webm '{}' ReadSample failed (hr={:08X}); frames={}, clip={}ms",
						a_asset.key,
						static_cast<unsigned>(readHr),
						a_asset.frames.size(),
						a_asset.webm.decodedDurationMs);
				}
				break;
			}
			if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
				if (a_asset.config.loop) {
					PROPVARIANT seekStart{};
					PropVariantInit(&seekStart);
					seekStart.vt = VT_I8;
					seekStart.hVal.QuadPart = 0;
					const HRESULT seekHr = a_asset.webm.reader->SetCurrentPosition(GUID_NULL, seekStart);
					PropVariantClear(&seekStart);
					if (SUCCEEDED(seekHr)) {
						a_asset.webm.previousTimestamp = -1;
						a_asset.webm.decodedDurationMs = 0;
						if (state.logLevel >= 1) {
							INFO("Hint media webm '{}' reached EOS and seeked to start for loop playback.", a_asset.key);
						}
						continue;
					}
					if (state.logLevel >= 1) {
						INFO("Hint media webm '{}' EOS seek failed (hr={:08X}); finishing stream.",
							a_asset.key,
							static_cast<unsigned>(seekHr));
					}
				}

				a_asset.webm.done = true;
				a_asset.webm.reader.Reset();
				if (state.logLevel >= 1) {
					INFO("Hint media webm '{}' decode complete: frames={}, clip={}ms",
						a_asset.key,
						a_asset.frames.size(),
						a_asset.webm.decodedDurationMs);
				}
				break;
			}
			if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
				ComPtr<IMFMediaType> changedType;
				if (SUCCEEDED(a_asset.webm.reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, changedType.GetAddressOf())) && changedType) {
					UINT32 changedW = 0;
					UINT32 changedH = 0;
					if (SUCCEEDED(MFGetAttributeSize(changedType.Get(), MF_MT_FRAME_SIZE, &changedW, &changedH)) &&
					    IsDecodeSizeAllowed(changedW, changedH)) {
						a_asset.webm.sourceWidth = changedW;
						a_asset.webm.sourceHeight = changedH;
						a_asset.webm.fallbackFrameMs = GetFrameDurationFromMediaType(changedType.Get());
					}
				}
			}
			if (!sample || a_asset.webm.sourceWidth == 0 || a_asset.webm.sourceHeight == 0) {
				++noProgressReads;
				if (noProgressReads >= WEBM_MAX_NO_PROGRESS_READS) {
					break;
				}
				continue;
			}

			std::vector<std::uint8_t> rgba;
			if (!ExtractRGB32SampleToRGBA(sample.Get(), a_asset.webm.sourceWidth, a_asset.webm.sourceHeight, rgba)) {
				++noProgressReads;
				if (noProgressReads >= WEBM_MAX_NO_PROGRESS_READS) {
					break;
				}
				continue;
			}
			if (a_asset.width != a_asset.webm.sourceWidth || a_asset.height != a_asset.webm.sourceHeight) {
				rgba = ResizeRGBA_Nearest(rgba, a_asset.webm.sourceWidth, a_asset.webm.sourceHeight, a_asset.width, a_asset.height);
				if (rgba.empty()) {
					continue;
				}
			}

			std::uint32_t durationMs = GetSampleDurationMs(sample.Get(), a_asset.webm.fallbackFrameMs);
			a_asset.webm.previousTimestamp = timestamp;

			const std::size_t frameBytes = rgba.size();
			if (frameBytes == 0) {
				continue;
			}

			if (a_assetReadyForGlobalBudget) {
				if (state.usedBytes + frameBytes > state.cacheBudgetBytes) {
					TrimConsumedWebmFrames(a_asset, maxBufferedAhead);
					EvictIfNeeded(a_asset.key);
				}
				if (state.usedBytes + frameBytes > state.cacheBudgetBytes) {
					if (!a_asset.webm.truncatedByBudget && state.logLevel >= 1) {
						INFO("Hint media webm '{}' decode stopped by cache budget: frames={}, clip={}ms, used={}MB, budget={}MB",
							a_asset.key,
							a_asset.frames.size(),
							a_asset.webm.decodedDurationMs,
							state.usedBytes / ONE_MEGABYTE,
							state.cacheBudgetBytes / ONE_MEGABYTE);
					}
					a_asset.webm.truncatedByBudget = true;
					break;
				}
				const std::size_t beforeBytes = a_asset.memoryBytes;
				PushFrame(a_asset, std::move(rgba), durationMs);
				const std::size_t deltaBytes = a_asset.memoryBytes - beforeBytes;
				state.usedBytes += deltaBytes;
				a_asset.webm.truncatedByBudget = false;
				EvictIfNeeded(a_asset.key);
			} else {
				if (a_asset.memoryBytes + frameBytes > state.cacheBudgetBytes) {
					if (!a_asset.webm.truncatedByBudget && state.logLevel >= 1) {
						INFO("Hint media webm '{}' decode stopped by per-asset cache budget: frames={}, clip={}ms, asset={}MB, budget={}MB",
							a_asset.key,
							a_asset.frames.size(),
							a_asset.webm.decodedDurationMs,
							a_asset.memoryBytes / ONE_MEGABYTE,
							state.cacheBudgetBytes / ONE_MEGABYTE);
					}
					a_asset.webm.truncatedByBudget = true;
					break;
				}
				PushFrame(a_asset, std::move(rgba), durationMs);
				a_asset.webm.truncatedByBudget = false;
			}

			a_asset.webm.decodedDurationMs += durationMs;
			++producedFrames;
			noProgressReads = 0;
		}

		return producedFrames > 0;
	}

	bool DecodeWebm(const ModSettings::entry_base::HintMediaConfig& a_config, MediaAsset& a_asset)
	{
		const fs::path file = GetConfigPath(a_config);
		if (!PathExists(file) || IsDirectory(file)) {
			return false;
		}
		if (!EnsureMediaFoundation()) {
			return false;
		}

		ComPtr<IMFAttributes> attributes;
		if (FAILED(MFCreateAttributes(attributes.GetAddressOf(), 2)) || !attributes) {
			return false;
		}
		attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
		attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);

		ComPtr<IMFSourceReader> reader;
		const HRESULT readerHr = MFCreateSourceReaderFromURL(file.wstring().c_str(), attributes.Get(), reader.GetAddressOf());
		if (FAILED(readerHr) || !reader) {
			return false;
		}

		reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
		reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

		ComPtr<IMFMediaType> outputType;
		if (FAILED(MFCreateMediaType(outputType.GetAddressOf())) || !outputType) {
			return false;
		}
		outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
		if (FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType.Get()))) {
			return false;
		}

		ComPtr<IMFMediaType> mediaType;
		if (FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, mediaType.GetAddressOf())) || !mediaType) {
			return false;
		}

		UINT32 width = 0;
		UINT32 height = 0;
		if (FAILED(MFGetAttributeSize(mediaType.Get(), MF_MT_FRAME_SIZE, &width, &height)) || !IsDecodeSizeAllowed(width, height)) {
			return false;
		}

		std::uint32_t targetW = width;
		std::uint32_t targetH = height;
		const float previewScale = (std::clamp)(State().previewScale, 0.5f, 3.0f);
		const float decodeScale = (std::max)(1.0f, previewScale);
		const float maxW = static_cast<float>((std::clamp)(a_config.maxW, 32, 4096));
		const float maxH = static_cast<float>((std::clamp)(a_config.maxH, 32, 4096));
		const float decodeMaxW = (std::clamp)(maxW * decodeScale, 32.0f, 4096.0f);
		const float decodeMaxH = (std::clamp)(maxH * decodeScale, 32.0f, 4096.0f);
		const float srcW = static_cast<float>(width);
		const float srcH = static_cast<float>(height);
		if (srcW > 0.0f && srcH > 0.0f) {
			const float fitScale = (std::min)(1.0f, (std::min)(decodeMaxW / srcW, decodeMaxH / srcH));
			targetW = static_cast<std::uint32_t>((std::max)(1.0f, std::round(srcW * fitScale)));
			targetH = static_cast<std::uint32_t>((std::max)(1.0f, std::round(srcH * fitScale)));
		}

		a_asset.width = targetW;
		a_asset.height = targetH;
		const std::size_t frameBytes = static_cast<std::size_t>(targetW) * static_cast<std::size_t>(targetH) * 4u;
		if (frameBytes == 0 || frameBytes > State().cacheBudgetBytes) {
			return false;
		}

		a_asset.webm.reader = std::move(reader);
		a_asset.webm.sourceWidth = width;
		a_asset.webm.sourceHeight = height;
		a_asset.webm.fallbackFrameMs = GetFrameDurationFromMediaType(mediaType.Get());
		a_asset.webm.decodedDurationMs = 0;
		a_asset.webm.previousTimestamp = -1;
		a_asset.webm.done = false;
		a_asset.webm.truncatedByBudget = false;
		a_asset.webm.truncatedByLimit = false;

		const bool decodedInitial = DecodeWebmStreamStep(a_asset, std::chrono::milliseconds(8), 1, false);

		if (State().logLevel >= 1) {
			INFO("Hint media webm '{}' initial decode -> {} frame(s), source={}x{}, target={}x{}, clip={}ms{}",
				file.string(),
				a_asset.frames.size(),
				width,
				height,
				targetW,
				targetH,
				a_asset.webm.decodedDurationMs,
				a_asset.webm.done ? " (complete)" : " (streaming)");
		}

		if (!decodedInitial || a_asset.frames.empty()) {
			a_asset.webm.done = true;
			a_asset.webm.reader.Reset();
			return false;
		}

		return a_asset.width > 0 && a_asset.height > 0;
	}

	bool CreateDynamicTexture(MediaAsset& a_asset)
	{
		auto& state = State();
		if (!state.device || !state.context || a_asset.width == 0 || a_asset.height == 0) {
			return false;
		}

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = a_asset.width;
		desc.Height = a_asset.height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		if (FAILED(state.device->CreateTexture2D(&desc, nullptr, a_asset.texture.GetAddressOf())) || !a_asset.texture) {
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;
		if (FAILED(state.device->CreateShaderResourceView(a_asset.texture.Get(), &srvDesc, a_asset.srv.GetAddressOf())) || !a_asset.srv) {
			a_asset.texture.Reset();
			return false;
		}

		a_asset.uploadedFrame = -1;
		return true;
	}

	bool UploadFrame(MediaAsset& a_asset, std::size_t a_frameIndex)
	{
		auto& state = State();
		if (!state.context || !a_asset.texture || a_frameIndex >= a_asset.frames.size()) {
			return false;
		}

		const MediaFrame& frame = a_asset.frames[a_frameIndex];
		const std::size_t expectedBytes = static_cast<std::size_t>(a_asset.width) * static_cast<std::size_t>(a_asset.height) * 4u;
		if (frame.rgba.size() < expectedBytes) {
			return false;
		}

		const UINT rowPitch = static_cast<UINT>(a_asset.width * 4u);
		state.context->UpdateSubresource(a_asset.texture.Get(), 0, nullptr, frame.rgba.data(), rowPitch, 0);
		a_asset.uploadedFrame = static_cast<int>(a_frameIndex);
		return true;
	}

	bool DecodeAssetFrames(MediaAsset& a_asset)
	{
		switch (a_asset.config.type) {
		case ModSettings::entry_base::HintMediaConfig::Type::Flipbook:
			return DecodeFlipbook(a_asset.config, a_asset);
		case ModSettings::entry_base::HintMediaConfig::Type::Gif:
			return DecodeGif(a_asset.config, a_asset);
		case ModSettings::entry_base::HintMediaConfig::Type::Webp:
			return DecodeWebp(a_asset.config, a_asset);
		case ModSettings::entry_base::HintMediaConfig::Type::Webm:
			return DecodeWebm(a_asset.config, a_asset);
		default:
			return false;
		}
	}

	bool EnsureAssetReady(MediaAsset& a_asset)
	{
		auto& state = State();
		if (a_asset.ready) {
			return true;
		}

		const Clock::time_point now = Clock::now();
		if (a_asset.failed && now < a_asset.retryAt) {
			return false;
		}

		a_asset.failed = false;
		a_asset.retryAt = Clock::time_point::min();
		a_asset.frames.clear();
		a_asset.memoryBytes = 0;
		a_asset.width = 0;
		a_asset.height = 0;

		if (!DecodeAssetFrames(a_asset) || a_asset.frames.empty() || a_asset.width == 0 || a_asset.height == 0) {
			MarkAssetFailed(a_asset, "decode failed");
			return false;
		}

		if (a_asset.memoryBytes > state.cacheBudgetBytes) {
			MarkAssetFailed(a_asset, "decoded media exceeds cache budget");
			return false;
		}

		if (!CreateDynamicTexture(a_asset)) {
			MarkAssetFailed(a_asset, "texture creation failed");
			return false;
		}

		a_asset.ready = true;
		a_asset.nextValidationAt = now + std::chrono::seconds(1);
		state.usedBytes += a_asset.memoryBytes;
		EvictIfNeeded(a_asset.key);
		return a_asset.ready;
	}

	MediaAsset* GetOrCreateAsset(const ModSettings::entry_base::HintMediaConfig& a_config)
	{
		auto& state = State();
		const std::string key = GetMediaKey(a_config);
		if (key.empty()) {
			return nullptr;
		}

		auto [it, inserted] = state.assets.try_emplace(key);
		MediaAsset& asset = it->second;
		if (inserted) {
			asset.key = key;
			asset.config = a_config;
		} else {
			if (NeedsAssetRebuild(asset.config, a_config)) {
				ReleaseAssetStorage(asset);
				asset.failed = false;
				asset.retryAt = Clock::time_point::min();
				asset.nextValidationAt = Clock::time_point::min();
				state.playbacks.erase(key);
			}
			asset.config = a_config;
		}

		asset.lastUsedTick = ++state.usageTick;
		asset.lastActiveHousekeepingTick = state.housekeepingTick;

		if (!EnsureAssetReady(asset)) {
			return nullptr;
		}
		return &asset;
	}

	bool ValidateSourcePathStillExists(MediaAsset& a_asset)
	{
		const Clock::time_point now = Clock::now();
		if (now < a_asset.nextValidationAt) {
			return true;
		}

		a_asset.nextValidationAt = now + std::chrono::seconds(1);
		const fs::path sourcePath = GetConfigPath(a_asset.config);

		const bool validPath = a_asset.config.type == ModSettings::entry_base::HintMediaConfig::Type::Flipbook ?
			                       (PathExists(sourcePath) && IsDirectory(sourcePath)) :
			                       PathExists(sourcePath);
		if (!validPath) {
			MarkAssetFailed(a_asset, "source path missing");
			return false;
		}
		return true;
	}

	std::size_t UpdatePlaybackAndGetFrame(MediaAsset& a_asset)
	{
		auto& state = State();
		PlaybackState& playback = state.playbacks[a_asset.key];
		playback.lastUsedTick = state.housekeepingTick;
		if (a_asset.frames.empty()) {
			playback.frameIndex = 0;
			playback.accumulatedMs = 0.0f;
			return 0;
		}

		if (playback.frameIndex >= a_asset.frames.size()) {
			playback.frameIndex = 0;
			playback.accumulatedMs = 0.0f;
		}

		if (a_asset.frames.size() == 1) {
			return playback.frameIndex;
		}

		const float deltaMs = (std::max)(0.0f, ImGui::GetIO().DeltaTime * 1000.0f);
		playback.accumulatedMs += deltaMs;

		std::size_t guard = 0;
		while (guard < a_asset.frames.size() * 2u) {
			const std::uint32_t frameDuration = a_asset.frames[playback.frameIndex].durationMs;
			if (playback.accumulatedMs < frameDuration) {
				break;
			}
			playback.accumulatedMs -= static_cast<float>(frameDuration);
			if (playback.frameIndex + 1u < a_asset.frames.size()) {
				++playback.frameIndex;
			} else if (a_asset.config.type == ModSettings::entry_base::HintMediaConfig::Type::Webm &&
			           !a_asset.webm.done &&
			           !a_asset.webm.truncatedByBudget) {
				// Avoid looping a partial decode while stream decoding is actively progressing.
				playback.frameIndex = a_asset.frames.size() - 1u;
				playback.accumulatedMs = 0.0f;
				break;
			} else if (a_asset.config.type == ModSettings::entry_base::HintMediaConfig::Type::Webm &&
			           a_asset.webm.done &&
			           (a_asset.webm.truncatedByBudget || a_asset.webm.truncatedByLimit)) {
				// If truncated, do not loop a short partial segment.
				playback.frameIndex = a_asset.frames.size() - 1u;
				playback.accumulatedMs = 0.0f;
				break;
			} else if (a_asset.config.loop) {
				playback.frameIndex = 0;
			} else {
				playback.frameIndex = a_asset.frames.size() - 1u;
				playback.accumulatedMs = 0.0f;
				break;
			}
			++guard;
		}

		const std::size_t keepBufferedFrames = (std::clamp)(State().webmMaxBufferedFrames, static_cast<std::size_t>(12), static_cast<std::size_t>(1440));
		TrimConsumedWebmFrames(a_asset, keepBufferedFrames);

		return playback.frameIndex;
	}

	bool DrawMediaPreview(const ModSettings::entry_base::HintMediaConfig& a_config)
	{
		MediaAsset* asset = GetOrCreateAsset(a_config);
		if (!asset || !asset->ready || !asset->srv) {
			return false;
		}
		if (!ValidateSourcePathStillExists(*asset)) {
			return false;
		}
		if (asset->config.type == ModSettings::entry_base::HintMediaConfig::Type::Webm &&
		    !asset->webm.done &&
		    asset->webm.reader) {
			const auto& state = State();
			const std::uint32_t budgetMs = (std::clamp)(state.webmDecodeBudgetMs, static_cast<std::uint32_t>(1), static_cast<std::uint32_t>(16));
			const std::size_t maxFrames = (std::clamp)(state.webmDecodeMaxFramesPerTick, static_cast<std::size_t>(1), static_cast<std::size_t>(8));
			DecodeWebmStreamStep(*asset, std::chrono::milliseconds(budgetMs), maxFrames, true);
		}

		const std::size_t frameIndex = UpdatePlaybackAndGetFrame(*asset);
		if (asset->uploadedFrame != static_cast<int>(frameIndex) && !UploadFrame(*asset, frameIndex)) {
			MarkAssetFailed(*asset, "frame upload failed");
			return false;
		}

		const float previewScale = (std::clamp)(State().previewScale, 0.5f, 3.0f);
		const float baseMaxW = static_cast<float>((std::clamp)(a_config.maxW, 32, 4096));
		const float baseMaxH = static_cast<float>((std::clamp)(a_config.maxH, 32, 4096));
		const float maxW = (std::clamp)(baseMaxW * previewScale, 32.0f, 4096.0f);
		const float maxH = (std::clamp)(baseMaxH * previewScale, 32.0f, 4096.0f);
		const float srcW = static_cast<float>(asset->width);
		const float srcH = static_cast<float>(asset->height);
		if (srcW <= 0.0f || srcH <= 0.0f) {
			return false;
		}

		const float fitLimit = (std::min)(maxW / srcW, maxH / srcH);
		const bool allowUpscale = State().previewScale > 1.001f;
		const float fitScale = allowUpscale ? (std::clamp)(fitLimit, 0.01f, 8.0f) : (std::min)(1.0f, fitLimit);
		const ImVec2 drawSize(srcW * fitScale, srcH * fitScale);
		ImGui::Image(reinterpret_cast<ImTextureID>(asset->srv.Get()), drawSize);
		return true;
	}

	bool ShouldShowTooltip(
		ModSettings::entry_base::HintConfig::ShowOn a_showOn,
		bool a_hoveredControl,
		bool a_hoveredNote)
	{
		switch (a_showOn) {
		case ModSettings::entry_base::HintConfig::ShowOn::Note:
			return a_hoveredNote;
		case ModSettings::entry_base::HintConfig::ShowOn::Control:
			return a_hoveredControl;
		case ModSettings::entry_base::HintConfig::ShowOn::Both:
			return a_hoveredNote || a_hoveredControl;
		default:
			return a_hoveredNote;
		}
	}
}

HintMediaManager& HintMediaManager::Get()
{
	static HintMediaManager instance;
	return instance;
}

void HintMediaManager::OnD3DReady(ID3D11Device* a_device, ID3D11DeviceContext* a_context)
{
	auto& state = State();
	state.device = a_device;
	state.context = a_context;
}

void HintMediaManager::Tick(float)
{
	auto& state = State();
	++state.housekeepingTick;
	if (state.housekeepingTick % 240 != 0) {
		return;
	}

	const std::uint64_t nowTick = state.housekeepingTick;
	for (auto it = state.playbacks.begin(); it != state.playbacks.end();) {
		if (nowTick > it->second.lastUsedTick + 600) {
			it = state.playbacks.erase(it);
		} else {
			++it;
		}
	}

	for (auto it = state.assets.begin(); it != state.assets.end();) {
		const bool stale = nowTick > it->second.lastActiveHousekeepingTick + 900;
		if (!stale) {
			++it;
			continue;
		}

		if (state.logLevel >= 2) {
			INFO("Releasing idle hint media '{}' from cache", it->first);
		}
		ReleaseAssetStorage(it->second);
		state.playbacks.erase(it->first);
		it = state.assets.erase(it);
	}

	if (state.usedBytes > state.cacheBudgetBytes) {
		EvictIfNeeded("");
	}
}

void HintMediaManager::SetEnabled(bool a_enabled)
{
	State().enabled = a_enabled;
}

void HintMediaManager::SetCacheBudgetMB(std::size_t a_cacheBudgetMB)
{
	auto& state = State();
	const std::size_t clampedMB = (std::clamp)(a_cacheBudgetMB, static_cast<std::size_t>(16), static_cast<std::size_t>(1024));
	state.cacheBudgetBytes = clampedMB * ONE_MEGABYTE;
	EvictIfNeeded("");
}

void HintMediaManager::SetMaxDecodePixels(std::uint64_t a_maxPixels)
{
	auto& state = State();
	state.maxDecodePixels = (std::clamp)(a_maxPixels, 256ull * 256ull, 8192ull * 8192ull);
}

void HintMediaManager::SetLogLevel(int a_logLevel)
{
	State().logLevel = (std::clamp)(a_logLevel, 0, 2);
}

void HintMediaManager::SetPreviewScale(float a_previewScale)
{
	State().previewScale = (std::clamp)(a_previewScale, 0.5f, 3.0f);
}

void HintMediaManager::SetWebmMaxBufferedFrames(std::size_t a_frames)
{
	State().webmMaxBufferedFrames = (std::clamp)(a_frames, static_cast<std::size_t>(12), static_cast<std::size_t>(1440));
}

void HintMediaManager::SetWebmMaxClipSeconds(std::uint32_t a_seconds)
{
	const std::uint32_t clampedSeconds = (std::clamp)(a_seconds, static_cast<std::uint32_t>(1), static_cast<std::uint32_t>(600));
	State().webmMaxClipMs = clampedSeconds * 1000u;
}

void HintMediaManager::SetWebmDecodeBudgetMs(std::uint32_t a_ms)
{
	State().webmDecodeBudgetMs = (std::clamp)(a_ms, static_cast<std::uint32_t>(1), static_cast<std::uint32_t>(16));
}

void HintMediaManager::SetWebmDecodeMaxFramesPerTick(std::size_t a_frames)
{
	State().webmDecodeMaxFramesPerTick = (std::clamp)(a_frames, static_cast<std::size_t>(1), static_cast<std::size_t>(8));
}

void HintMediaManager::TryPreload(const ModSettings::entry_base& a_entry)
{
	const auto& state = State();
	if (!state.enabled || !state.device || !state.context || !a_entry.hint.has_value() || !a_entry.hint->media.has_value()) {
		return;
	}

	const auto& media = *a_entry.hint->media;
	if (!media.preload) {
		return;
	}

	[[maybe_unused]] MediaAsset* ignored = GetOrCreateAsset(media);
}

bool HintMediaManager::DrawHintTooltip(
	const ModSettings::entry_base& a_entry,
	const char* a_descText,
	bool a_hoveredControl,
	bool a_hoveredNote,
	bool a_forceShow,
	const ImVec2* a_forcedPos)
{
	const auto& state = State();
	if (!a_entry.hint.has_value()) {
		return false;
	}
	if (!a_forceShow && !ShouldShowTooltip(a_entry.hint->showOn, a_hoveredControl, a_hoveredNote)) {
		return false;
	}

	const bool hasDesc = a_descText != nullptr && a_descText[0] != '\0';
	const bool wantsMedia = state.enabled && a_entry.hint->media.has_value() && state.device && state.context;

	if (!hasDesc && !wantsMedia) {
		return false;
	}

	auto drawContent = [&]() -> bool {
		bool drewAnything = false;
		bool drewMedia = false;
		if (wantsMedia) {
			drewMedia = DrawMediaPreview(*a_entry.hint->media);
			drewAnything |= drewMedia;
		}

		if (hasDesc) {
			if (drewMedia) {
				ImGui::Separator();
			}
			const float previewScale = (std::clamp)(state.previewScale, 0.5f, 3.0f);
			const int baseWrap = (std::clamp)(a_entry.hint->media ? a_entry.hint->media->maxW : 420, 120, 900);
			const float wrapWidth = (std::clamp)(static_cast<float>(baseWrap) * previewScale, 120.0f, 1200.0f);
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);
			ImGui::TextUnformatted(a_descText);
			ImGui::PopTextWrapPos();
			drewAnything = true;
		}
		return drewAnything;
	};

	if (a_forceShow) {
		ImVec2 windowPos = a_forcedPos ? *a_forcedPos : ImGui::GetMousePos();
		if (const ImGuiViewport* vp = ImGui::GetMainViewport()) {
			const float margin = 6.0f;
			windowPos.x = (std::clamp)(windowPos.x, vp->Pos.x + margin, vp->Pos.x + vp->Size.x - margin);
			windowPos.y = (std::clamp)(windowPos.y, vp->Pos.y + margin, vp->Pos.y + vp->Size.y - margin);
		}
		// Reuse tooltip path for click-pinned mode to keep playback behavior identical to hover mode.
		ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
	}

	ImGui::BeginTooltip();
	const bool drewAnything = drawContent();
	ImGui::EndTooltip();
	return drewAnything;
}
