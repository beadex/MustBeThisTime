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

	RECT windowRect = { 0, 0, static_cast<LONG>(pApp->GetWidth()), static_cast<LONG>(pApp->GetHeight()) };
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

// Main message handler for the sample.
LRESULT CALLBACK Win32Application::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	D3DApp* pApp = reinterpret_cast<D3DApp*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

	switch (message)
	{
		// WM_ACTIVATE is sent when the window is activated or deactivated.  
		// We pause the game when the window is deactivated and unpause it 
		// when it becomes active.  
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
		// Save the D3DApp* passed in to CreateWindow.
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
		if (pApp)
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
		SetCursor(nullptr);
		return TRUE;

		// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
	case WM_ENTERSIZEMOVE:
		m_appPaused = true;
		m_timer.Stop();
		return 0;

		// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
		// Here we reset everything based on the new window dimensions.
	case WM_EXITSIZEMOVE:
		m_appPaused = false;
		m_timer.Start();
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	// Handle any messages the switch statement didn't.
	return DefWindowProc(hWnd, message, wParam, lParam);
}