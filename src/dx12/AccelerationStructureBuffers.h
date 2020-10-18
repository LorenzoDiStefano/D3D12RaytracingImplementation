#ifndef ACCELERATION_STRUCTURE_BUFFERS_GUARD
#define ACCELERATION_STRUCTURE_BUFFERS_GUARD
#pragma once
#include <d3d12.h>
#include <wrl/client.h>

namespace RaytracingImplementation
{
	// #DXR
	struct AccelerationStructureBuffers
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> pScratch;      // Scratch memory for AS builder
		Microsoft::WRL::ComPtr<ID3D12Resource> pResult;       // Where the AS is
		Microsoft::WRL::ComPtr<ID3D12Resource> pInstanceDesc; // Hold the matrices of the instances
	};
}

#endif