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

#include "stdafx.h"
#include "RaytracingSample.h"
#include "dx12/dxr/nv_helpers_dx12/RootSignatureGenerator.h"
#include "Win32Application.h"
#include <array>

namespace RaytracingImplementation
{

	RaytracingSample::RaytracingSample(UINT width, UINT height, std::wstring name) :
		gpu{ width, height },
		m_vertexBufferView{ 0 },
		m_title{ name },
		m_windowWidth{ width },
		m_windowHeight{ height }
	{
		m_windowAspectRatio = static_cast<float>(width) / static_cast<float>(height);
	}

	void RaytracingSample::OnInit()
	{
		SetUpPipeline();

		LoadAssets();

		if (gpu.GetRaytracingSupport())
		{
			gpu.raster = false;

			// Setup the acceleration structures (AS) for raytracing. When setting up
			// geometry, each bottom-level AS has its own transform matrix.
			gpu.CreateAccelerationStructures(m_vertexBuffer);

			gpu.CloseCommandList();

			// Create the raytracing pipeline, associating the shader code to symbol names
			// and to their root signatures, and defining the amount of memory carried by
			// rays (ray payload)
			gpu.CreateRaytracingPipeline(); // #DXR

			// Create the buffer containing the raytracing result (always output in a
			// UAV), and create the heap referencing the resources used by the raytracing,
			// such as the acceleration structure
			gpu.CreateShaderResourceHeap(); // #DXR

			// Create the shader binding table and indicating which shaders
			// are invoked for each instance in the  AS
			gpu.CreateShaderBindingTable(m_vertexBuffer);
		}
	}

	// Load the rendering pipeline dependencies.
	void RaytracingSample::SetUpPipeline()
	{
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.BufferCount = FrameCount;
		swapChainDesc.Width = m_windowWidth;
		swapChainDesc.Height = m_windowHeight;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.SampleDesc.Count = 1;

		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		gpu.Init(queueDesc, swapChainDesc, rtvHeapDesc);
	}

	// Load the sample assets.
	void RaytracingSample::LoadAssets()
	{

		//Describe an empty root signature.
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		const wchar_t* shaderFilePath = L"resources/shaders/shaders.hlsl";

		//Define the vertex input layout.
		std::array< D3D12_INPUT_ELEMENT_DESC, 2> inputElementDescs;
		inputElementDescs.at(0) = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
		inputElementDescs.at(1) = { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };

		//Describe and create the graphics pipeline state object (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = {inputElementDescs.data(), static_cast<UINT>(inputElementDescs.size())};
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;

		gpu.CreatePipelineState(rootSignatureDesc, psoDesc, shaderFilePath, 
			shaderFilePath, inputElementDescs);

		Vertex triangleVertices[] = {
		{{0.0f, 0.25f * m_windowAspectRatio, 0.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},
		{{0.25f, -0.25f * m_windowAspectRatio, 0.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},
		{{-0.25f, -0.25f * m_windowAspectRatio, 0.0f}, {1.0f, 0.0f, 1.0f, 1.0f}} };

		const UINT vertexBufferSize = sizeof(triangleVertices);

		gpu.CreateVertexBuffer(vertexBufferSize, m_vertexBuffer, m_vertexBufferView, 
			triangleVertices, sizeof(triangleVertices));

	}

	// Update frame-based values.
	void RaytracingSample::OnUpdate() {}

	// Render the scene.
	void RaytracingSample::OnRender()
	{
		gpu.PopulateCommandList(m_vertexBufferView);
		gpu.Swap();
	}

	void RaytracingSample::OnDestroy() {}

	void RaytracingSample::OnKeyDown(UINT8) {}

	void RaytracingSample::OnKeyUp(UINT8 key)
	{
		// Alternate between rasterization and raytracing using the spacebar
		if (key == VK_SPACE)
		{
			gpu.raster = !gpu.raster;
		}
	}

	UINT RaytracingSample::GetWidth() const
	{
		return m_windowWidth;
	}

	UINT RaytracingSample::GetHeight() const
	{
		return m_windowHeight;
	}

	const WCHAR* RaytracingSample::GetTitle() const
	{
		return m_title.c_str();
	}

	// Helper function for parsing any supplied command line args.
	_Use_decl_annotations_
		void RaytracingSample::ParseCommandLineArgs(WCHAR* argv[], int argc)
	{
		for (int i = 1; i < argc; ++i)
		{
			if (_wcsnicmp(argv[i], L"-warp", wcslen(argv[i])) == 0 ||
				_wcsnicmp(argv[i], L"/warp", wcslen(argv[i])) == 0)
			{
				gpu.useWarpDevice = true;
				m_title = m_title + L" (WARP)";
			}
		}
	}

	// Helper function for setting the window's title text.
	void RaytracingSample::SetCustomWindowText(LPCWSTR text)
	{
		std::wstring windowText = m_title + L": " + text;
		SetWindowText(Win32Application::GetHwnd(), windowText.c_str());
	}
}