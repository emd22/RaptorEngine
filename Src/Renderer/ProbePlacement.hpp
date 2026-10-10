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
#include <limits>

namespace fx {

struct ProbeVolumeData
{
	/// World space min corner in XYZ. W holds the number of volumes in the buffer.
	float32 MinAndCount[4];

	float32 InvCellSize[4];

	/// Grid dimensions in XYZ, and in W the index of this volume's first point in the probe grid table
	uint32 DimsAndFirst[4];

	float32 MaxAndCellVolume[4];
};

static_assert(sizeof(ProbeVolumeData) == 64, "ProbeVolumeData must mirror the HLSL ProbeVolume struct");

/// Probe grid resolution of one volume, in probes along each axis
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

		/// The bounding planes of a brush that isn't a box, in local space. Without them the solid is LocalBounds,
		/// which for a slanted brush would also take in the empty space beside the slope.
		SizedArray<Plane> Planes;
	};

	static constexpr uint32 scMaxBoxes = 256;

	Box Boxes[scMaxBoxes];
	uint32 Count = 0;

	Vec3f Min = Vec3f(std::numeric_limits<float32>::max());
	Vec3f Max = Vec3f(-std::numeric_limits<float32>::max());

	/// Solids that are not surfaces: nothing is lit by them, so they don't attract probes, but no probe goes inside
	/// them either. These are the brushes that only seal the map for the map compiler (dbound).
	DynArray<Box, GrowthFunctions::Double> Bounds;

	/// Objects that were offered to AddBox(), including the ones past scMaxBoxes that only widened Min and Max
	uint32 ObjectCount = 0;

	/// Optional. Returns false for points that no probe should be placed at, on top of the boxes. The map compiler
	/// uses it to keep probes out of the void around a sealed map.
	std::function<bool(const Vec3f)> IsOpen;

	bool IsEmpty() const { return Min.X > Max.X; }
};

struct ProbePlacementSettings
{
	static constexpr float32 scDefaultBrushSpacing = 2.5f;
	static constexpr float32 scDefaultLevelSpacing = 0.5f;

	/// Probe spacing inside probe volume brushes
	float32 BrushSpacing = scDefaultBrushSpacing;
	/// Probe spacing of the volume that covers the surfaces of the whole level
	float32 LevelSpacing = scDefaultLevelSpacing;
};

/**
 * @brief The probe volumes of a level, the table that maps their grid points to probes, and where each probe is
 */
struct ProbeLayout
{
	ProbeVolumeData Volumes[Limits::MaxProbeVolumes] {};
	ProbeVolumeRange Ranges[Limits::MaxProbeVolumes] {};
	uint32 VolumeCount = 0;

	/// One entry per grid point: a probe index, Limits::ProbeGridEmpty or Limits::ProbeGridSolid
	DynArray<uint16, GrowthFunctions::Double> Grid;
	DynArray<Vec3f, GrowthFunctions::Double> Positions;

	/// Grid points that were left without a probe because ProbePlacementBoxes::IsOpen() turned them down
	uint32 VoidPointCount = 0;

	uint32 GetProbeCount() const { return Positions.Size; }
	uint32 GetGridPointCount() const { return Grid.Size; }

	/**
	 * @brief Adds `count` grid points to the end of the table and returns the first of them
	 */
	uint16* AddGridPoints(uint32 count)
	{
		const uint32 first = Grid.Size;

		for (uint32 i = 0; i < count; i++) {
			Grid.Emplace(Limits::ProbeGridEmpty);
		}

		return (count > 0) ? &Grid[first] : nullptr;
	}

	/**
	 * @brief Drops the grid points and probes past the given counts
	 */
	void Truncate(uint32 grid_point_count, uint32 probe_count)
	{
		while (Grid.Size > grid_point_count) {
			Grid.RemoveLast();
		}

		while (Positions.Size > probe_count) {
			Positions.RemoveLast();
		}
	}

	/**
	 * @brief Makes this a copy of another layout. The arrays can't be copied by assignment.
	 */
	void CopyFrom(const ProbeLayout& other)
	{
		Clear();

		VolumeCount = other.VolumeCount;
		VoidPointCount = other.VoidPointCount;

		for (uint32 volume = 0; volume < VolumeCount; volume++) {
			Volumes[volume] = other.Volumes[volume];
			Ranges[volume] = other.Ranges[volume];
		}

		for (const uint16 point : other.Grid) {
			Grid.Emplace(point);
		}

		for (const Vec3f position : other.Positions) {
			Positions.Emplace(position);
		}
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

/**
 * @brief Adds a solid for probes to stay out of.
 * @param planes The bounding planes of a brush that isn't a box, in local space. Empty for a box.
 * @returns False if the solid is too big to be level geometry (the sky) and was left out
 */
bool AddBox(ProbePlacementBoxes& boxes, const BBox& local_bounds, const Mat4f& local_to_world,
			const SizedArray<ProbePlacementBoxes::Plane>& planes);

/**
 * @brief Adds a solid that probes stay out of without being placed around it, see ProbePlacementBoxes::Bounds
 */
void AddBound(ProbePlacementBoxes& boxes, const BBox& local_bounds, const Mat4f& local_to_world,
			  const SizedArray<ProbePlacementBoxes::Plane>& planes);

bool CheckGridSize(const ProbeGridSize& grid);

/**
 * @brief True if a probe that belongs at `grid_position` can sit at `position`: clear of every box, and not on the
 * far side of one
 */
bool IsPositionValid(const Vec3f grid_position, const Vec3f position, const ProbePlacementBoxes& boxes);

/**
 * @brief Searches around `grid_position` for the valid position closest to `preferred`
 */
bool FindValidPosition(const Vec3f grid_position, const Vec3f preferred, const Vec3f max_relocation,
					   const ProbePlacementBoxes& boxes, Vec3f& out_position);
void FinaliseVolumeBounds(ProbeVolumeData& volume);

/**
 * @brief Adds a volume to the layout and places its probes
 */
bool AddVolume(ProbeLayout& layout, const Vec3f volume_min, const Vec3f volume_size, const ProbeGridSize& grid,
			   const ProbePlacementBoxes& boxes, eProbeFill fill);

/**
 * @brief Adds the coarse volume that covers all of the level
 */
bool AddLevelBaseVolume(ProbeLayout& layout, const ProbePlacementBoxes& boxes);

/**
 * @brief Adds a volume with probes near surfaces only, coarsening `spacing` until it fits the probes that are left
 */
bool AddSurfaceVolumeForBudget(ProbeLayout& layout, const Vec3f region_min, const Vec3f region_max, float32 spacing,
							   const ProbePlacementBoxes& boxes, bool cell_centred);

/**
 * @brief Replaces the layout with the one for a whole level: the base volume, a surface volume per probe volume brush,
 * and a surface volume over the level
 * @param brush_regions World bounds of the probe volume brushes
 * @returns The number of brush regions that got a volume
 */
uint32 BuildLevelLayout(ProbeLayout& layout, const ProbePlacementBoxes& boxes, const Slice<BBox>& brush_regions,
						const ProbePlacementSettings& settings);

} // namespace ProbePlacement

} // namespace fx
