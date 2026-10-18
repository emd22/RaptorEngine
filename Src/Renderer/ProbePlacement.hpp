/*
 * File:        ProbePlacement.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: Lays out light probe volumes around the solid geometry of a level. Shared by the engine and the map
 * compiler, so it only knows about boxes and brush planes.
 */

#pragma once

#include <Core/DynArray.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Slice.hpp>
#include <Core/StackArray.hpp>
#include <Core/Types.hpp>
#include <Math/BBox.hpp>
#include <Math/Mat4.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/Limits.hpp>
#include <functional>

namespace fx {
class Patch;
}
#include <limits>

namespace fx {

/// Mirrors ProbeVolume in Shaders/ProbeCommon.hlsli
struct ProbeVolumeData
{
	/// W is the number of volumes in the buffer
	float32 MinAndCount[4];
	float32 InvCellSize[4];
	/// W is the volume's first point in the probe grid table
	uint32 DimsAndFirst[4];
	float32 MaxAndCellVolume[4];
};

static_assert(sizeof(ProbeVolumeData) == 64);

struct ProbeGridSize
{
	uint32 X = Limits::ProbeGridDims[0];
	uint32 Y = Limits::ProbeGridDims[1];
	uint32 Z = Limits::ProbeGridDims[2];

	uint32 GetProbeCount() const { return X * Y * Z; }

	bool IsValid() const
	{
		return X >= Limits::MinProbeGridDim && Y >= Limits::MinProbeGridDim && Z >= Limits::MinProbeGridDim;
	}
};

enum class eProbeFill
{
	Dense,
	Surface,
};

struct ProbeVolumeRange
{
	uint32 FirstProbe = 0;
	uint32 ProbeCount = 0;
};

struct ProbePlacementBoxes
{
	struct Plane
	{
		Vec3f Normal;
		float32 Distance;
	};

	struct Box
	{
		BBox LocalBounds;
		BBox WorldBounds;
		bool bAxisAligned = false;
		Mat4f LocalToWorld;
		Mat4f WorldToLocal;

		/// Local space planes of a brush that isn't a box. Empty for a box.
		SizedArray<Plane> Planes;
	};

	static constexpr uint32 scMaxBoxes = 256;

	/// Lit solids. Probes are placed around them and kept out of them.
	DynArray<Box, GrowthFunctions::Double> Boxes;
	/// Unlit solids (dbound). Probes are only kept out of them.
	DynArray<Box, GrowthFunctions::Double> Bounds;
	/// World bounds of thin lit surfaces (patches). Probes are placed around them but not kept out of them.
	DynArray<BBox, GrowthFunctions::Double> Surfaces;

	Vec3f Min = Vec3f(std::numeric_limits<float32>::max());
	Vec3f Max = Vec3f(-std::numeric_limits<float32>::max());

	/// Every object offered to AddBox(), including the ones past scMaxBoxes
	uint32 ObjectCount = 0;

	/// Optional. Returns false for points that no probe should be placed at.
	std::function<bool(const Vec3f)> IsOpen;

	bool IsEmpty() const { return Min.X > Max.X; }
};

struct ProbePlacementSettings
{
	static constexpr float32 scDefaultBrushSpacing = 2.5f;
	static constexpr float32 scDefaultLevelSpacing = 0.5f;

	float32 BrushSpacing = scDefaultBrushSpacing;
	float32 LevelSpacing = scDefaultLevelSpacing;
};

struct ProbeLayout
{
	ProbeVolumeData Volumes[Limits::MaxProbeVolumes] {};
	ProbeVolumeRange Ranges[Limits::MaxProbeVolumes] {};
	uint32 VolumeCount = 0;

	/// One entry per grid point: a probe index, Limits::ProbeGridEmpty or Limits::ProbeGridSolid
	DynArray<uint16, GrowthFunctions::Double> Grid;
	DynArray<Vec3f, GrowthFunctions::Double> Positions;

	/// Grid points left without a probe by ProbePlacementBoxes::IsOpen
	uint32 VoidPointCount = 0;

	uint32 GetProbeCount() const { return Positions.Size; }
	uint32 GetGridPointCount() const { return Grid.Size; }

	uint16* AddGridPoints(uint32 count)
	{
		const uint32 first = Grid.Size;

		for (uint32 i = 0; i < count; i++) {
			Grid.Emplace(Limits::ProbeGridEmpty);
		}

		return (count > 0) ? &Grid[first] : nullptr;
	}

	void Truncate(uint32 grid_point_count, uint32 probe_count)
	{
		while (Grid.Size > grid_point_count) {
			Grid.RemoveLast();
		}

		while (Positions.Size > probe_count) {
			Positions.RemoveLast();
		}
	}

	void CopyFrom(const ProbeLayout& other)
	{
		VolumeCount = other.VolumeCount;
		VoidPointCount = other.VoidPointCount;

		for (uint32 volume = 0; volume < VolumeCount; volume++) {
			Volumes[volume] = other.Volumes[volume];
			Ranges[volume] = other.Ranges[volume];
		}

		Grid.Assign(other.Grid.pData, other.Grid.Size);
		Positions.Assign(other.Positions.pData, other.Positions.Size);
	}

	void Clear()
	{
		VolumeCount = 0;
		VoidPointCount = 0;
		Grid.Clear();
		Positions.Clear();
	}
};

namespace ProbePlacement {

/// Near plane of the probe capture cameras. Probes are kept twice this far from every surface.
constexpr float32 scCaptureNearPlane = 0.1f;

/// @returns False if the solid is too big to be level geometry (the sky) and was left out
bool AddBox(ProbePlacementBoxes& boxes, const BBox& local_bounds, const Mat4f& local_to_world,
			const SizedArray<ProbePlacementBoxes::Plane>& planes);
void AddBound(ProbePlacementBoxes& boxes, const BBox& local_bounds, const Mat4f& local_to_world,
			  const SizedArray<ProbePlacementBoxes::Plane>& planes);
void AddSurface(ProbePlacementBoxes& boxes, const BBox& world_bounds);
void AddPatchSurfaces(ProbePlacementBoxes& boxes, const Patch& patch, const Mat4f& local_to_world);

bool CheckGridSize(const ProbeGridSize& grid);
void FinaliseVolumeBounds(ProbeVolumeData& volume);

/// True if a probe that belongs at `grid_position` can sit at `position`
bool IsPositionValid(const Vec3f grid_position, const Vec3f position, const ProbePlacementBoxes& boxes);

/// Searches around `grid_position` for the valid position closest to `preferred`
bool FindValidPosition(const Vec3f grid_position, const Vec3f preferred, const Vec3f max_relocation,
					   const ProbePlacementBoxes& boxes, Vec3f& out_position);

bool AddVolume(ProbeLayout& layout, const Vec3f volume_min, const Vec3f volume_size, const ProbeGridSize& grid,
			   const ProbePlacementBoxes& boxes, eProbeFill fill);

/// Adds the coarse dense volume that covers the whole level
bool AddLevelBaseVolume(ProbeLayout& layout, const ProbePlacementBoxes& boxes);

/// Adds a volume with probes near surfaces only, coarsening `spacing` until it fits the probes that are left
bool AddSurfaceVolumeForBudget(ProbeLayout& layout, const Vec3f region_min, const Vec3f region_max, float32 spacing,
							   const ProbePlacementBoxes& boxes, bool cell_centred);

/**
 * @brief Replaces the layout with the base volume, a surface volume per probe volume brush, and a surface volume over
 * the level
 * @returns The number of brush regions that got a volume
 */
uint32 BuildLevelLayout(ProbeLayout& layout, const ProbePlacementBoxes& boxes, const Slice<BBox>& brush_regions,
						const ProbePlacementSettings& settings);

} // namespace ProbePlacement

} // namespace fx
