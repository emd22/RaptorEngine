/*
 * File:        Surfaces.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: Turns the visible parts of brush faces into batched triangle meshes
 */

#pragma once

#include "Bsp.hpp"
#include "MapSource.hpp"
#include "Patches.hpp"

#include <Math/Vec2.hpp>

namespace fx::mapc {

struct SurfaceOptions
{
	bool bCullHidden = true;
	bool bMerge = true;
	bool bFixTJunctions = true;
	double ChunkSize = 32.0;

	bool bMakeLightmapCharts = true;
};

struct SurfaceStats
{
	uint32 NumSrcFaces = 0;
	uint32 NumSrcTriangles = 0;

	uint32 FacesRemoved = 0;
	uint32 FacesClipped = 0;
	uint32 OverlapsRemoved = 0;

	uint32 NumPolygonsBeforeMerge = 0;
	uint32 NumPolygons = 0;
	uint32 TJunctionsFixed = 0;
	uint32 MergeFallbacks = 0;

	uint32 NumTriangles = 0;
	uint32 NumVertices = 0;
	uint32 NumShadowTriangles = 0;
	uint32 NumCaulkTriangles = 0;

	double SourceArea = 0.0;
	double Area = 0.0;
};

struct SurfacePolygon
{
	int32 PlaneIndex = PlaneSet::scNone;
	int32 MaterialIndex = 0;
	int32 MapIndex = 0;

	Winding Points;
};

struct LightmapChart
{
	int32 PlaneIndex = PlaneSet::scNone;

	Vec3d U = Vec3d(0.0);
	Vec3d V = Vec3d(0.0);

	Array<Vec2d> TrianglePoints;

	Array<Vec3d> WorldPoints;
	Array<Vec3d> Normals;

	bool IsPlanar() const { return WorldPoints.Size == 0; }
};

struct LightmapVertex
{
	int32 Chart = -1;
	Vec2d Point = Vec2d(0.0, 0.0);
};

class SurfaceBuilder
{
public:
	void Build(const PlaneSet& planes, const Bsp& bsp, const MapSource& source, bool sealed,
			   const SurfaceOptions& options, PatchBuilder* patches, CompiledMap& out_map);

public:
	SurfaceStats Stats;

	/// Three corners per triangle of everything that was written, for verification
	Array<Vec3d> TrianglePoints;

	Array<LightmapChart> Charts;
	Array<Array<LightmapVertex>> LightmapVertices;

private:
	int32 FindOrAddMap(const TextureMap& map);

	void CullFaces(const PlaneSet& planes, const Bsp& bsp, const MapSource& source, bool sealed,
				   const SurfaceOptions& options);
	void Triangulate(const PlaneSet& planes, const MapSource& source, const SurfaceOptions& options,
					 PatchBuilder* patches, CompiledMap& out_map);

private:
	Array<SurfacePolygon> mPolygons;
	Array<TextureMap> mMaps;
};

} // namespace fx::mapc
