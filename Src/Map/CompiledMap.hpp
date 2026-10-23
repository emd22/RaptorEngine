/*
 * File:        CompiledMap.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: World geometry compiled from a prototype file by the map compiler (Tools/MapCompiler)
 */

#pragma once

#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <Math/Vec4.hpp>
#include <Renderer/Limits.hpp>
#include <Renderer/ProbePlacement.hpp>

namespace fx {

constexpr int32 scNoMapArea = -1;

struct CompiledMapVertex
{
	Vec3f Position;
	Vec3f Normal;
	/// The fourth value is the handedness
	Vec4f Tangent;
	Vec2f Texcoord;

	// float32 Position[3];
	// float32 Normal[3];
	// float32 Tangent[4];
	// float32 Texcoord[2];
};

struct CompiledMapMaterial
{
	/// Name in the material library. When empty, `LibraryIndex` is used instead.
	String Name;
	int32 LibraryIndex = -1;
};

/**
 * @brief A batch of triangles that share a material, in world space
 */
struct CompiledMapSurface
{
	uint32 MaterialIndex = 0;
	int32 Area = scNoMapArea;

	BBox Bounds;

	SizedArray<CompiledMapVertex> Vertices;
	SizedArray<uint32> Indices;

	SizedArray<float32> LightmapTexcoords;

	/// RGBA8 per vertex (red in the lowest byte), or empty when every vertex is white and opaque
	SizedArray<uint32> Colors;

	bool HasLightmapTexcoords() const { return LightmapTexcoords.Size == Vertices.Size * 2 && Vertices.Size > 0; }
	bool HasVertexColors() const { return Colors.Size == Vertices.Size && Vertices.Size > 0; }
};

struct CompiledMapShadowSurface
{
	BBox Bounds;

	SizedArray<float32> Points;
	SizedArray<uint32> Indices;

	uint32 GetPointCount() const { return static_cast<uint32>(Points.Size / 3); }
};

struct CompiledMapLightmap
{
	static constexpr uint32 scFlagSun = 1 << 0;

	uint32 Width = 0;
	uint32 Height = 0;
	uint32 Flags = 0;

	SizedArray<uint8> SunVisibility;

	SizedArray<uint16> Irradiance;
	SizedArray<uint8> Directions;

	float32 SunDirection[3] = { 0.0f, 1.0f, 0.0f };

	uint64 GetTexelCount() const { return static_cast<uint64>(Width) * Height; }

	bool IsEmpty() const { return SunVisibility.Size == 0; }
	bool HasSun() const { return (Flags & scFlagSun) != 0; }
	bool HasIndirect() const
	{
		return !IsEmpty() && Irradiance.Size == GetTexelCount() * 3 && Directions.Size == GetTexelCount() * 3;
	}
};

/**
 * @brief The corners of a convex collider, in world space
 */
struct CompiledMapHull
{
	SizedArray<float32> Points;

	uint32 GetPointCount() const { return static_cast<uint32>(Points.Size / 3); }
};

struct CompiledMapCollisionMesh
{
	SizedArray<float32> Points;
	SizedArray<uint32> Indices;

	uint32 GetPointCount() const { return static_cast<uint32>(Points.Size / 3); }
};

struct CompiledMapArea
{
	BBox Bounds;
};

struct CompiledMapAreaPortal
{
	String Name;

	uint32 BackArea = 0;
	uint32 FrontArea = 0;

	BBox Bounds;

	SizedArray<float32> Points;
	SizedArray<uint32> PolygonSizes;

	uint32 GetPointCount() const { return static_cast<uint32>(Points.Size / 3); }
};

struct CompiledMapAreaNode
{
	float32 Plane[4] = {};
	int32 Front = 0;
	int32 Back = 0;
};

class CompiledMap
{
public:
	static constexpr uint32 scMagic = 0x504D5846;
	static constexpr uint32 scVersion = 10;
	static constexpr const char* scExtension = ".fxmap";
	static constexpr const char* scPrototypeExtension = ".prx";

public:
	bool Load(const String& path);
	bool Save(const String& path) const;

	bool HasBrush(const String& name) const;

	/**
	 * @brief Hashes the contents of a file, to tell if a compiled map still matches the prototype it came from
	 * @returns 0 if the file can't be read
	 */
	static uint64 HashFile(const String& path);

	/**
	 * @brief Returns the path of the compiled map that belongs to a prototype file
	 */
	static String GetPathForPrototype(const String& prototype_path);

	/**
	 * @brief True if the path names a compiled map rather than a prototype, going by its extension
	 */
	static bool IsCompiledMapPath(const String& path);

	/**
	 * @brief Returns the path of the prototype that a compiled map was made from. A prototype's own path comes back
	 * as it is.
	 */
	static String GetPrototypePath(const String& path);

public:
	uint64 SourceHash = 0;

	/// True if the map had no leaks, so the faces that only the void can see were removed
	bool bSealed = false;

	SizedArray<CompiledMapMaterial> Materials;
	SizedArray<CompiledMapSurface> Surfaces;
	SizedArray<CompiledMapShadowSurface> ShadowSurfaces;
	SizedArray<CompiledMapHull> Hulls;
	SizedArray<CompiledMapCollisionMesh> CollisionMeshes;

	/// Where the light probes go, worked out from the brushes. Empty if the map was compiled without probes.
	ProbeLayout Probes;
	ProbePlacementSettings ProbeSettings;

	bool HasProbeLayout() const { return Probes.VolumeCount > 0; }

	CompiledMapLightmap Lightmap;

	SizedArray<uint8> ProbeSunVisibility;

	static constexpr uint32 scProbeSHFloats = Limits::ProbeSHCoeffCount * 3;
	static constexpr uint32 scProbeMomentValues = Limits::ProbeDepthFaces * Limits::ProbeDepthTexelsPerFace * 2;

	SizedArray<float32> ProbeIrradiance;
	SizedArray<uint16> ProbeMoments;

	bool HasLightmap() const { return !Lightmap.IsEmpty(); }
	bool HasBakedSun() const
	{
		return HasLightmap() && Lightmap.HasSun() && ProbeSunVisibility.Size == Probes.GetProbeCount();
	}
	bool HasBakedProbes() const
	{
		const uint64 probe_count = Probes.GetProbeCount();
		return probe_count > 0 && ProbeIrradiance.Size == probe_count * scProbeSHFloats &&
			   ProbeMoments.Size == probe_count * scProbeMomentValues;
	}

	/// The brushes of the prototype that the surfaces and hulls replace, sorted by Load()
	SizedArray<String> BrushNames;

	SizedArray<CompiledMapArea> Areas;
	SizedArray<CompiledMapAreaPortal> AreaPortals;
	SizedArray<CompiledMapAreaNode> AreaNodes;
	int32 AreaRoot = MakeLeafChild(scNoMapArea);

	bool HasAreas() const { return Areas.Size > 0; }

	int32 FindArea(const Vec3f& point) const;

	static constexpr int32 MakeLeafChild(int32 area) { return -2 - area; }
	static constexpr int32 GetLeafArea(int32 child) { return -2 - child; }
};

} // namespace fx
