#pragma once

// Forward declarations to avoid needing D3D headers in this header.
struct ID3D11Device;
struct ID3D11DeviceContext;

class Renderer
{
	struct WndProcHook
	{
		static LRESULT thunk(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
		static inline WNDPROC func;
	};

	struct D3DInitHook
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;

		static inline std::atomic<bool> initialized = false;
	};

	struct DXGIPresentHook
	{
		static void thunk(std::uint32_t a_p1);
		static inline REL::Relocation<decltype(thunk)> func;
	};


private:
	Renderer() = delete;

	static void draw();  //Rendering Meters.
	static void MessageCallback(SKSE::MessagingInterface::Message* msg);

	static inline bool ShowMeters = false;
	static inline ID3D11Device* device = nullptr;
	static inline ID3D11DeviceContext* context = nullptr;

	static inline std::atomic<bool> enable{ false };


public:
	static bool Install();

	static void flip();
	static void SetEnabled(bool a_enabled);
	static void Open();
	static void Close();
	static void Toggle();

	static bool IsEnabled() { return enable.load(); }
	static bool IsReady() { return D3DInitHook::initialized.load(); }

	static float GetResolutionScaleWidth();   // { return ImGui::GetIO().DisplaySize.x / 1920.f; }
	static float GetResolutionScaleHeight();  //{ return ImGui::GetIO().DisplaySize.y / 1080.f; }

};
