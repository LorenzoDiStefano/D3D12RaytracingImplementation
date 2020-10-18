//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#ifndef WIN32_APPLICATION_GUARD
#define WIN32_APPLICATION_GUARD

#pragma once

#include "IApplication.h"

namespace RaytracingImplementation
{

	class Win32Application
	{
	public:
		static int Run(IApplication* pSample, HINSTANCE hInstance, int nCmdShow);
		static HWND GetHwnd() { return m_hwnd; }

	protected:
		static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	private:
		static HWND m_hwnd;
	};
}

#endif // !WIN32_APPLICATION_GUARD