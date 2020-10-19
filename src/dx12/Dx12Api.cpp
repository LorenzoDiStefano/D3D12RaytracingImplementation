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
#include <array>
#include "Dx12Api.h"
#include "dx12/dxr/nv_helpers_dx12/RaytracingPipelineGenerator.h"   
#include "dx12/dxr/nv_helpers_dx12/RootSignatureGenerator.h"
#include "Win32Application.h"

namespace RaytracingImplementation
{

	Dx12Api::Dx12Api(UINT width, UINT height) :
		m_viewportWidth(width),
		m_viewportHeight(height),
		useWarpDevice(false),
		m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
		m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
		m_rtvDescriptorSize(0),
		m_frameIndex(0)
	{
		WCHAR assetsPath[512];
		GetAssetsPath(assetsPath, _countof(assetsPath));
		m_assetsPath = assetsPath;

		m_viewportAspectRatio = static_cast<float>(width) / static_cast<float>(height);
	}

	Dx12Api::~Dx12Api()
	{
		// Ensure that the GPU is no longer referencing resources that are about to be
		// cleaned up by the destructor.
		WaitForPreviousFrame();

		CloseHandle(m_fenceEvent);
	}

	// Helper function for resolving the full path of assets.
	std::wstring Dx12Api::GetAssetFullPath(LPCWSTR assetName)
	{
		return m_assetsPath + assetName;
	}

	// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
	// If no such adapter can be found, *ppAdapter will be set to nullptr.
	_Use_decl_annotations_
		void Dx12Api::GetHardwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter)
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
		*ppAdapter = nullptr;

		for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Don't select the Basic Render Driver adapter.
				// If you want a software adapter, pass in "/warp" on the command line.
				continue;
			}

			// Check to see if the adapter supports Direct3D 12, but don't create the
			// actual device yet.
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
			{
				break;
			}
		}

		*ppAdapter = adapter.Detach();
	}

	void Dx12Api::Init(
		D3D12_COMMAND_QUEUE_DESC& queueDesc, DXGI_SWAP_CHAIN_DESC1& swapChainDesc, D3D12_DESCRIPTOR_HEAP_DESC& rtvHeapDesc)
	{
		EnableDebugLayer();

		ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

		if (useWarpDevice)
		{
			Microsoft::WRL::ComPtr<IDXGIAdapter> warpAdapter;
			ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

			ThrowIfFailed(D3D12CreateDevice(
				warpAdapter.Get(),
				D3D_FEATURE_LEVEL_12_1,
				IID_PPV_ARGS(&m_device)
			));
		}
		else
		{
			Microsoft::WRL::ComPtr<IDXGIAdapter1> hardwareAdapter;
			GetHardwareAdapter(factory.Get(), &hardwareAdapter);

			ThrowIfFailed(D3D12CreateDevice(
				hardwareAdapter.Get(),
				D3D_FEATURE_LEVEL_12_1,
				IID_PPV_ARGS(&m_device)
			));
		}

		// Check the raytracing capabilities of the device
		m_raytracing_support = CheckRaytracingSupport();

		CreateCommandQueue(queueDesc);

		CreateSwapChain(swapChainDesc);

		CreateRtvResources(rtvHeapDesc);

		ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
	}

	void Dx12Api::CreateVertexBuffer(
		const UINT vertexBufferSize, Microsoft::WRL::ComPtr<ID3D12Resource>& m_vertexBuffer,
		D3D12_VERTEX_BUFFER_VIEW& m_vertexBufferView, const void* const data, const size_t size)
	{
		// Note: using upload heaps to transfer static data like vert buffers is not 
		// recommended. Every time the GPU needs it, the upload heap will be marshalled 
		// over. Please read up on Default Heap usage. An upload heap is used here for 
		// code simplicity and because there are very few verts to actually transfer.
		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_vertexBuffer)));

		// Copy the triangle data to the vertex buffer.
		UINT8* pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
		ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, data, size);
		m_vertexBuffer->Unmap(0, nullptr);

		// Initialize the vertex buffer view.
		m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferView.StrideInBytes = sizeof(Vertex);
		m_vertexBufferView.SizeInBytes = vertexBufferSize;

		WaitUploadVertexBuffer();
	}

	void Dx12Api::WaitUploadVertexBuffer()
	{
		// Create synchronization objects and wait until assets have been uploaded to the GPU.
		ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
		m_fenceValue = 1;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForPreviousFrame();
	}

	bool Dx12Api::CheckRaytracingSupport()
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
		ThrowIfFailed(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
			&options5, sizeof(options5)));
		if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
			return false;
		else
			return true;
	}

	void Dx12Api::CreateCommandQueue(D3D12_COMMAND_QUEUE_DESC& queueDesc)
	{
		ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
	}

	void Dx12Api::CreateSwapChain(DXGI_SWAP_CHAIN_DESC1& swapChainDesc)
	{
		Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
		ThrowIfFailed(factory->CreateSwapChainForHwnd(
			m_commandQueue.Get(),		// Swap chain needs the queue so that it can force a flush on it.
			Win32Application::GetHwnd(),
			&swapChainDesc,
			nullptr,
			nullptr,
			&swapChain
		));

		// This sample does not support fullscreen transitions.
		ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

		ThrowIfFailed(swapChain.As(&m_swapChain));
		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	}

	void Dx12Api::CreateRtvResources(D3D12_DESCRIPTOR_HEAP_DESC& rtvHeapDesc)
	{
		// Create descriptor heaps.

		ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// Create frame resources.

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV for each frame.
		for (UINT n = 0; n < rtvHeapDesc.NumDescriptors; n++)
		{
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorSize);
		}

	}

	void Dx12Api::EnableDebugLayer()
	{
#if defined(_DEBUG)
		// Enable the debug layer (requires the Graphics Tools "optional feature").
		// NOTE: Enabling the debug layer after device creation will invalidate the active device.
		{
			Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
			{
				debugController->EnableDebugLayer();

				// Enable additional debug layers.
				dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
			}
		}
#endif
	}

	void Dx12Api::WaitForPreviousFrame()
	{
		// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
		// This is code implemented as such for simplicity. The sample illustrates 
		//how to use fences for efficient resource usage and to maximize GPU utilization.

		// Signal and increment the fence value.
		const UINT64 fence = m_fenceValue;
		ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
		m_fenceValue++;

		// Wait until the previous frame is finished.
		if (m_fence->GetCompletedValue() < fence)
		{
			ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}

		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	}

	void Dx12Api::CreatePipelineState(CD3DX12_ROOT_SIGNATURE_DESC& rootSignatureDesc, 
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc, const wchar_t* vertexShaderPath, const wchar_t* pixelShaderPath,
		std::array< D3D12_INPUT_ELEMENT_DESC,2>& inputElementDescs)
	{

		Microsoft::WRL::ComPtr<ID3DBlob> signature;
		Microsoft::WRL::ComPtr<ID3DBlob> error;
		ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));

		// Create the pipeline state, which includes compiling and loading shaders.
		Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
		Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif

		ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(vertexShaderPath).c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
		ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(pixelShaderPath).c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());

		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

		// Create the command list.
		ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));
	}

	//-----------------------------------------------------------------------------
	// The ray generation shader needs to access 2 resources: the raytracing output
	// and the top-level acceleration structure
	//
	Microsoft::WRL::ComPtr<ID3D12RootSignature> Dx12Api::CreateRayGenSignature()
	{
		NvHelpers::RootSignatureGenerator rsc;
		rsc.AddHeapRangesParameter(
			{ {0 /*u0*/, 1 /*1 descriptor */, 0 /*use the implicit register space 0*/,
			  D3D12_DESCRIPTOR_RANGE_TYPE_UAV /* UAV representing the output buffer*/,
			  0 /*heap slot where the UAV is defined*/},
			 {0 /*t0*/, 1, 0,
			  D3D12_DESCRIPTOR_RANGE_TYPE_SRV /*Top-level acceleration structure*/,
			  1} });

		return rsc.Generate(m_device.Get(), true);
	}

	//-----------------------------------------------------------------------------
	// The hit shader communicates only through the ray payload, and therefore does
	// not require any resources
	//
	Microsoft::WRL::ComPtr<ID3D12RootSignature> Dx12Api::CreateHitSignature()
	{
		NvHelpers::RootSignatureGenerator rsc;
		rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV);
		return rsc.Generate(m_device.Get(), true);
	}

	//-----------------------------------------------------------------------------
	// The miss shader communicates only through the ray payload, and therefore
	// does not require any resources
	//
	Microsoft::WRL::ComPtr<ID3D12RootSignature> Dx12Api::CreateMissSignature()
	{
		NvHelpers::RootSignatureGenerator rsc;
		return rsc.Generate(m_device.Get(), true);
	}

	//-----------------------------------------------------------------------------
	//
	// The raytracing pipeline binds the shader code, root signatures and pipeline
	// characteristics in a single structure used by DXR to invoke the shaders and
	// manage temporary memory during raytracing
	//
	//
	void Dx12Api::CreateRaytracingPipeline()
	{
		NvHelpers::RayTracingPipelineGenerator pipeline(m_device.Get());

		// The pipeline contains the DXIL code of all the shaders potentially executed
		// during the raytracing process. This section compiles the HLSL code into a
		// set of DXIL libraries. We chose to separate the code in several libraries
		// by semantic (ray generation, hit, miss) for clarity. Any code layout can be
		// used.
		m_rayGenLibrary = NvHelpers::CompileShaderLibrary(L"resources/shaders/raytracing/RayGen.hlsl");
		m_missLibrary = NvHelpers::CompileShaderLibrary(L"resources/shaders/raytracing/Miss.hlsl");
		m_hitLibrary = NvHelpers::CompileShaderLibrary(L"resources/shaders/raytracing/Hit.hlsl");
		// In a way similar to DLLs, each library is associated with a number of
		// exported symbols. This
		// has to be done explicitly in the lines below. Note that a single library
		// can contain an arbitrary number of symbols, whose semantic is given in HLSL
		// using the [shader("xxx")] syntax
		pipeline.AddLibrary(m_rayGenLibrary.Get(), { L"RayGen" });
		pipeline.AddLibrary(m_missLibrary.Get(), { L"Miss" });
		pipeline.AddLibrary(m_hitLibrary.Get(), { L"ClosestHit" });
		// To be used, each DX12 shader needs a root signature defining which
		// parameters and buffers will be accessed.
		m_rayGenSignature = CreateRayGenSignature();
		m_missSignature = CreateMissSignature();
		m_hitSignature = CreateHitSignature();
		// 3 different shaders can be invoked to obtain an intersection: an
		// intersection shader is called
		// when hitting the bounding box of non-triangular geometry. This is beyond
		// the scope of this tutorial. An any-hit shader is called on potential
		// intersections. This shader can, for example, perform alpha-testing and
		// discard some intersections. Finally, the closest-hit program is invoked on
		// the intersection point closest to the ray origin. Those 3 shaders are bound
		// together into a hit group.

		// Note that for triangular geometry the intersection shader is built-in. An
		// empty any-hit shader is also defined by default, so in our simple case each
		// hit group contains only the closest hit shader. Note that since the
		// exported symbols are defined above the shaders can be simply referred to by
		// name.

		// Hit group for the triangles, with a shader simply interpolating vertex
		// colors
		pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");
		// The following section associates the root signature to each shader. Note
		// that we can explicitly show that some shaders share the same root signature
		// (eg. Miss and ShadowMiss). Note that the hit shaders are now only referred
		// to as hit groups, meaning that the underlying intersection, any-hit and
		// closest-hit shaders share the same root signature.
		pipeline.AddRootSignatureAssociation(m_rayGenSignature.Get(), { L"RayGen" });
		pipeline.AddRootSignatureAssociation(m_missSignature.Get(), { L"Miss" });
		pipeline.AddRootSignatureAssociation(m_hitSignature.Get(), { L"HitGroup" });
		// The payload size defines the maximum size of the data carried by the rays,
		// ie. the the data
		// exchanged between shaders, such as the HitInfo structure in the HLSL code.
		// It is important to keep this value as low as possible as a too high value
		// would result in unnecessary memory consumption and cache trashing.
		pipeline.SetMaxPayloadSize(4 * sizeof(float)); // RGB + distance

		// Upon hitting a surface, DXR can provide several attributes to the hit. In
		// our sample we just use the barycentric coordinates defined by the weights
		// u,v of the last two vertices of the triangle. The actual barycentrics can
		// be obtained using float3 barycentrics = float3(1.f-u-v, u, v);
		pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates

		// The raytracing process can shoot rays from existing hit points, resulting
		// in nested TraceRay calls. Our sample code traces only primary rays, which
		// then requires a trace depth of 1. Note that this recursion depth should be
		// kept to a minimum for best performance. Path tracing algorithms can be
		// easily flattened into a simple loop in the ray generation.
		pipeline.SetMaxRecursionDepth(1);
		// Compile the pipeline for execution on the GPU
		m_rtStateObject = pipeline.Generate();

		// Cast the state object into a properties object, allowing to later access
		// the shader pointers by name
		ThrowIfFailed(m_rtStateObject->QueryInterface(IID_PPV_ARGS(&m_rtStateObjectProps)));

		// Allocate the buffer storing the raytracing output, with the same dimensions
		// as the target image
		CreateRaytracingOutputBuffer();
	}

	//-----------------------------------------------------------------------------
	//
	// Allocate the buffer holding the raytracing output, with the same size as the
	// output image
	//
	void Dx12Api::CreateRaytracingOutputBuffer()
	{
		D3D12_RESOURCE_DESC resDesc = {};
		resDesc.DepthOrArraySize = 1;
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		// The backbuffer is actually DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, but sRGB
		// formats cannot be used with UAVs. For accuracy we should convert to sRGB
		// ourselves in the shader
		resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

		resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		resDesc.Width = GetViewportWidth();
		resDesc.Height = GetViewportHeight();
		resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resDesc.MipLevels = 1;
		resDesc.SampleDesc.Count = 1;
		ThrowIfFailed(m_device->CreateCommittedResource(
			&NvHelpers::kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
			D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
			IID_PPV_ARGS(&m_outputResource)));
	}

	//-----------------------------------------------------------------------------
	//
	// Create the main heap used by the shaders, which will give access to the
	// raytracing output and the top-level acceleration structure
	//
	void Dx12Api::CreateShaderResourceHeap() 
	{
		// Create a SRV/UAV/CBV descriptor heap. We need 2 entries - 1 UAV for the
		// raytracing output and 1 SRV for the TLAS
		m_srvUavHeap = NvHelpers::CreateDescriptorHeap(
			m_device.Get(), 2, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

		// Get a handle to the heap memory on the CPU side, to be able to write the
		// descriptors directly
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
			m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();

		// Create the UAV. Based on the root signature we created it is the first
		// entry. The Create*View methods write the view information directly into
		// srvHandle
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		m_device->CreateUnorderedAccessView(m_outputResource.Get(), nullptr, &uavDesc,
			srvHandle);

		// Add the Top Level AS SRV right after the raytracing output buffer
		srvHandle.ptr += m_device->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.RaytracingAccelerationStructure.Location =
			m_topLevelASBuffers.pResult->GetGPUVirtualAddress();
		// Write the acceleration structure view in the heap
		m_device->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);
	}

	//-----------------------------------------------------------------------------
	//
	// The Shader Binding Table (SBT) is the cornerstone of the raytracing setup:
	// this is where the shader resources are bound to the shaders, in a way that
	// can be interpreted by the raytracer on GPU. In terms of layout, the SBT
	// contains a series of shader IDs with their resource pointers. The SBT
	// contains the ray generation shader, the miss shaders, then the hit groups.
	// Using the helper class, those can be specified in arbitrary order.
	//
	void Dx12Api::CreateShaderBindingTable(Microsoft::WRL::ComPtr<ID3D12Resource>& m_vertexBuffer)
	{
		// The SBT helper class collects calls to Add*Program.  If called several
		// times, the helper must be emptied before re-adding shaders.
		m_sbtHelper.Reset();
		m_sbtHelper.AddHitGroup(L"HitGroup", { (void*)(m_vertexBuffer->GetGPUVirtualAddress()) });
		// The pointer to the beginning of the heap is the only parameter required by
		// shaders without root parameters
		D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle =
			m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
		// The helper treats both root parameter pointers and heap pointers as void*,
		// while DX12 uses the
		// D3D12_GPU_DESCRIPTOR_HANDLE to define heap pointers. The pointer in this
		// struct is a UINT64, which then has to be reinterpreted as a pointer.
		auto heapPointer = reinterpret_cast<UINT64*>(srvUavHeapHandle.ptr);
		// The ray generation only uses heap data
		m_sbtHelper.AddRayGenerationProgram(L"RayGen", { heapPointer });

		// The miss and hit shaders do not access any external resources: instead they
		// communicate their results through the ray payload
		m_sbtHelper.AddMissProgram(L"Miss", {});

		// Adding the triangle hit shader
		m_sbtHelper.AddHitGroup(L"HitGroup", {});
		// Compute the size of the SBT given the number of shaders and their
		// parameters
		uint32_t sbtSize = m_sbtHelper.ComputeSBTSize();

		// Create the SBT on the upload heap. This is required as the helper will use
		// mapping to write the SBT contents. After the SBT compilation it could be
		// copied to the default heap for performance.
		m_sbtStorage = NvHelpers::CreateBuffer(
			m_device.Get(), sbtSize, D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_GENERIC_READ, NvHelpers::kUploadHeapProps);
		if (!m_sbtStorage) {
			throw std::logic_error("Could not allocate the shader binding table");
		}
		// Compile the SBT from the shader and parameters info
		m_sbtHelper.Generate(m_sbtStorage.Get(), m_rtStateObjectProps.Get());
	}

	void Dx12Api::CloseCommandList()
	{
		// Command lists are created in the recording state, but there is
		// nothing to record yet. The main loop expects it to be closed, so
		// close it now.
		ThrowIfFailed(m_commandList->Close());
	}

	//Bootleg temporary "draw"
	// Record all the commands we need to render the scene into the command list.

	void Dx12Api::PopulateCommandList(D3D12_VERTEX_BUFFER_VIEW& m_vertexBufferView)
	{
		// Command list allocators can only be reset when the associated 
		// command lists have finished execution on the GPU; apps should use 
		// fences to determine GPU execution progress.
		ThrowIfFailed(m_commandAllocator->Reset());

		// However, when ExecuteCommandList() is called on a particular command 
		// list, that command list can then be reset at any time and must be before 
		// re-recording.
		ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

		// Set necessary state.
		m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
		m_commandList->RSSetViewports(1, &m_viewport);
		m_commandList->RSSetScissorRects(1, &m_scissorRect);

		// Indicate that the back buffer will be used as a render target.
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
		m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

		// Record commands.
		// #DXR
		if (raster)
		{
			const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
			m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
			m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
			m_commandList->DrawInstanced(3, 1, 0, 0);
		}
		else
		{
			// #DXR
			// Bind the descriptor heap giving access to the top-level acceleration
			// structure, as well as the raytracing output
			std::vector<ID3D12DescriptorHeap*> heaps = { m_srvUavHeap.Get() };
			m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()),
				heaps.data());
			// On the last frame, the raytracing output was used as a copy source, to
			// copy its contents into the render target. Now we need to transition it to
			// a UAV so that the shaders can write in it.
			CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
				m_outputResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			m_commandList->ResourceBarrier(1, &transition);
			// Setup the raytracing task
			D3D12_DISPATCH_RAYS_DESC desc = {};
			// The layout of the SBT is as follows: ray generation shader, miss
			// shaders, hit groups. As described in the CreateShaderBindingTable method,
			// all SBT entries of a given type have the same size to allow a fixed stride.

			// The ray generation shaders are always at the beginning of the SBT. 
			uint32_t rayGenerationSectionSizeInBytes = m_sbtHelper.GetRayGenSectionSize();
			desc.RayGenerationShaderRecord.StartAddress = m_sbtStorage->GetGPUVirtualAddress();
			desc.RayGenerationShaderRecord.SizeInBytes = rayGenerationSectionSizeInBytes;
			// The miss shaders are in the second SBT section, right after the ray
			// generation shader. We have one miss shader for the camera rays and one
			// for the shadow rays, so this section has a size of 2*m_sbtEntrySize. We
			// also indicate the stride between the two miss shaders, which is the size
			// of a SBT entry
			uint32_t missSectionSizeInBytes = m_sbtHelper.GetMissSectionSize();
			desc.MissShaderTable.StartAddress =
				m_sbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes;
			desc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
			desc.MissShaderTable.StrideInBytes = m_sbtHelper.GetMissEntrySize();
			// The hit groups section start after the miss shaders. In this sample we
			// have one 1 hit group for the triangle
			uint32_t hitGroupsSectionSize = m_sbtHelper.GetHitGroupSectionSize();
			desc.HitGroupTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() +
				rayGenerationSectionSizeInBytes +
				missSectionSizeInBytes;
			desc.HitGroupTable.SizeInBytes = hitGroupsSectionSize;
			desc.HitGroupTable.StrideInBytes = m_sbtHelper.GetHitGroupEntrySize();
			// Dimensions of the image to render, identical to a kernel launch dimension
			desc.Width = m_viewportWidth;
			desc.Height = m_viewportHeight;
			desc.Depth = 1;

			// Bind the raytracing pipeline
			m_commandList->SetPipelineState1(m_rtStateObject.Get());
			// Dispatch the rays and write to the raytracing output
			m_commandList->DispatchRays(&desc);

			// The raytracing output needs to be copied to the actual render target used
			// for display. For this, we need to transition the raytracing output from a
			// UAV to a copy source, and the render target buffer to a copy destination.
			// We can then do the actual copy, before transitioning the render target
			// buffer into a render target, that will be then used to display the image
			transition = CD3DX12_RESOURCE_BARRIER::Transition(
				m_outputResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_COPY_SOURCE);
			m_commandList->ResourceBarrier(1, &transition);
			transition = CD3DX12_RESOURCE_BARRIER::Transition(
				m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_COPY_DEST);
			m_commandList->ResourceBarrier(1, &transition);

			m_commandList->CopyResource(m_renderTargets[m_frameIndex].Get(),
				m_outputResource.Get());

			transition = CD3DX12_RESOURCE_BARRIER::Transition(
				m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_RENDER_TARGET);
			m_commandList->ResourceBarrier(1, &transition);
		}

		// Indicate that the back buffer will now be used to present.
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

		ThrowIfFailed(m_commandList->Close());
	}

	void Dx12Api::Swap()
	{
		// Execute the command list.
		ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
		m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

		// Present the frame.
		ThrowIfFailed(m_swapChain->Present(1, 0));

		WaitForPreviousFrame();
	}

	//-----------------------------------------------------------------------------
	//
	// Create a bottom-level acceleration structure based on a list of vertex
	// buffers in GPU memory along with their vertex count. The build is then done
	// in 3 steps: gathering the geometry, computing the sizes of the required
	// buffers, and building the actual AS
	//
	AccelerationStructureBuffers Dx12Api::CreateBottomLevelAS(
		std::vector<std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers)
	{
		NvHelpers::BottomLevelASGenerator bottomLevelAS;

		// Adding all vertex buffers and not transforming their position.
		for (const auto& buffer : vVertexBuffers)
		{
			bottomLevelAS.AddVertexBuffer(buffer.first.Get(), 0, buffer.second, sizeof(Vertex), 0, 0);
		}

		// The AS build requires some scratch space to store temporary information.
		// The amount of scratch memory is dependent on the scene complexity.
		UINT64 scratchSizeInBytes = 0;
		// The final AS also needs to be stored in addition to the existing vertex
		// buffers. It size is also dependent on the scene complexity.
		UINT64 resultSizeInBytes = 0;

		bottomLevelAS.ComputeASBufferSizes(m_device.Get(), false, &scratchSizeInBytes, &resultSizeInBytes);

		// Once the sizes are obtained, the application is responsible for allocating
		// the necessary buffers. Since the entire generation will be done on the GPU,
		// we can directly allocate those on the default heap
		AccelerationStructureBuffers buffers;

		buffers.pScratch = NvHelpers::CreateBuffer(
			m_device.Get(), scratchSizeInBytes,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON,
			NvHelpers::kDefaultHeapProps);

		buffers.pResult = NvHelpers::CreateBuffer(
			m_device.Get(), resultSizeInBytes,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			NvHelpers::kDefaultHeapProps);

		// Build the acceleration structure. Note that this call integrates a barrier
		// on the generated AS, so that it can be used to compute a top-level AS right
		// after this method.
		bottomLevelAS.Generate(m_commandList.Get(), buffers.pScratch.Get(), buffers.pResult.Get(), false, nullptr);

		return buffers;
	}

	//-----------------------------------------------------------------------------
	// Create the main acceleration structure that holds all instances of the scene.
	// Similarly to the bottom-level AS generation, it is done in 3 steps: gathering
	// the instances, computing the memory requirements for the AS, and building the
	// AS itself
	//
	void Dx12Api::CreateTopLevelAS(
		const std::vector<std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances,
		AccelerationStructureBuffers& m_topLevelASBuffers) // pair of bottom level AS and matrix of the instance
	{
		// Gather all the instances into the builder helper
		for (size_t i = 0; i < instances.size(); i++)
		{
			m_topLevelASGenerator.AddInstance(instances[i].first.Get(),
				instances[i].second, static_cast<UINT>(i),
				static_cast<UINT>(0));
		}

		// As for the bottom-level AS, the building the AS requires some scratch space
		// to store temporary data in addition to the actual AS. In the case of the
		// top-level AS, the instance descriptors also need to be stored in GPU
		// memory. This call outputs the memory requirements for each (scratch,
		// results, instance descriptors) so that the application can allocate the
		// corresponding memory
		UINT64 scratchSize, resultSize, instanceDescsSize;

		m_topLevelASGenerator.ComputeASBufferSizes(m_device.Get(), true, &scratchSize, &resultSize, &instanceDescsSize);

		// Create the scratch and result buffers. Since the build is all done on GPU,
		// those can be allocated on the default heap
		m_topLevelASBuffers.pScratch = NvHelpers::CreateBuffer(
			m_device.Get(), scratchSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			NvHelpers::kDefaultHeapProps);

		m_topLevelASBuffers.pResult = NvHelpers::CreateBuffer(
			m_device.Get(), resultSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			NvHelpers::kDefaultHeapProps);

		// The buffer describing the instances: ID, shader binding information,
		// matrices ... Those will be copied into the buffer by the helper through
		// mapping, so the buffer has to be allocated on the upload heap.
		m_topLevelASBuffers.pInstanceDesc = NvHelpers::CreateBuffer(
			m_device.Get(), instanceDescsSize, D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_GENERIC_READ, NvHelpers::kUploadHeapProps);

		// After all the buffers are allocated, or if only an update is required, we
		// can build the acceleration structure. Note that in the case of the update
		// we also pass the existing AS as the 'previous' AS, so that it can be
		// refitted in place.
		m_topLevelASGenerator.Generate(m_commandList.Get(),
			m_topLevelASBuffers.pScratch.Get(),
			m_topLevelASBuffers.pResult.Get(),
			m_topLevelASBuffers.pInstanceDesc.Get());
	}

	//-----------------------------------------------------------------------------
	//
	// Combine the BLAS and TLAS builds to construct the entire acceleration
	// structure required to raytrace the scene
	//
	void Dx12Api::CreateAccelerationStructures(Microsoft::WRL::ComPtr<ID3D12Resource>& m_vertexBuffer)
	{
		// Build the bottom AS from the Triangle vertex buffer
		AccelerationStructureBuffers bottomLevelBuffers =
			CreateBottomLevelAS({ {m_vertexBuffer.Get(), 3} });


		// Just one instance for now
		m_instances = { {bottomLevelBuffers.pResult,DirectX::XMMatrixIdentity()} };
		CreateTopLevelAS(m_instances, m_topLevelASBuffers);

		// Flush the command list and wait for it to finish
		m_commandList->Close();
		ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
		m_commandQueue->ExecuteCommandLists(1, ppCommandLists);
		m_fenceValue++;
		m_commandQueue->Signal(m_fence.Get(), m_fenceValue);

		m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
		WaitForSingleObject(m_fenceEvent, INFINITE);

		// Once the command list is finished executing, reset it to be reused for
		// rendering
		ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

		// Store the AS buffers. The rest of the buffers will be released once we exit
		// the function
		m_bottomLevelAS = bottomLevelBuffers.pResult;
	}
}