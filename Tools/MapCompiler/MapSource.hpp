/*
 * File:        MapSource.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: The world brushes and entities of a prototype file, flattened into world space
 */

#pragma once

#include "Geometry.hpp"

#include <Map/CompiledMap.hpp>
#include <Patch.hpp>
#include <Renderer/ProbePlacement.hpp>

namespace fx {

class ConfigEntry;
class String;

} // namespace fx

namespace fx::mapc {

struct CachedMaterials;

struct TextureMap
{
	Vec3d GradientU = Vec3d::scZero;
	Vec3d GradientV = Vec3d::scZero;

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

	bool bPortal = false;

	Array<int32> PlaneIndices;
	Array<TextureMap> Maps;

	Array<int32> FaceMaterials;

	Polyhedron Shape;

	static constexpr int32 scDroppedFace = -1;

	const TextureMap* FindMap(int32 plane_index, int32& out_material) const;
};

struct SourcePatch
{
	String Name;
	int32 MaterialIndex = 0;

	bool bCollides = true;
	bool bCastsShadow = true;

	Patch Shape;
};

struct SourceLight
{
	Vec3d Position = Vec3d::scZero;
	Vec3d Direction = Vec3d(0.0, -1.0, 0.0);
	Vec3d Color = Vec3d::scZero;

	double InvRadiusSq = 0.0;

	bool bSpot = false;
	double SpotCosOuter = 0.0;
	double SpotAngleScale = 0.0;
};

class MapSource
{
public:
	bool Load(const String& path, PlaneSet& planes);

	void AddBrush(PlaneSet& planes, ConfigEntry& entry, const String& name, const CachedMaterials& mc);
	void AddPatch(ConfigEntry& entry, const String& name, const CachedMaterials& mc);

public:
	/// These three hold strings, so they are sized once from the number of brushes in the prototype and never grow
	SizedArray<SourceBrush> Brushes;
	SizedArray<SourcePatch> Patches;
	SizedArray<CompiledMapMaterial> Materials;
	Array<uint8> OpaqueMaterials;
	SizedArray<String> Warnings;

	Array<Vec3d> Entities;

	bool bHasPlayerSpawn = false;
	Vec3d PlayerSpawn = Vec3d::scZero;

	bool bHasSun = false;
	Vec3d SunDirection = Vec3d::scUp;
	Vec3d SunColor = Vec3d::scZero;

	Array<SourceLight> Lights;

	/// Everything the engine keeps light probes out of, in the order the engine would gather it
	ProbePlacementBoxes ProbeBoxes;
	/// World bounds of the probe volume brushes
	Array<BBox> ProbeRegions;
	uint32 ModelCount = 0;

	Vec3d Min = Vec3d::scZero;
	Vec3d Max = Vec3d::scZero;

	uint32 EntityBrushCount = 0;
	uint32 ClipBrushCount = 0;
	uint32 BoundBrushCount = 0;
	uint32 PortalBrushCount = 0;
	uint32 TransparentBrushCount = 0;
	uint32 NonVisFaceCount = 0;
	uint32 InvalidBrushCount = 0;
	uint32 PatchCount = 0;

private:
	int32 FindOrAddMaterial(const String& name, int32 library_index, const CachedMaterials& mc);
};

} // namespace fx::mapc
