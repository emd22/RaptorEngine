/*
 * File:        MapSource.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: The world brushes and entities of a prototype file, flattened into world space
 */

#pragma once

#include "Geometry.hpp"

#include <Map/CompiledMap.hpp>
#include <Math/Mat4.hpp>
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
	DVec3 GradientU = DVec3::scZero;
	DVec3 GradientV = DVec3::scZero;

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
	/// Filled with a water material, which absorbs the light that crosses it in the indirect bake
	bool bWater = false;

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

struct SourceModel
{
	String Name;
	String MeshPath;

	/// Model space to world space, as Entity::RecalculateModelMatrix builds it
	Mat4f Transform = Mat4f::scIdentity;

	/// Casts baked shadows and takes part in the bounce (see IsBakedModelEntry)
	bool bBaked = false;
	/// Keeps light probes out of its bounds, as the engine's GatherPlacementBoxes does
	bool bProbeBox = false;
};

struct SourceLight
{
	DVec3 Position = DVec3::scZero;
	DVec3 Direction = DVec3(0.0, -1.0, 0.0);
	DVec3 Color = DVec3::scZero;

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
	void AddModels(const ConfigEntry* models_entry, const ConfigEntry* brushes);

public:
	/// These three hold strings, so they are sized once from the number of brushes in the prototype and never grow
	SizedArray<SourceBrush> Brushes;
	SizedArray<SourcePatch> Patches;
	SizedArray<CompiledMapMaterial> Materials;
	Array<uint8> OpaqueMaterials;
	Array<uint8> WaterMaterials;
	SizedArray<String> Warnings;

	Array<DVec3> Entities;

	bool bHasPlayerSpawn = false;
	DVec3 PlayerSpawn = DVec3::scZero;

	bool bHasSun = false;
	DVec3 SunDirection = DVec3::scUp;
	DVec3 SunColor = DVec3::scZero;

	Array<SourceLight> Lights;

	/// Everything the engine keeps light probes out of, in the order the engine would gather it
	ProbePlacementBoxes ProbeBoxes;
	/// World bounds of the probe volume brushes
	Array<BBox> ProbeRegions;
	/// Holds strings, so it is sized once from the prototype's `objects` entry
	SizedArray<SourceModel> Models;

	DVec3 Min = DVec3::scZero;
	DVec3 Max = DVec3::scZero;

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
