/*
 * File:        IndirectLight.hpp
 * Author:      emd22
 * Created:     20/10/2026
 * Description: Baker for indirect lighting for compiled maps. Uses iterative radiosity (Jacobi method)
 */

#pragma once

#include "Lightmap.hpp"

#include <Core/SizedArray.hpp>
#include <Math/Rect.hpp>

namespace fx::mapc {

// struct LightmapRect
// {
// 	uint32 X = 0;
// 	uint32 Y = 0;
// 	uint32 Width = 0;
// 	uint32 Height = 0;
// };


struct IndirectInput
{
	uint32 Width = 0;
	uint32 Height = 0;
	float32 TexelSize = 0.0f;

	const uint8* pCovered = nullptr;
	const uint8* pSunVisibility = nullptr;
	const Vec3f* pPositions = nullptr;
	const Vec3f* pNormals = nullptr;


	SizedArray<URect> LightmapRects;

	// const LightmapRect* pRects = nullptr;
	// uint32 RectCount = 0;
};

bool BakeIndirectLight(const MapSource& source, const SurfaceBuilder& surfaces, const IndirectInput& input,
					   const LightmapOptions& options, CompiledMap& out_map, LightmapStats& out_stats);

} // namespace fx::mapc
