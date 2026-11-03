#pragma once

#include <Core/DynArray.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <functional>
#include <optional>

namespace fx {

struct AcousticSurface
{
	String Name;
	std::optional<float32> Density;

	bool operator==(const AcousticSurface& other) const { return Name == other.Name && Density == other.Density; }
};

struct AcousticGeometry
{
	DynArray<Vec3f, GrowthFunctions::Double> Vertices;
	DynArray<uint32, GrowthFunctions::Double> Indices;
	DynArray<uint32, GrowthFunctions::Double> TriangleSurfaces;
	DynArray<AcousticSurface, GrowthFunctions::Double> Surfaces;

	AcousticGeometry Clone() const
	{
		AcousticGeometry copy;
		copy.Vertices = Vertices.Clone();
		copy.Indices = Indices.Clone();
		copy.TriangleSurfaces = TriangleSurfaces.Clone();
		copy.Surfaces = Surfaces.Clone();

		return copy;
	}

	void AddTriangle(const Vec3f a, const Vec3f b, const Vec3f c, uint32 surface)
	{
		const uint32 base = Vertices.Size;

		Vertices.Insert(a);
		Vertices.Insert(b);
		Vertices.Insert(c);

		Indices.Insert(base);
		Indices.Insert(base + 1);
		Indices.Insert(base + 2);

		TriangleSurfaces.Insert(surface);
	}

	uint32 FindOrAddSurface(const AcousticSurface& surface)
	{
		for (uint32 i = 0; i < Surfaces.Size; i++) {
			if (Surfaces[i] == surface) {
				return i;
			}
		}

		Surfaces.Insert(surface);

		return Surfaces.Size - 1;
	}

	uint32 GetTriangleCount() const { return TriangleSurfaces.Size; }
};

struct AcousticBakeSettings
{
	Vec3f BoundsMin = Vec3f::sZero;
	Vec3f BoundsMax = Vec3f::sZero;

	float32 ProbeSpacing = 3.0f;
	float32 ProbeHeight = 1.5f;

	int32 ReverbRays = 16384;
	int32 ReverbBounces = 64;
	float32 ReverbDuration = 2.0f;

	bool bPathing = true;
	int32 PathingSamples = 4;
	float32 PathingRadius = 0.5f;
	float32 PathingThreshold = 0.1f;
	float32 PathingVisRange = 50.0f;
	float32 PathingRange = 100.0f;

	int32 Threads = 0;
};

struct AcousticBakeResult
{
	SizedArray<uint8> Data;

	uint32 ProbeCount = 0;
	Vec3f ProbeMin = Vec3f::sZero;
	Vec3f ProbeMax = Vec3f::sZero;

	bool bHasPathing = false;

	double ReflectionSeconds = 0.0;
	double PathingSeconds = 0.0;

	String Error;
};

using AcousticProgressFunction = std::function<void(const char* stage, float32 fraction)>;

namespace Acoustics {

static constexpr int32 scProbeOrder = 1;

Vec3f ToAudioSpace(const Vec3f value);
AcousticGeometry ToAudioSpace(const AcousticGeometry& geometry);

uint32 GetMaterialPreset(const String& surface);
const char* GetMaterialPresetName(uint32 preset);
float32 GetReflectivity(const String& surface);

bool Bake(const AcousticGeometry& geometry, const AcousticBakeSettings& settings, AcousticBakeResult& out_result,
		  const AcousticProgressFunction& progress = {});

bool UnwrapProbeData(const SizedArray<uint8>& data, const uint8*& out_payload, size_t& out_size, String& out_error);

} // namespace Acoustics

} // namespace fx
