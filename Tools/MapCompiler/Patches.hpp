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
	DVec3 Position = DVec3(0.0);
	DVec3 Normal = DVec3(0.0);
	DVec3 Tangent = DVec3(0.0);
	double Handedness = 1.0;
	DVec2 Texcoord = DVec2(0.0, 0.0);
	DVec2 Chart = DVec2(0.0, 0.0);
	PatchColor Color;
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

struct PatchEdge
{
	int32 From = 0;
	int32 To = 0;
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

	static void FindBoundaryEdges(const PatchSurface& surface, Array<PatchEdge>& out_edges);

public:
	Array<PatchSurface> Surfaces;

	Array<DVec3> ReferencePoints;
	Array<DVec3> ReferenceFacings;

	PatchStats Stats;

private:
	void SmoothNormals(const MapSource& source);
	void Cull(const Bsp& bsp, bool sealed);
};

} // namespace fx::mapc
