#include "stdafx.h"
#include "win32App.h"
#include <hidusage.h>

namespace
{
	void LockCursorToClient(HWND hwnd)
	{
		RECT clientRect = {};
		GetClientRect(hwnd, &clientRect);

		POINT topLeft = { clientRect.left, clientRect.top };
		POINT bottomRight = { clientRect.right, clientRect.bottom };

		ClientToScreen(hwnd, &topLeft);
		ClientToScreen(hwnd, &bottomRight);

		RECT clipRect = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
		ClipCursor(&clipRect);
		ShowCursor(FALSE);
	}

	void UnlockCursor()
	{
		ClipCursor(nullptr);
		ShowCursor(TRUE);
	}
}

HWND Win32Application::m_hwnd = nullptr;
Timer Win32Application::m_timer = Timer();
bool Win32Application::m_appPaused = false;

int Win32Application::Run(D3DApp* pApp, HINSTANCE hInstance, int nCmdShow) {
	// Make process DPI aware and obtain main monitor scale
	ImGui_ImplWin32_EnableDpiAwareness();
	float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

	int argc;

	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	pApp->ParseCommandLineArgs(argv, argc);
	LocalFree(argv);

	// Initialize the window class.
	WNDCLASSEX windowClass = { 0 };
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName = L"DXSampleClass";
	RegisterClassEx(&windowClass);

	RECT windowRect = { 0, 0, static_cast<LONG>(pApp->GetWidth() * main_scale), static_cast<LONG>(pApp->GetHeight() * main_scale) };
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	// Create the window and store a handle to it.
	m_hwnd = CreateWindow(
		windowClass.lpszClassName,
		pApp->GetTitle(),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,        // We have no parent window.
		nullptr,        // We aren't using menus.
		hInstance,
		pApp);

	RAWINPUTDEVICE rid = {};
	rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
	rid.usUsage = HID_USAGE_GENERIC_MOUSE;
	rid.dwFlags = 0;
	rid.hwndTarget = m_hwnd;
	ThrowIfFailed(RegisterRawInputDevices(&rid, 1, sizeof(rid)) ? S_OK : HRESULT_FROM_WIN32(GetLastError()));

	// Initialize the D3D App. OnInit is defined in each child-implementation of D3DApp.
	pApp->OnInit();

	ShowWindow(m_hwnd, nCmdShow);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Setup scaling
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(m_hwnd);

	ImGui_ImplDX12_InitInfo init_info = pApp->GetImGuiInitInfo();
	ImGui_ImplDX12_Init(&init_info);

	SetForegroundWindow(m_hwnd);
	SetFocus(m_hwnd);
	LockCursorToClient(m_hwnd);

	// Main loop.
	MSG msg = {};

	m_timer.Reset();

	while (msg.message != WM_QUIT)
	{
		// Process any messages in the queue.
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			m_timer.Tick();

			if (!m_appPaused) {
				pApp->OnUpdate(m_timer);
				pApp->OnRender(m_timer);
			}
			else
			{
				Sleep(100);
			}
		}
	}

	UnlockCursor();
	pApp->OnDestroy();

	// Return this part of the WM_QUIT message to Windows.
	return static_cast<char>(msg.wParam);
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Main message handler for the sample.
LRESULT CALLBACK Win32Application::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui::GetCurrentContext() != nullptr && ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
	{
		return TRUE;
	}

	D3DApp* pApp = reinterpret_cast<D3DApp*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

	switch (message)
	{
	case WM_ACTIVATE:
		if (LOWORD(wParam) == WA_INACTIVE)
		{
			m_appPaused = true;
			m_timer.Stop();
		}
		else
		{
			m_appPaused = false;
			m_timer.Start();
		}
		return 0;

	case WM_CREATE:
	{
		LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
	}
	return 0;

	case WM_KEYDOWN:
		if (pApp)
		{
			pApp->OnKeyDown(static_cast<UINT8>(wParam));
		}
		return 0;

	case WM_KEYUP:
		if (pApp)
		{
			pApp->OnKeyUp(static_cast<UINT8>(wParam));
		}
		return 0;

	case WM_MOUSEMOVE:
		return 0;

	case WM_INPUT:
		if (pApp && !pApp->IsMenuVisible())
		{
			UINT size = 0;
			if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == 0 && size > 0)
			{
				std::vector<BYTE> buffer(size);
				if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) == size)
				{
					const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(buffer.data());
					if (raw->header.dwType == RIM_TYPEMOUSE)
					{
						pApp->OnMouseRawDelta(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
					}
				}
			}
		}
		return 0;

	case WM_SETCURSOR:
		if (pApp && pApp->IsMenuVisible())
		{
			SetCursor(LoadCursor(nullptr, IDC_ARROW));
		}
		else
		{
			SetCursor(nullptr);
		}
		return TRUE;

	case WM_ENTERSIZEMOVE:
		m_appPaused = true;
		m_timer.Stop();
		return 0;

	case WM_EXITSIZEMOVE:
		m_appPaused = false;
		m_timer.Start();
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}