#pragma once

#include "d3dApp.h"

class D3DApp;

class Win32Application {
public:
	static int Run(D3DApp* pApp, HINSTANCE hInstance, int nCmdShow);
	static HWND GetHwnd() { return m_hwnd; }

protected:
	static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
	static HWND m_hwnd;
};
