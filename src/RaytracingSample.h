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

#ifndef RAY_TRACING_SAMPLE
#define RAY_TRACING_SAMPLE

#pragma once

#include "IApplication.h"
#include "dx12/Dx12Api.h"

namespace RaytracingImplementation
{

	class RaytracingSample : public IApplication
	{
	public:
		RaytracingSample(UINT width, UINT height, std::wstring name);

		virtual void OnInit();
		virtual void OnUpdate();
		virtual void OnRender();
		virtual void OnDestroy();

		// Samples override the event handlers to handle specific messages.
		virtual void OnKeyDown(UINT8 key);
		virtual void OnKeyUp(UINT8 key);

		// Accessors.
		virtual UINT GetWidth() const;
		virtual UINT GetHeight() const;
		virtual const WCHAR* GetTitle() const;

		virtual void ParseCommandLineArgs(_In_reads_(argc) WCHAR* argv[], int argc);

	private:

		void SetUpPipeline();
		void LoadAssets();
		void SetCustomWindowText(LPCWSTR text);

		// Window vars
		std::wstring m_title;
		UINT m_windowWidth;
		UINT m_windowHeight;
		float m_windowAspectRatio;

		// App resources.
		static const UINT FrameCount = 2;
		Dx12Api gpu;
		Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
		D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

	};
}

#endif // !RAY_TRACING_SAMPLE