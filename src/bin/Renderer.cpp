#include "Renderer.h"

#include <d3d11.h>

#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <array>
#include <dxgi.h>
#include "imgui_internal.h"

#include "dMenu.h"

#include "Utils.h"
#include "HintMedia.h"
#include "ScreenKeyboardBridge.h"
#include "menus/Settings.h"
#include "InputListener.h"
#include "RuntimeCompatibility.h"
#include "ime/IMEManager.h"

#include "menus/Translator.h"
namespace
{
	std::atomic<bool> g_applyMenuStateToBackends{ true };

	void RequestMenuStateBackendApply()
	{
		g_applyMenuStateToBackends.store(true, std::memory_order_release);
	}

	void ApplyMenuStateToBackends()
	{
		if (!Renderer::IsReady() || !ImGui::GetCurrentContext()) {
			return;
		}
		if (!g_applyMenuStateToBackends.exchange(false, std::memory_order_acq_rel)) {
			return;
		}

		const bool enabled = Renderer::IsEnabled();
		IME::Manager::Get().SetMenuEnabled(enabled);
		ImGui::GetIO().MouseDrawCursor = enabled;
	}
}


LRESULT Renderer::WndProcHook::thunk(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	auto& io = ImGui::GetIO();
	LRESULT imeResult = 0;
	if (IME::Manager::Get().HandleWndProc(hWnd, uMsg, wParam, lParam, imeResult)) {
		return imeResult;
	}

	if (uMsg == WM_KILLFOCUS || (uMsg == WM_ACTIVATEAPP && !wParam)) {
		io.ClearInputCharacters();
		io.ClearInputKeys();
	}

	return func(hWnd, uMsg, wParam, lParam);
}


void SetupImGuiStyle()
{
	auto& style = ImGui::GetStyle();
	auto& colors = style.Colors;

	// Theme from https://github.com/ArranzCNL/ImprovedCameraSE-NG

	//style.WindowTitleAlign = ImVec2(0.5, 0.5);
	//style.FramePadding = ImVec2(4, 4);

	// Rounded slider grabber
	style.GrabRounding = 12.0f;

	// Window
	colors[ImGuiCol_WindowBg] = ImVec4{ 0.118f, 0.118f, 0.118f, 0.784f };
	colors[ImGuiCol_ResizeGrip] = ImVec4{ 0.2f, 0.2f, 0.2f, 0.5f };
	colors[ImGuiCol_ResizeGripHovered] = ImVec4{ 0.3f, 0.3f, 0.3f, 0.75f };
	colors[ImGuiCol_ResizeGripActive] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };

	// Header
	colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.2f, 0.2f, 1.0f };
	colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.3f, 0.3f, 1.0f };
	colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };

	// Title
	colors[ImGuiCol_TitleBg] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };
	colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };

	// Frame Background
	colors[ImGuiCol_FrameBg] = ImVec4{ 0.2f, 0.2f, 0.2f, 1.0f };
	colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.3f, 0.3f, 1.0f };
	colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };

	// Button
	colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.2f, 0.2f, 1.0f };
	colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.3f, 0.3f, 1.0f };
	colors[ImGuiCol_ButtonActive] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };

	// Tab
	colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };
	colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.38f, 0.38f, 1.0f };
	colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.28f, 0.28f, 1.0f };
	colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.15f, 0.15f, 0.15f, 1.0f };
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.2f, 0.2f, 1.0f };

}

void Renderer::D3DInitHook::thunk()
{
	func();

	INFO("D3DInit Hooked!");

	// Updated for newer CommonLibSSE-NG: use BSGraphics::Renderer instead of BSRenderManager.
	auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
	if (!renderer) {
		ERROR("Cannot find BSGraphics::Renderer. Initialization failed!");
		return;
	}

	auto* nativeDevice = RE::BSGraphics::Renderer::GetDevice();
	auto* nativeContext = renderer->GetRuntimeData().context;
	if (!nativeDevice || !nativeContext) {
		ERROR("Cannot get the D3D11 device/context. Initialization failed!");
		return;
	}

	INFO("Getting swapchain...");
	auto* window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
	auto* swapchain = window ? window->swapChain : nullptr;
	if (!swapchain) {
		ERROR("Cannot find swapchain. Initialization failed!");
		return;
	}

	device = reinterpret_cast<ID3D11Device*>(nativeDevice);
	context = reinterpret_cast<ID3D11DeviceContext*>(nativeContext);

	INFO("Initializing ImGui...");
	ImGui::CreateContext();
	auto& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
	auto hwnd = reinterpret_cast<HWND>(window->hWnd);
	if (!ImGui_ImplWin32_Init(hwnd)) {
		ERROR("ImGui initialization failed (Win32)");
		return;
	}
	if (!ImGui_ImplDX11_Init(device, context)) {
		ERROR("ImGui initialization failed (DX11)");
		return;
	}
	HintMediaManager::Get().OnD3DReady(device, context);

	INFO("ImGui initialized!");

	WndProcHook::func = reinterpret_cast<WNDPROC>(
		SetWindowLongPtrA(
			hwnd,
			GWLP_WNDPROC,
			reinterpret_cast<LONG_PTR>(WndProcHook::thunk)));
	if (!WndProcHook::func)
		ERROR("SetWindowLongPtrA failed!");

	IME::Manager::Get().Initialize(hwnd);

// initialize font selection here
	INFO("Building font atlas...");
	std::filesystem::path fontPath;
	bool foundCustomFont = false;
	const ImWchar* glyphRanges = 0;
#define FONTSETTING_PATH "Data\\SKSE\\Plugins\\dMenu\\fonts\\FontConfig.ini"
	CSimpleIniA ini;
	ini.LoadFile(FONTSETTING_PATH);
	if (!ini.IsEmpty()) {
		const char* language = ini.GetValue("config", "font", 0);
		if (language) {
			std::string fontDir = R"(Data\SKSE\Plugins\dMenu\fonts\)" + std::string(language);
			// check if folder exists
			if (std::filesystem::exists(fontDir) && std::filesystem::is_directory(fontDir)) {
				for (const auto& entry : std::filesystem::directory_iterator(fontDir)) {
					auto entryPath = entry.path();
					if (entryPath.extension() == ".ttf" || entryPath.extension() == ".ttc") {
						fontPath = entryPath;
						foundCustomFont = true;
						break;
					}
				}
			}
			if (foundCustomFont) {
				std::string languageStr = language;
				INFO("Loading font: {}", fontPath.string().c_str());
				if (languageStr == "Chinese") {
					INFO("Glyph range set to Chinese");
					glyphRanges = ImGui::GetIO().Fonts->GetGlyphRangesChineseFull();
				} else if (languageStr == "Korean") {
					INFO("Glyph range set to Korean");
					glyphRanges = ImGui::GetIO().Fonts->GetGlyphRangesKorean();
				} else if (languageStr == "Japanese") {
					INFO("Glyph range set to Japanese");
					glyphRanges = ImGui::GetIO().Fonts->GetGlyphRangesJapanese();
				} else if (languageStr == "Thai") {
					INFO("Glyph range set to Thai");
					glyphRanges = ImGui::GetIO().Fonts->GetGlyphRangesThai();
				} else if (languageStr == "Vietnamese") {
					INFO("Glyph range set to Vietnamese");
					glyphRanges = ImGui::GetIO().Fonts->GetGlyphRangesVietnamese();
				} else if (languageStr == "Cyrillic") {
					glyphRanges = ImGui::GetIO().Fonts->GetGlyphRangesCyrillic();
					INFO("Glyph range set to Cyrillic");
				} else if (languageStr == "Turkish") {
					// Basic Latin + Latin-1 + Latin Extended-A for Turkish characters
					static const ImWchar turkishRanges[] = {
						0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
						0x0100, 0x017F, // Latin Extended-A (İ, ı, Ğ, ğ, Ş, ş, etc.)
						0
					};
					glyphRanges = turkishRanges;
					INFO("Glyph range set to Turkish (Latin Extended-A)");
				} else if (
					// Languages that rely on Latin + Latin-1 + Latin Extended-A.
					languageStr == "LatinExt" ||
					languageStr == "Polish" ||
					languageStr == "Czech" ||
					languageStr == "Slovak" ||
					languageStr == "Hungarian" ||
					languageStr == "Romanian" ||
					languageStr == "Croatian" ||
					languageStr == "Slovenian" ||
					languageStr == "Lithuanian" ||
					languageStr == "Latvian" ||
					languageStr == "Estonian" ||
					languageStr == "Albanian" ||
					languageStr == "Icelandic" ||
					languageStr == "Bosnian" ||
					languageStr == "SerbianLatin" ||
					languageStr == "Norwegian" ||
					languageStr == "Swedish" ||
					languageStr == "Danish" ||
					languageStr == "Finnish") {
					static const ImWchar latinExtRanges[] = {
						0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
						0x0100, 0x017F, // Latin Extended-A
						0
					};
					glyphRanges = latinExtRanges;
					INFO("Glyph range set to Latin Extended-A for '{}'", languageStr);
				}
			} else {
				INFO("No font found for language: {}", language);
			}
		}
	}
#define ENABLE_FREETYPE 0
#if ENABLE_FREETYPE
	ImFontAtlas* atlas = ImGui::GetIO().Fonts;
	atlas->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
	atlas->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
#else
#endif
	if (foundCustomFont) {
		ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 32.0f, NULL, glyphRanges);
	}

	auto mergeFallbackFont = [&](const auto& candidates, const ImWchar* ranges, std::string_view label) {
		ImFontConfig config{};
		config.MergeMode = true;
		config.PixelSnapH = true;
		for (const auto& candidate : candidates) {
			if (!std::filesystem::exists(candidate)) {
				continue;
			}

			if (ImGui::GetIO().Fonts->AddFontFromFileTTF(candidate, 32.0f, &config, ranges)) {
				INFO("Merged {} fallback font: {}", label, candidate);
				return;
			}
		}

		INFO("No {} fallback font available for screen keyboard glyphs", label);
	};

	mergeFallbackFont(
		std::array{
			"C:\\Windows\\Fonts\\meiryo.ttc",
			"C:\\Windows\\Fonts\\msgothic.ttc",
			"C:\\Windows\\Fonts\\YuGothM.ttc"
		},
		ImGui::GetIO().Fonts->GetGlyphRangesJapanese(),
		"Japanese");
	mergeFallbackFont(
		std::array{
			"C:\\Windows\\Fonts\\malgun.ttf",
			"C:\\Windows\\Fonts\\gulim.ttc"
		},
		ImGui::GetIO().Fonts->GetGlyphRangesKorean(),
		"Korean");
	mergeFallbackFont(
		std::array{
			"C:\\Windows\\Fonts\\msyh.ttc",
			"C:\\Windows\\Fonts\\msyh.ttf",
			"C:\\Windows\\Fonts\\simsun.ttc",
			"C:\\Windows\\Fonts\\simhei.ttf"
		},
		ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon(),
		"Chinese");
	SetupImGuiStyle();

	initialized.store(true);
	ApplyMenuStateToBackends();

}

void Renderer::DXGIPresentHook::thunk(std::uint32_t a_p1)
{
	func(a_p1);

	if (!D3DInitHook::initialized.load())
		return;

	// prologue
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	InputListener::ApplyBlockedImGuiGamepadKeys();
	ImGui::NewFrame();
	ApplyMenuStateToBackends();
	HintMediaManager::Get().Tick(ImGui::GetIO().DeltaTime);

	// do stuff
	Renderer::draw();

	// epilogue
	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

struct ImageSet
{
	std::int32_t my_image_width = 0;
	std::int32_t my_image_height = 0;
	ID3D11ShaderResourceView* my_texture = nullptr;
};


void Renderer::MessageCallback(SKSE::MessagingInterface::Message* msg)  //CallBack & LoadTextureFromFile should called after resource loaded.
{
	if (msg->type == SKSE::MessagingInterface::kDataLoaded && D3DInitHook::initialized.load()) {
		// Read Texture only after game engine finished load all it renderer resource.
		auto& io = ImGui::GetIO();
		io.MouseDrawCursor = IsEnabled();
		io.WantSetMousePos = true;
	}
}

bool Renderer::Install()
{
	const auto* d3dInit = RuntimeCompatibility::GetResolvedCallSite(RuntimeCompatibility::Hook::D3DInit);
	const auto* dxgiPresent = RuntimeCompatibility::GetResolvedCallSite(RuntimeCompatibility::Hook::DXGIPresent);
	if (!d3dInit || !dxgiPresent) {
		ERROR("Renderer hook preflight was not completed; renderer hooks were not installed");
		return false;
	}

	auto g_message = SKSE::GetMessagingInterface();
	if (!g_message) {
		ERROR("Messaging Interface Not Found!");
		return false;
	}

	if (!g_message->RegisterListener(MessageCallback)) {
		ERROR("Failed to register the renderer messaging listener");
		return false;
	}

	auto& trampoline = SKSE::GetTrampoline();
	D3DInitHook::func = trampoline.write_call<5>(d3dInit->address, D3DInitHook::thunk);
	DXGIPresentHook::func = trampoline.write_call<5>(dxgiPresent->address, DXGIPresentHook::thunk);
	
	return true;
}

void Renderer::flip() 
{
	Toggle();
}

void Renderer::SetEnabled(bool a_enabled)
{
	enable.store(a_enabled);
	RequestMenuStateBackendApply();
}

void Renderer::Open()
{
	SetEnabled(true);
}

void Renderer::Close()
{
	SetEnabled(false);
}

void Renderer::Toggle()
{
	SetEnabled(!IsEnabled());
}


float Renderer::GetResolutionScaleWidth()
{
	return ImGui::GetIO().DisplaySize.x / 1920.f;
}

float Renderer::GetResolutionScaleHeight()
{
	return ImGui::GetIO().DisplaySize.y / 1080.f;
}


void Renderer::draw()
{
	const bool menuEnabled = IsEnabled();
	IME::Manager::Get().BeginFrame(menuEnabled);
	if (menuEnabled) {
		if (!DMenu::initialized) {
			ImVec2 screenSize = ImGui::GetMainViewport()->Size;
			float screenSizeX = screenSize.x;
			float screenSizeY = screenSize.y;
			DMenu::init(screenSizeX, screenSizeY);
		}

		DMenu::draw();
		ScreenKeyboardBridge::GetSingleton().Draw();
	}
	IME::Manager::Get().EndFrame();

}
