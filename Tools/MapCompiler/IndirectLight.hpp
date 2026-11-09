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

struct IndirectInput
{
	uint32 Width = 0;
	uint32 Height = 0;
	float32 TexelSize = 0.0f;

	const uint8* pCovered = nullptr;
	const uint8* pSunVisibility = nullptr;
	const Vec3f* pPositions = nullptr;
	const Vec3f* pNormals = nullptr;

	/// The brush planes of the map, for the water volumes
	const PlaneSet* pPlanes = nullptr;

	SizedArray<URect> LightmapRects;
};

/**
 * @brief Bakes bounce light into the lightmap and the light probes. Model triangles (`models`, optional) have no
 * lightmap texels, so their radiance lives in a bake-only cache of world space cells that is gathered like texels.
 */
bool BakeIndirectLight(const MapSource& source, const SurfaceBuilder& surfaces, const ModelBuilder* models,
					   const IndirectInput& input, const LightmapOptions& options, CompiledMap& out_map,
					   LightmapStats& out_stats);

} // namespace fx::mapc
