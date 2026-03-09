#pragma once

#include "d3dApp.h"
#include "timer.h"

class D3DApp;

class Win32Application {
public:
	static int Run(D3DApp* pApp, HINSTANCE hInstance, int nCmdShow);
	static HWND GetHwnd() { return m_hwnd; }

protected:
	static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static Timer m_timer;

	static bool m_appPaused;


private:
	static HWND m_hwnd;
};
