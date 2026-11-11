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
#include <Math/FVec3.hpp>
#include <Math/FVec4.hpp>
#include <Math/Vec2.hpp>
#include <Renderer/Limits.hpp>
#include <Renderer/ProbePlacement.hpp>

namespace fx {

constexpr int32 scNoMapArea = -1;

struct CompiledMapVertex
{
	FVec3 Position;
	FVec3 Normal;
	/// The fourth value is the handedness
	FVec4 Tangent;
	FVec2 Texcoord;
	/// Named so it is copied and written as zeros instead of whatever the allocation held
	float32 Padding[2] = {};

	// float32 Position[3];
	// float32 Normal[3];
	// float32 Tangent[4];
	// float32 Texcoord[2];
};

static_assert(sizeof(CompiledMapVertex) == 64, "CompiledMapVertex is written to .fxmap files as is");

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

struct CompiledMapFogShadowLevel
{
	uint32 Width = 0;
	uint32 Height = 0;

	/// The light space depth of the first occluder seen from the sun, across the page's depth range. 0xFFFF where
	/// nothing in the page is shadowed.
	SizedArray<uint16> Depths;
};

/**
 * @brief The sun's shadow over one square of the world, seen along the baked sun direction. Pages only exist where
 * something casts a shadow, every other cell of the grid is fully lit.
 */
struct CompiledMapFogShadowPage
{
	int32 CellX = 0;
	int32 CellZ = 0;

	/// Lowest corner and size of the page in the light space plane
	float32 LightMin[2] = {};
	float32 LightSize[2] = {};

	float32 DepthMin = 0.0f;
	float32 DepthMax = 0.0f;

	SizedArray<CompiledMapFogShadowLevel> Levels;
};

/**
 * @brief Sun shadow pages for the fog, so it can be shadowed by the static world without a runtime shadow map
 */
struct CompiledMapFogShadow
{
	static constexpr uint32 scLevelCount = 3;
	static constexpr uint32 scLevelStep = 4;

	float32 Right[3] = { 1.0f, 0.0f, 0.0f };
	float32 Up[3] = { 0.0f, 0.0f, 1.0f };
	float32 Forward[3] = { 0.0f, -1.0f, 0.0f };

	float32 PageSize = 0.0f;
	float32 TexelSize = 0.0f;

	int32 CellMin[2] = {};
	uint32 CellCount[2] = {};

	SizedArray<CompiledMapFogShadowPage> Pages;

	bool IsEmpty() const { return CellCount[0] == 0 || CellCount[1] == 0; }
};

/**
 * @brief The corners of a convex collider, in world space
 */
/// One clip level of the SDF page scene, whose pages are `SDFPageSize` metres wide
struct CompiledMapSDFPageLevel
{
	float32 SDFPageSize = 0.0f;
	uint32 FirstSDFPage = 0;
	uint32 SDFPageCount = 0;
	uint32 Padding = 0;
};

/// A page that some geometry passes within the margin of. `Coord` times the level's page size is its minimum corner.
struct CompiledMapSDFPage
{
	int32 Coord[3] = {};
	uint32 FirstRef = 0;
	uint32 RefCount = 0;
	uint32 Padding = 0;
};

/// Laid out to match SDFPageTriangle in Shaders/SDFPageCommon.hlsli. The first corner's W holds the triangle's
/// linear albedo as RGB8 (red in the low byte), stored as the bits of a uint32.
struct CompiledMapSDFPageTriangle
{
	float32 Corners[3][4] = {};
};

/**
 * @brief The geometry the GI traces against: for every clip level, the pages that geometry touches and the triangles
 * that their distances are computed from. The distances themselves are computed on the GPU.
 */
struct CompiledMapSDFPages
{
	static constexpr uint32 scTexelsPerSDFPage = 8;
	static constexpr uint32 scMaxLevels = 8;

	/// Distances are kept up to this many page widths, and a page lists every triangle within that of it
	float32 MarginScale = 0.5f;

	SizedArray<CompiledMapSDFPageLevel> Levels;
	/// Each level's pages are sorted by X, then Y, then Z
	SizedArray<CompiledMapSDFPage> SDFPages;
	SizedArray<uint32> Refs;
	SizedArray<CompiledMapSDFPageTriangle> Triangles;

	bool IsEmpty() const { return Levels.Size == 0 || SDFPages.Size == 0 || Triangles.Size == 0; }
};

struct CompiledMapProbeMaskLevel
{
	float32 Spacing = 0.0f;
	/// The box's minimum grid point, in spacings
	int32 Origin[3] = {};
	/// Grid points along each axis
	uint32 Size[3] = {};
	/// Where the level's bits start in CompiledMapProbeMask::Words
	uint32 FirstWord = 0;
};

/**
 * @brief Which points of the runtime GI probe grids can hold a probe: a bit per grid point of every level, set where
 * the point, or somewhere a probe there could be moved to, is in open space that the map's entities can reach.
 */
struct CompiledMapProbeMask
{
	static constexpr uint32 scMaxLevels = 8;

	/// Whether grid points outside a level's box are dead as well, which they are when the map is sealed
	uint32 bOutsideDead = 0;

	SizedArray<CompiledMapProbeMaskLevel> Levels;
	/// X fastest, then Y, then Z
	SizedArray<uint32> Words;

	bool IsEmpty() const { return Levels.Size == 0 || Words.Size == 0; }

	/// Whether a probe may be at `probe` (in spacings) on `level`. True where the map has no mask for it.
	bool IsAlive(uint32 level, const int32 probe[3]) const
	{
		if (level >= Levels.Size) {
			return true;
		}

		const CompiledMapProbeMaskLevel& mask = Levels[level];
		uint32 local[3];

		for (uint32 axis = 0; axis < 3; axis++) {
			const int64 offset = static_cast<int64>(probe[axis]) - mask.Origin[axis];

			if (offset < 0 || offset >= static_cast<int64>(mask.Size[axis])) {
				return bOutsideDead == 0;
			}

			local[axis] = static_cast<uint32>(offset);
		}

		const uint64 bit = local[0] + static_cast<uint64>(mask.Size[0]) * (local[1] + static_cast<uint64>(mask.Size[1]) * local[2]);

		return ((Words[mask.FirstWord + bit / 32] >> (bit % 32)) & 1U) != 0;
	}
};

struct CompiledMapHull
{
	SizedArray<float32> Points;

	SizedArray<float32> SidePlanes;
	SizedArray<int32> SideMaterials;

	uint32 GetSideCount() const { return static_cast<uint32>(SideMaterials.Size); }

	uint32 GetPointCount() const { return static_cast<uint32>(Points.Size / 3); }
};

struct CompiledMapCollisionMesh
{
	SizedArray<float32> Points;
	SizedArray<uint32> Indices;

	SizedArray<int32> TriangleMaterials;

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
	static constexpr uint32 scVersion = 19;
	/// Oldest version that still loads, the parts added since are left empty
	static constexpr uint32 scMinVersion = 10;
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

	SizedArray<uint8> AcousticProbes;

	/// Where the SH probes of the reference bake go, worked out from the brushes. Empty unless mapc was asked for
	/// the reference bake. The engine does not use them.
	ProbeLayout Probes;
	ProbePlacementSettings ProbeSettings;

	bool HasProbeLayout() const { return Probes.VolumeCount > 0; }

	CompiledMapLightmap Lightmap;
	CompiledMapSDFPages SDFPages;
	CompiledMapProbeMask ProbeMask;

	SizedArray<uint8> ProbeSunVisibility;

	static constexpr uint32 scProbeSHFloats = Limits::ProbeSHCoeffCount * 3;
	static constexpr uint32 scProbeMomentValues = Limits::ProbeDepthTexelCount * 2;

	SizedArray<float32> ProbeIrradiance;
	SizedArray<uint16> ProbeMoments;

	bool HasLightmap() const { return !Lightmap.IsEmpty(); }
	bool HasBakedSun() const
	{
		return HasLightmap() && Lightmap.HasSun();
	}
	CompiledMapFogShadow FogShadow;

	bool HasFogShadow() const { return !FogShadow.IsEmpty(); }

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

	int32 FindArea(const FVec3& point) const;

	static constexpr int32 MakeLeafChild(int32 area) { return -2 - area; }
	static constexpr int32 GetLeafArea(int32 child) { return -2 - child; }
};

} // namespace fx
