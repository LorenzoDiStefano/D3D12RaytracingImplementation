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

#ifndef DX_SAMPLE_GUARD
#define DX_SAMPLE_GUARD


#pragma once

#include "AccelerationStructureBuffers.h"
#include <dxgi1_2.h>
#include <stdexcept>
#include <vector>
#include "vertex.h"
#include <combaseapi.h>
#include "dx12/dxr/nv_helpers_dx12/ShaderBindingTableGenerator.h"
#include "dx12/dxr/DXRHelper.h"
#include "dx12/dxr/nv_helpers_dx12/TopLevelASGenerator.h"
#include "dx12/dxr/nv_helpers_dx12/BottomLevelASGenerator.h"

namespace RaytracingImplementation
{

	class Dx12Api
	{
	public:
		Dx12Api(UINT width, UINT height);
		Dx12Api() = delete;
		Dx12Api(const Dx12Api&) = delete;

		Dx12Api& operator = (const Dx12Api&) = delete;

		virtual ~Dx12Api();

		// Accessors.
		inline UINT GetViewportWidth() const { return m_viewportWidth; }
		inline UINT GetViewportHeight() const { return m_viewportHeight; }

		void Init(D3D12_COMMAND_QUEUE_DESC&, DXGI_SWAP_CHAIN_DESC1&, D3D12_DESCRIPTOR_HEAP_DESC&);

		//void CreateVertexBuffer(const UINT, Microsoft::WRL::ComPtr<ID3D12Resource>&);
		void CreateVertexBuffer(const UINT vertexBufferSize, Microsoft::WRL::ComPtr<ID3D12Resource>& m_vertexBuffer,
			D3D12_VERTEX_BUFFER_VIEW& m_vertexBufferView, const void* const data, const size_t size);

		void CreatePipelineState(CD3DX12_ROOT_SIGNATURE_DESC&, D3D12_GRAPHICS_PIPELINE_STATE_DESC&, const wchar_t*, const wchar_t*, std::array<D3D12_INPUT_ELEMENT_DESC,2>&);
		void PopulateCommandList(D3D12_VERTEX_BUFFER_VIEW&);
		void Swap();

		/// Create all acceleration structures, bottom and top
		void CreateAccelerationStructures(Microsoft::WRL::ComPtr<ID3D12Resource>&);

		void CreateRaytracingPipeline();
		void CreateShaderResourceHeap();
		void CreateShaderBindingTable(Microsoft::WRL::ComPtr<ID3D12Resource>&);
		void CloseCommandList();
		inline bool GetRaytracingSupport() const { return m_raytracing_support; }

		// Adapter info.
		bool useWarpDevice;

		//Raster change var
		bool m_raster = true;

	private:
		std::wstring GetAssetFullPath(LPCWSTR assetName);
		void GetHardwareAdapter(_In_ IDXGIFactory2* pFactory, _Outptr_result_maybenull_ IDXGIAdapter1** ppAdapter);
		void EnableDebugLayer();
		void WaitForPreviousFrame();

		bool m_raytracing_support = false;

		// Viewport dimensions.
		UINT m_viewportWidth;
		UINT m_viewportHeight;
		float m_viewportAspectRatio;

		// Pipeline objects.
		Microsoft::WRL::ComPtr<ID3D12Device5> m_device;
		CD3DX12_VIEWPORT m_viewport;
		CD3DX12_RECT m_scissorRect;
		Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
		Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTargets[2];
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> m_commandList;
		UINT m_rtvDescriptorSize;

		// Synchronization objects.
		UINT m_frameIndex;
		HANDLE m_fenceEvent;
		Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
		UINT64 m_fenceValue;

		// #DXR
		Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRayGenSignature();
		Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateMissSignature();
		Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateHitSignature();


		Microsoft::WRL::ComPtr<IDxcBlob> m_rayGenLibrary;
		Microsoft::WRL::ComPtr<IDxcBlob> m_hitLibrary;
		Microsoft::WRL::ComPtr<IDxcBlob> m_missLibrary;

		Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rayGenSignature;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> m_hitSignature;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> m_missSignature;

		// Ray tracing pipeline state
		Microsoft::WRL::ComPtr<ID3D12StateObject> m_rtStateObject;
		// Ray tracing pipeline state properties, retaining the shader identifiers
		// to use in the Shader Binding Table
		Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> m_rtStateObjectProps;

		// #DXR
		Microsoft::WRL::ComPtr<ID3D12Resource> m_outputResource;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;

		// #DXR
		NvHelpers::ShaderBindingTableGenerator m_sbtHelper;
		Microsoft::WRL::ComPtr<ID3D12Resource> m_sbtStorage;

		bool CheckRaytracingSupport();
		void CreateCommandQueue(D3D12_COMMAND_QUEUE_DESC&);
		void CreateSwapChain(DXGI_SWAP_CHAIN_DESC1&);
		void CreateRtvResources(D3D12_DESCRIPTOR_HEAP_DESC&);
		void WaitUploadVertexBuffer();
		void CreateRaytracingOutputBuffer();

		// Root assets path.
		std::wstring m_assetsPath;

		//
		Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
		UINT dxgiFactoryFlags = 0;

		NvHelpers::TopLevelASGenerator m_topLevelASGenerator;
		std::vector<std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> m_instances;

		/// Create the acceleration structure of an instance
		///
		/// \param     vVertexBuffers : pair of buffer and vertex count
		/// \return    AccelerationStructureBuffers for TLAS
		AccelerationStructureBuffers CreateBottomLevelAS(
			std::vector<std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers);

		/// Create the main acceleration structure that holds
		/// all instances of the scene
		/// \param     instances : pair of BLAS and transform
		void CreateTopLevelAS(
			const std::vector<std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances,
			AccelerationStructureBuffers& m_topLevelASBuffers);

		Microsoft::WRL::ComPtr<ID3D12Resource> m_bottomLevelAS; // Storage for the bottom Level AS
		AccelerationStructureBuffers m_topLevelASBuffers;

	};
}

#endif // !DX_SAMPLE_GUARD