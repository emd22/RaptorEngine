/*
 * File:        MapSource.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: The world brushes and entities of a prototype file, flattened into world space
 */

#pragma once

#include "Geometry.hpp"

#include <Map/CompiledMap.hpp>
#include <Renderer/ProbePlacement.hpp>

namespace fx::mapc {

struct TextureMap
{
	Vec3d GradientU = Vec3d(0.0);
	Vec3d GradientV = Vec3d(0.0);
	double OffsetU = 0.0;
	double OffsetV = 0.0;
};

struct SourceBrush
{
	String Name;
	int32 MaterialIndex = 0;

	/// Emits triangles
	bool bVisible = true;
	/// Emits a collision hull
	bool bCollides = true;
	/// Takes part in the BSP, so it seals the map and hides the faces buried in it
	bool bSolid = true;
	bool bCastsShadow = true;

	Array<int32> PlaneIndices;
	Array<TextureMap> Maps;

	Polyhedron Shape;

	const TextureMap* FindMap(int32 plane_index) const;
};

class MapSource
{
public:
	bool Load(const String& path, PlaneSet& planes);

public:
	/// These three hold strings, so they are sized once from the number of brushes in the prototype and never grow
	SizedArray<SourceBrush> Brushes;
	SizedArray<CompiledMapMaterial> Materials;
	SizedArray<String> Warnings;

	Array<Vec3d> Entities;

	bool bHasPlayerSpawn = false;
	Vec3d PlayerSpawn = Vec3d(0.0);

	bool bHasSun = false;
	Vec3d SunDirection = Vec3d(0.0, 1.0, 0.0);

	/// Everything the engine keeps light probes out of, in the order the engine would gather it
	ProbePlacementBoxes ProbeBoxes;
	/// World bounds of the probe volume brushes
	Array<BBox> ProbeRegions;
	uint32 ModelCount = 0;

	Vec3d Min = Vec3d(0.0);
	Vec3d Max = Vec3d(0.0);

	uint32 EntityBrushCount = 0;
	uint32 ClipBrushCount = 0;
	uint32 BoundBrushCount = 0;
	uint32 TransparentBrushCount = 0;
	uint32 InvalidBrushCount = 0;

private:
	int32 FindOrAddMaterial(const String& name, int32 library_index);
};

} // namespace fx::mapc
