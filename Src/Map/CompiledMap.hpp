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
#include <Renderer/ProbePlacement.hpp>

namespace fx {

struct CompiledMapVertex
{
	float32 Position[3];
	float32 Normal[3];
	/// The fourth value is the handedness
	float32 Tangent[4];
	float32 Texcoord[2];
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

	float32 BoundsMin[3] = { 0.0f, 0.0f, 0.0f };
	float32 BoundsMax[3] = { 0.0f, 0.0f, 0.0f };

	SizedArray<CompiledMapVertex> Vertices;
	SizedArray<uint32> Indices;

	SizedArray<float32> LightmapTexcoords;

	bool HasLightmapTexcoords() const { return LightmapTexcoords.Size == Vertices.Size * 2 && Vertices.Size > 0; }
};

struct CompiledMapLightmap
{
	uint32 Width = 0;
	uint32 Height = 0;

	SizedArray<uint8> SunVisibility;

	float32 SunDirection[3] = { 0.0f, 1.0f, 0.0f };

	bool IsEmpty() const { return SunVisibility.Size == 0; }
};

/**
 * @brief The corners of a convex collider, in world space
 */
struct CompiledMapHull
{
	SizedArray<float32> Points;

	uint32 GetPointCount() const { return static_cast<uint32>(Points.Size / 3); }
};

class CompiledMap
{
public:
	static constexpr uint32 scMagic = 0x504D5846;
	static constexpr uint32 scVersion = 3;
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
	SizedArray<CompiledMapHull> Hulls;

	/// Where the light probes go, worked out from the brushes. Empty if the map was compiled without probes.
	ProbeLayout Probes;
	ProbePlacementSettings ProbeSettings;

	bool HasProbeLayout() const { return Probes.VolumeCount > 0; }

	CompiledMapLightmap Lightmap;

	SizedArray<uint8> ProbeSunVisibility;

	bool HasBakedSun() const { return !Lightmap.IsEmpty() && ProbeSunVisibility.Size == Probes.GetProbeCount(); }

	/// The brushes of the prototype that the surfaces and hulls replace, sorted by Load()
	SizedArray<String> BrushNames;
};

} // namespace fx
