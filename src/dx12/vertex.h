#ifndef VERTEX_GUARD
#define VERTEX_GUARD

#pragma once
#include <DirectXMath.h>

struct Vertex
{
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT4 color;
};

#endif // !VERTEX_GUARD