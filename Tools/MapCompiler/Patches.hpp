/*
 * File:        Patches.hpp
 * Author:      emd22
 * Created:     15/10/2026
 * Description: Tessellates the curve patches of a prototype into world space triangles
 */

#pragma once

#include "Bsp.hpp"
#include "MapSource.hpp"

#include <Math/Vec2.hpp>

namespace fx::mapc {

struct PatchSurfaceVertex
{
	Vec3d Position = Vec3d(0.0);
	Vec3d Normal = Vec3d(0.0);
	Vec3d Tangent = Vec3d(0.0);
	double Handedness = 1.0;
	Vec2d Texcoord = Vec2d(0.0, 0.0);
	Vec2d Chart = Vec2d(0.0, 0.0);
};

struct PatchSurface
{
	uint32 SourceIndex = 0;
	int32 MaterialIndex = 0;
	bool bCollides = true;
	bool bCastsShadow = true;

	Array<PatchSurfaceVertex> Vertices;
	Array<int32> Indices;
};

struct PatchStats
{
	uint32 PatchCount = 0;
	uint32 SourceTriangles = 0;
	uint32 CulledTriangles = 0;
	uint32 SmoothedVertices = 0;
	uint32 TJunctionsFixed = 0;
	uint32 Triangles = 0;
	uint32 CollisionMeshes = 0;
	uint32 CollisionTriangles = 0;
};

class PatchBuilder
{
public:
	void Build(const MapSource& source, const Bsp& bsp, bool sealed, bool cull_hidden);

	void WriteCollision(double chunk_size, CompiledMap& out_map);

	static void FindBoundaryEdges(const PatchSurface& surface, Array<std::pair<int32, int32>>& out_edges);

public:
	Array<PatchSurface> Surfaces;

	Array<Vec3d> ReferencePoints;

	PatchStats Stats;

private:
	void SmoothNormals(const MapSource& source);
	void Cull(const Bsp& bsp, bool sealed);
};

} // namespace fx::mapc
