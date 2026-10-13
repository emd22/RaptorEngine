#include "ProbePlacement.hpp"

#include <Brush.hpp>
#include <Core/Log.hpp>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace fx {
namespace {
constexpr float32 scProbeSurfaceOffset = 0.25f;
constexpr float32 scProbeHugSurfaceOffset = 1.0f;

constexpr float32 scProbeInsideSkin = 0.05f;

constexpr float32 scProbeHugMaxDistance = 1.5f;
constexpr uint32 scMaxPushOutIterations = 8;

constexpr float32 scMaxProbeRelocation = 0.45f;

constexpr float32 scProbeCrossingSkin = 0.01f;
constexpr float32 scProbeMinClearance = 2.0f * ProbePlacement::scCaptureNearPlane;
constexpr uint32 scRelocationSearchSteps = 5;

constexpr float32 scMaxPlacementObjectSize = 100.0f;

constexpr float32 scMaxProbeSpacing = 64.0f;
constexpr float32 scSurfaceSpacingStep = 1.25f;

constexpr float32 scBaseProbeSpacing = 2.0f;
constexpr uint32 scBaseProbeGridBudget = 512;

constexpr float32 scSurfaceCellMargin = 0.05f;

constexpr float32 scAxisAlignedNormal = 0.999f;

constexpr float32 scMinAlignedSpacing = 0.5f;
constexpr uint32 scAlignSteps = 4;

using NeighbourList = StackArray<Vec3f, 26>;

constexpr float32 scShaderNormalBiasScale = 0.2f;
constexpr float32 scShaderNormalBiasMin = 0.02f;
constexpr float32 scShaderNormalBiasMax = 0.5f;

constexpr uint8 scGridPointOpen = 0;
constexpr uint8 scGridPointNeeded = 1;
constexpr uint8 scGridPointSolid = 2;

using Box = ProbePlacementBoxes::Box;
using Plane = ProbePlacementBoxes::Plane;

Vec3f TransformPoint(const Mat4f& matrix, const Vec3f point)
{
	const Vec4f result = matrix * Vec4f(point.X, point.Y, point.Z, 1.0f);
	return Vec3f(result.X, result.Y, result.Z);
}

Vec3f GridPosition(const Vec3f volume_min, const Vec3f cell_size, uint32 ix, uint32 iy, uint32 iz)
{
	return volume_min + cell_size * Vec3f(static_cast<float32>(ix), static_cast<float32>(iy), static_cast<float32>(iz));
}

uint32 SlicesForExtent(float32 extent, float32 spacing)
{
	const float32 slices = std::max(extent, 0.0f) / std::max(spacing, 1e-3f);
	return std::max(static_cast<uint32>(std::lround(slices)), Limits::MinProbeGridDim);
}

Vec3f ToBoxLocal(const Vec3f point, const Box& box) { return TransformPoint(box.WorldToLocal, point); }

bool IsInsideBox(const Vec3f point, const Box& box, float32 skin)
{
	const Vec3f local = ToBoxLocal(point, box);

	if (!box.Planes.IsEmpty()) {
		for (const Plane& plane : box.Planes) {
			if (plane.Normal.Dot(local) - plane.Distance >= skin) {
				return false;
			}
		}

		return true;
	}

	for (uint32 axis = 0; axis < 3; axis++) {
		if (local.mData[axis] <= box.LocalBounds.Min.mData[axis] - skin ||
			local.mData[axis] >= box.LocalBounds.Max.mData[axis] + skin) {
			return false;
		}
	}

	return true;
}

bool IsInsideAnyBox(const Vec3f point, const ProbePlacementBoxes& boxes)
{
	for (const Box& box : boxes.Boxes) {
		if (IsInsideBox(point, box, scProbeInsideSkin)) {
			return true;
		}
	}

	return false;
}

bool IsInsideAnyBound(const Vec3f point, const ProbePlacementBoxes& boxes)
{
	for (const Box& bound : boxes.Bounds) {
		if (IsInsideBox(point, bound, 0.0f)) {
			return true;
		}
	}

	return false;
}

bool IsInsideLevel(const Vec3f point, const ProbePlacementBoxes& boxes)
{
	for (uint32 axis = 0; axis < 3; axis++) {
		if (point.mData[axis] < boxes.Min.mData[axis] || point.mData[axis] > boxes.Max.mData[axis]) {
			return false;
		}
	}

	return true;
}

Vec3f ClosestPointInBox(const Vec3f local, const Box& box)
{
	if (box.Planes.IsEmpty()) {
		return Vec3f::Clamp(local, box.LocalBounds.Min, box.LocalBounds.Max);
	}

	constexpr uint32 cMaxIterations = 32;
	constexpr float32 cConvergedSquared = 1e-8f;

	Vec3f corrections[Brush::scMaxPlanes] = {};

	const uint32 plane_count = std::min(static_cast<uint32>(box.Planes.Size), Brush::scMaxPlanes);

	Vec3f closest = local;

	for (uint32 iteration = 0; iteration < cMaxIterations; iteration++) {
		float32 moved_squared = 0.0f;

		for (uint32 i = 0; i < plane_count; i++) {
			const Plane& plane = box.Planes[i];

			const Vec3f shifted = closest + corrections[i];
			const float32 outside = plane.Normal.Dot(shifted) - plane.Distance;
			const Vec3f projected = (outside > 0.0f) ? shifted - plane.Normal * outside : shifted;

			corrections[i] = shifted - projected;

			const Vec3f step = projected - closest;
			moved_squared = std::max(moved_squared, step.Dot(step));
			closest = projected;
		}

		if (moved_squared < cConvergedSquared) {
			break;
		}
	}

	return closest;
}

Vec3f ClosestPointOnBox(const Vec3f point, const Box& box)
{
	return TransformPoint(box.LocalToWorld, ClosestPointInBox(ToBoxLocal(point, box), box));
}

float32 DistanceToBox(const Vec3f point, const Box& box) { return (point - ClosestPointOnBox(point, box)).Length(); }

bool SegmentCrossesBox(const Vec3f from, const Vec3f to, const Box& box, float32 skin)
{
	const Vec3f local_from = ToBoxLocal(from, box);
	const Vec3f local_to = ToBoxLocal(to, box);

	float32 t_enter = 0.0f;
	float32 t_exit = 1.0f;

	if (!box.Planes.IsEmpty()) {
		for (const Plane& plane : box.Planes) {
			const float32 limit = plane.Distance - skin;
			const float32 start = plane.Normal.Dot(local_from);
			const float32 delta = plane.Normal.Dot(local_to) - start;

			if (std::abs(delta) < 1e-6f) {
				if (start >= limit) {
					return false;
				}

				continue;
			}

			const float32 t = (limit - start) / delta;

			if (delta < 0.0f) {
				t_enter = std::max(t_enter, t);
			}
			else {
				t_exit = std::min(t_exit, t);
			}

			if (t_enter >= t_exit) {
				return false;
			}
		}

		return true;
	}

	for (uint32 axis = 0; axis < 3; axis++) {
		const float32 lo = box.LocalBounds.Min.mData[axis] + skin;
		const float32 hi = box.LocalBounds.Max.mData[axis] - skin;
		const float32 start = local_from.mData[axis];
		const float32 delta = local_to.mData[axis] - start;

		if (std::abs(delta) < 1e-6f) {
			if (start <= lo || start >= hi) {
				return false;
			}

			continue;
		}

		float32 t0 = (lo - start) / delta;
		float32 t1 = (hi - start) / delta;

		if (t0 > t1) {
			std::swap(t0, t1);
		}

		t_enter = std::max(t_enter, t0);
		t_exit = std::min(t_exit, t1);

		if (t_enter >= t_exit) {
			return false;
		}
	}

	return true;
}

bool IsProbePlacementValid(const Vec3f grid_position, const Vec3f position, const ProbePlacementBoxes& boxes)
{
	if (boxes.IsEmpty()) {
		return true;
	}

	if (!IsInsideLevel(position, boxes)) {
		return false;
	}

	if (IsInsideAnyBound(position, boxes) || (boxes.IsOpen && !boxes.IsOpen(position))) {
		return false;
	}

	for (const Box& box : boxes.Boxes) {
		if (DistanceToBox(position, box) < scProbeMinClearance) {
			return false;
		}

		if (!IsInsideBox(grid_position, box, scProbeInsideSkin) &&
			SegmentCrossesBox(grid_position, position, box, scProbeCrossingSkin)) {
			return false;
		}
	}

	return true;
}

uint32 GetFaceCount(const Box& box) { return box.Planes.IsEmpty() ? 6 : static_cast<uint32>(box.Planes.Size); }

float32 GetFacePenetration(const Vec3f local, const Box& box, uint32 face)
{
	if (!box.Planes.IsEmpty()) {
		const Plane& plane = box.Planes[face];
		return plane.Distance - plane.Normal.Dot(local);
	}

	const uint32 axis = face / 2;

	return ((face & 1) != 0) ? box.LocalBounds.Max.mData[axis] - local.mData[axis]
							 : local.mData[axis] - box.LocalBounds.Min.mData[axis];
}

Vec3f FaceExitPoint(const Vec3f local, const Box& box, uint32 face, Vec3f& out_world_normal)
{
	Vec3f exit_local = local;
	Vec3f local_normal = Vec3f::sZero;

	if (!box.Planes.IsEmpty()) {
		const Plane& plane = box.Planes[face];

		exit_local = local + plane.Normal * (plane.Distance - plane.Normal.Dot(local));
		local_normal = plane.Normal;
	}
	else {
		const uint32 axis = face / 2;
		const bool through_max = (face & 1) != 0;

		exit_local.mData[axis] = through_max ? box.LocalBounds.Max.mData[axis] : box.LocalBounds.Min.mData[axis];
		local_normal.mData[axis] = through_max ? 1.0f : -1.0f;
	}

	const Vec4f world_normal = box.LocalToWorld * Vec4f(local_normal.X, local_normal.Y, local_normal.Z, 0.0f);
	out_world_normal = Vec3f(world_normal.X, world_normal.Y, world_normal.Z).Normalize();

	return TransformPoint(box.LocalToWorld, exit_local);
}

Vec3f ExitThroughFace(const Vec3f local, const Box& box, uint32 face)
{
	Vec3f world_normal;
	const Vec3f exit = FaceExitPoint(local, box, face, world_normal);

	return exit + world_normal * scProbeSurfaceOffset;
}

float32 DistanceToBoxSurface(const Vec3f point, const Box& box)
{
	if (!IsInsideBox(point, box, 0.0f)) {
		return DistanceToBox(point, box);
	}

	const Vec3f local = ToBoxLocal(point, box);

	float32 nearest = std::numeric_limits<float32>::max();

	for (uint32 face = 0; face < GetFaceCount(box); face++) {
		Vec3f world_normal;
		nearest = std::min(nearest, (FaceExitPoint(local, box, face, world_normal) - point).Length());
	}

	return nearest;
}

enum class eCellOverlap
{
	Outside,
	Crossing,
	Inside,
};

eCellOverlap ClassifyCell(const Vec3f cell_min, const Vec3f cell_max, const Box& box)
{
	constexpr uint32 cCornerCount = 8;

	Vec3f corners[cCornerCount];

	for (uint32 corner = 0; corner < cCornerCount; corner++) {
		const Vec3f world((corner & 1) ? cell_max.X : cell_min.X, (corner & 2) ? cell_max.Y : cell_min.Y,
						  (corner & 4) ? cell_max.Z : cell_min.Z);

		corners[corner] = ToBoxLocal(world, box);
	}

	bool inside = true;

	for (uint32 face = 0; face < GetFaceCount(box); face++) {
		float32 deepest = -std::numeric_limits<float32>::max();
		float32 shallowest = std::numeric_limits<float32>::max();

		for (const Vec3f& corner : corners) {
			const float32 penetration = GetFacePenetration(corner, box, face);

			deepest = std::max(deepest, penetration);
			shallowest = std::min(shallowest, penetration);
		}

		if (deepest < 0.0f) {
			return eCellOverlap::Outside;
		}

		inside &= (shallowest >= 0.0f);
	}

	return inside ? eCellOverlap::Inside : eCellOverlap::Crossing;
}

bool CellTouchesSurface(const Vec3f cell_min, const Vec3f cell_max, float32 pad, const ProbePlacementBoxes& boxes)
{
	const Vec3f padded_min = cell_min - Vec3f(pad);
	const Vec3f padded_max = cell_max + Vec3f(pad);

	for (const Box& box : boxes.Boxes) {
		const BBox& bounds = box.WorldBounds;

		bool overlaps = true;
		bool contained = true;

		for (uint32 axis = 0; axis < 3; axis++) {
			overlaps &= padded_min.mData[axis] < bounds.Max.mData[axis] &&
						padded_max.mData[axis] > bounds.Min.mData[axis];
			contained &= padded_min.mData[axis] >= bounds.Min.mData[axis] &&
						 padded_max.mData[axis] <= bounds.Max.mData[axis];
		}

		if (!overlaps) {
			continue;
		}

		if (box.bAxisAligned) {
			if (!contained) {
				return true;
			}

			continue;
		}

		if (ClassifyCell(padded_min, padded_max, box) == eCellOverlap::Crossing) {
			return true;
		}
	}

	return false;
}

float32 ShaderNormalBias(const Vec3f cell_size)
{
	const float32 min_spacing = std::min({ cell_size.X, cell_size.Y, cell_size.Z });
	return std::clamp(scShaderNormalBiasScale * min_spacing, scShaderNormalBiasMin, scShaderNormalBiasMax);
}

bool IsBuriedTooDeep(const Vec3f point, const Vec3f max_relocation, const ProbePlacementBoxes& boxes)
{
	for (const Box& box : boxes.Boxes) {
		if (!IsInsideBox(point, box, 0.0f)) {
			continue;
		}

		if (!box.bAxisAligned) {
			if (DistanceToBoxSurface(point, box) + scProbeMinClearance > max_relocation.Length()) {
				return true;
			}

			continue;
		}

		bool can_escape = false;

		for (uint32 axis = 0; axis < 3; axis++) {
			const float32 to_min = point.mData[axis] - box.WorldBounds.Min.mData[axis];
			const float32 to_max = box.WorldBounds.Max.mData[axis] - point.mData[axis];

			can_escape |= std::min(to_min, to_max) + scProbeMinClearance <= max_relocation.mData[axis];
		}

		if (!can_escape) {
			return true;
		}
	}

	return false;
}

uint32 FindNeededGridPoints(const Vec3f volume_min, const Vec3f cell_size, const ProbeGridSize& grid,
							const ProbePlacementBoxes& boxes, eProbeFill fill, SizedArray<uint8>& out_needed,
							uint32* out_void_count = nullptr)
{
	uint32 void_count = 0;

	const uint32 dims[3] = { grid.X, grid.Y, grid.Z };
	const uint32 num_points = grid.GetProbeCount();

	out_needed.InitCapacity(num_points);
	out_needed.MarkFull();

	std::fill(out_needed.begin(), out_needed.end(),
			  (fill == eProbeFill::Dense || boxes.Boxes.Size == 0) ? scGridPointNeeded : scGridPointOpen);

	if (boxes.Boxes.Size == 0) {
		return num_points;
	}

	const float32 pad = ShaderNormalBias(cell_size) + scSurfaceCellMargin;

	for (uint32 iz = 0; fill == eProbeFill::Surface && iz + 1 < dims[2]; iz++) {
		for (uint32 iy = 0; iy + 1 < dims[1]; iy++) {
			for (uint32 ix = 0; ix + 1 < dims[0]; ix++) {
				const Vec3f cell_min = GridPosition(volume_min, cell_size, ix, iy, iz);

				if (!CellTouchesSurface(cell_min, cell_min + cell_size, pad, boxes)) {
					continue;
				}

				for (uint32 corner = 0; corner < 8; corner++) {
					const uint32 cx = ix + (corner & 1);
					const uint32 cy = iy + ((corner >> 1) & 1);
					const uint32 cz = iz + ((corner >> 2) & 1);

					out_needed[cx + dims[0] * (cy + dims[1] * cz)] = scGridPointNeeded;
				}
			}
		}
	}

	const Vec3f max_relocation = cell_size * scMaxProbeRelocation;

	uint32 count = 0;
	uint32 point = 0;

	for (uint32 iz = 0; iz < dims[2]; iz++) {
		for (uint32 iy = 0; iy < dims[1]; iy++) {
			for (uint32 ix = 0; ix < dims[0]; ix++, point++) {
				const Vec3f position = GridPosition(volume_min, cell_size, ix, iy, iz);

				if (out_needed[point] == scGridPointOpen) {
					if (IsInsideAnyBox(position, boxes)) {
						out_needed[point] = scGridPointSolid;
					}

					continue;
				}

				if (IsBuriedTooDeep(position, max_relocation, boxes)) {
					out_needed[point] = scGridPointSolid;
					continue;
				}

				if (IsInsideAnyBound(position, boxes) || (boxes.IsOpen && !boxes.IsOpen(position))) {
					out_needed[point] = scGridPointSolid;
					void_count++;
					continue;
				}

				count++;
			}
		}
	}

	if (out_void_count != nullptr) {
		*out_void_count = void_count;
	}

	return count;
}

bool PushOutOfBox(Vec3f& point, const Box& box)
{
	if (!IsInsideBox(point, box, scProbeInsideSkin)) {
		return false;
	}

	const Vec3f local = ToBoxLocal(point, box);

	uint32 exit_face = 0;
	float32 exit_distance = std::numeric_limits<float32>::max();

	for (uint32 face = 0; face < GetFaceCount(box); face++) {
		const float32 penetration = GetFacePenetration(local, box, face);

		if (penetration < exit_distance) {
			exit_distance = penetration;
			exit_face = face;
		}
	}

	point = ExitThroughFace(local, box, exit_face);

	return true;
}

bool PushOutOfBoxes(Vec3f& point, const ProbePlacementBoxes& boxes)
{
	constexpr uint32 cNoExit = 3;

	uint32 best_rank = cNoExit;
	float32 best_distance = std::numeric_limits<float32>::max();
	Vec3f best_exit = point;
	bool inside = false;

	for (const Box& box : boxes.Boxes) {
		if (!IsInsideBox(point, box, scProbeInsideSkin)) {
			continue;
		}

		inside = true;

		const Vec3f local = ToBoxLocal(point, box);

		for (uint32 face = 0; face < GetFaceCount(box); face++) {
			const Vec3f exit = ExitThroughFace(local, box, face);
			const float32 distance = (exit - point).Length();

			uint32 rank = cNoExit;

			if (IsProbePlacementValid(point, exit, boxes)) {
				rank = 0;
			}
			else if (!IsInsideAnyBox(exit, boxes)) {
				rank = IsInsideLevel(exit, boxes) ? 1 : 2;
			}

			if (rank < best_rank || (rank == best_rank && rank != cNoExit && distance < best_distance)) {
				best_rank = rank;
				best_distance = distance;
				best_exit = exit;
			}
		}
	}

	if (!inside) {
		return false;
	}

	if (best_rank != cNoExit) {
		point = best_exit;
		return true;
	}

	for (uint32 iteration = 0; iteration < scMaxPushOutIterations; iteration++) {
		bool pushed = false;

		for (uint32 i = 0; i < boxes.Boxes.Size && !pushed; i++) {
			pushed = PushOutOfBox(point, boxes.Boxes[i]);
		}

		if (!pushed) {
			break;
		}
	}

	return true;
}

float32 DistanceToOtherBoxes(const Vec3f point, const ProbePlacementBoxes& boxes, uint32 skip_box)
{
	float32 nearest = std::numeric_limits<float32>::max();

	for (uint32 i = 0; i < boxes.Boxes.Size; i++) {
		if (i != skip_box) {
			nearest = std::min(nearest, DistanceToBox(point, boxes.Boxes[i]));
		}
	}

	return nearest;
}

bool HugNearestSurface(Vec3f& point, const ProbePlacementBoxes& boxes, float32 max_distance)
{
	float32 best_distance = max_distance;
	uint32 best_box = 0;
	Vec3f best_closest = point;
	bool found = false;

	for (uint32 i = 0; i < boxes.Boxes.Size; i++) {
		const Vec3f closest = ClosestPointOnBox(point, boxes.Boxes[i]);
		const float32 distance = (point - closest).Length();

		if (distance < 1e-6f || distance >= best_distance) {
			continue;
		}

		best_distance = distance;
		best_box = i;
		best_closest = closest;
		found = true;
	}

	if (!found) {
		return false;
	}

	const Vec3f direction = (point - best_closest) * (1.0f / best_distance);

	float32 target = scProbeHugSurfaceOffset;

	const auto keeps_clear = [&](float32 along)
	{ return DistanceToOtherBoxes(best_closest + direction * along, boxes, best_box) >= along; };

	if (target > best_distance && !keeps_clear(target)) {
		float32 clear = best_distance;
		float32 blocked = target;

		for (uint32 step = 0; step < 16; step++) {
			const float32 middle = (clear + blocked) * 0.5f;

			if (keeps_clear(middle)) {
				clear = middle;
			}
			else {
				blocked = middle;
			}
		}

		target = clear;
	}

	point = best_closest + direction * target;
	return true;
}

bool AlignToSlantedSurface(Vec3f& point, const Vec3f grid_position, const Vec3f cell_size, const Vec3f max_relocation,
						   const ProbePlacementBoxes& boxes, const NeighbourList& neighbours)
{
	float32 best_distance = std::numeric_limits<float32>::max();
	Vec3f best_closest = point;
	bool found = false;

	for (const Box& box : boxes.Boxes) {
		const Vec3f closest = ClosestPointOnBox(point, box);
		const float32 distance = (point - closest).Length();

		if (distance < 1e-6f || distance >= best_distance) {
			continue;
		}

		best_distance = distance;
		best_closest = closest;
		found = !box.bAxisAligned;
	}

	if (!found) {
		return false;
	}

	const Vec3f normal = (point - best_closest) * (1.0f / best_distance);

	if (std::max({ std::abs(normal.X), std::abs(normal.Y), std::abs(normal.Z) }) > scAxisAlignedNormal) {
		return false;
	}

	const float32 staircase = std::abs(normal.X) * cell_size.X + std::abs(normal.Y) * cell_size.Y +
							  std::abs(normal.Z) * cell_size.Z;

	if (best_distance > staircase + scSurfaceCellMargin) {
		return false;
	}

	const float32 target = std::max(0.5f * staircase, scProbeMinClearance + scSurfaceCellMargin);
	const Vec3f move = normal * (target - best_distance);

	float32 fraction = 1.0f;

	for (uint32 axis = 0; axis < 3; axis++) {
		const float32 step = move.mData[axis];

		if (std::abs(step) < 1e-6f) {
			continue;
		}

		const float32 limit = (step > 0.0f) ? (grid_position.mData[axis] + max_relocation.mData[axis])
											: (grid_position.mData[axis] - max_relocation.mData[axis]);

		fraction = std::min(fraction, std::max((limit - point.mData[axis]) / step, 0.0f));
	}

	const float32 min_spacing = scMinAlignedSpacing * std::min({ cell_size.X, cell_size.Y, cell_size.Z });

	const auto keeps_apart = [&](const Vec3f candidate)
	{
		for (const Vec3f neighbour : neighbours) {
			const float32 spacing = (candidate - neighbour).Length();

			if (spacing < min_spacing && spacing < (point - neighbour).Length()) {
				return false;
			}
		}

		return true;
	};

	for (uint32 step = 0; step < scAlignSteps && fraction > 0.0f; step++, fraction *= 0.5f) {
		const Vec3f aligned = point + move * fraction;

		if (keeps_apart(aligned) && IsProbePlacementValid(grid_position, aligned, boxes)) {
			point = aligned;
			return true;
		}
	}

	return false;
}

bool FindValidProbePosition(const Vec3f grid_position, const Vec3f preferred, const Vec3f max_relocation,
							const ProbePlacementBoxes& boxes, Vec3f& out_position)
{
	constexpr float32 cStepScale = 2.0f / static_cast<float32>(scRelocationSearchSteps - 1);

	float32 best_distance = std::numeric_limits<float32>::max();
	bool found = false;

	for (uint32 iz = 0; iz < scRelocationSearchSteps; iz++) {
		for (uint32 iy = 0; iy < scRelocationSearchSteps; iy++) {
			for (uint32 ix = 0; ix < scRelocationSearchSteps; ix++) {
				const Vec3f step = Vec3f(static_cast<float32>(ix), static_cast<float32>(iy), static_cast<float32>(iz)) *
									   cStepScale -
								   Vec3f(1.0f);

				const Vec3f candidate = grid_position + max_relocation * step;
				const float32 distance = (candidate - preferred).Length();

				if (distance >= best_distance || !IsProbePlacementValid(grid_position, candidate, boxes)) {
					continue;
				}

				best_distance = distance;
				out_position = candidate;
				found = true;
			}
		}
	}

	return found;
}

void FitCellCentred(const Vec3f region_min, const Vec3f region_max, const ProbeGridSize& grid, Vec3f& out_min,
					Vec3f& out_size)
{
	const Vec3f region_size = region_max - region_min;
	const Vec3f slices(static_cast<float32>(grid.X), static_cast<float32>(grid.Y), static_cast<float32>(grid.Z));
	const Vec3f inset = region_size / (slices * 2.0f);

	out_min = region_min + inset;
	out_size = region_size - inset * 2.0f;
}

ProbeGridSize GridForSpacing(const Vec3f region_size, float32 spacing, bool cell_centred)
{
	const uint32 extra = cell_centred ? 0 : 1;

	return ProbeGridSize {
		SlicesForExtent(region_size.X, spacing) + extra,
		SlicesForExtent(region_size.Y, spacing) + extra,
		SlicesForExtent(region_size.Z, spacing) + extra,
	};
}

void LayoutGrid(const Vec3f region_min, const Vec3f region_max, const ProbeGridSize& grid, bool cell_centred,
				Vec3f& out_min, Vec3f& out_size)
{
	if (cell_centred) {
		FitCellCentred(region_min, region_max, grid, out_min, out_size);
		return;
	}

	out_min = region_min;
	out_size = region_max - region_min;
}

Vec3f GridCellSize(const Vec3f volume_size, const ProbeGridSize& grid)
{
	const Vec3f num_cells(static_cast<float32>(grid.X - 1), static_cast<float32>(grid.Y - 1),
						  static_cast<float32>(grid.Z - 1));

	return volume_size / num_cells;
}

bool PlaceVolumeProbes(ProbeLayout& layout, const Vec3f volume_min, const Vec3f volume_size, const ProbeGridSize& grid,
					   const ProbePlacementBoxes& boxes, eProbeFill fill)
{
	const uint32 volume_index = layout.VolumeCount;
	const uint32 dims[3] = { grid.X, grid.Y, grid.Z };

	const Vec3f num_cells(static_cast<float32>(dims[0] - 1), static_cast<float32>(dims[1] - 1),
						  static_cast<float32>(dims[2] - 1));

	const Vec3f cell_size = volume_size / num_cells;

	const float32 hug_distance = std::min(
		{ scProbeHugMaxDistance, 0.5f * cell_size.X, 0.5f * cell_size.Y, 0.5f * cell_size.Z });

	const Vec3f max_relocation = cell_size * scMaxProbeRelocation;

	SizedArray<uint8> needed;
	uint32 num_void = 0;
	const uint32 num_needed = FindNeededGridPoints(volume_min, cell_size, grid, boxes, fill, needed, &num_void);

	const uint32 first_probe = layout.GetProbeCount();
	const uint32 first_point = layout.GetGridPointCount();

	layout.AddGridPoints(grid.GetProbeCount());

	uint32 num_pushed = 0;
	uint32 num_hugged = 0;
	uint32 num_aligned = 0;
	NeighbourList neighbours;
	uint32 num_relocated = 0;
	uint32 num_unplaced = 0;
	uint32 point_index = first_point;

	for (uint32 iz = 0; iz < dims[2]; iz++) {
		for (uint32 iy = 0; iy < dims[1]; iy++) {
			for (uint32 ix = 0; ix < dims[0]; ix++, point_index++) {
				const uint8 point_state = needed[point_index - first_point];

				layout.Grid[point_index] = (point_state == scGridPointSolid) ? Limits::ProbeGridSolid
																			 : Limits::ProbeGridEmpty;

				if (point_state != scGridPointNeeded) {
					continue;
				}

				const Vec3f grid_position = GridPosition(volume_min, cell_size, ix, iy, iz);
				Vec3f position = grid_position;

				if (PushOutOfBoxes(position, boxes)) {
					num_pushed++;
				}

				if (HugNearestSurface(position, boxes, hug_distance)) {
					PushOutOfBoxes(position, boxes);
					num_hugged++;
				}

				position = Vec3f::Clamp(position, grid_position - max_relocation, grid_position + max_relocation);

				if (!IsProbePlacementValid(grid_position, position, boxes)) {
					Vec3f fallback;

					if (!FindValidProbePosition(grid_position, position, max_relocation, boxes, fallback)) {
						layout.Grid[point_index] = Limits::ProbeGridSolid;
						num_unplaced++;
						continue;
					}

					position = fallback;
					num_relocated++;
				}

				if (layout.GetProbeCount() >= Limits::MaxIrradianceProbes) {
					LogWarning("Probe volume rejected: it needs more than the {} probes left of the {} probe budget",
							   Limits::MaxIrradianceProbes - first_probe, Limits::MaxIrradianceProbes);

					layout.Truncate(first_point, first_probe);
					return false;
				}

				layout.Grid[point_index] = static_cast<uint16>(layout.GetProbeCount());
				layout.Positions.Emplace(position);
			}
		}
	}

	point_index = first_point;

	for (uint32 iz = 0; fill == eProbeFill::Surface && iz < dims[2]; iz++) {
		for (uint32 iy = 0; iy < dims[1]; iy++) {
			for (uint32 ix = 0; ix < dims[0]; ix++, point_index++) {
				const uint16 probe = layout.Grid[point_index];

				if (probe >= Limits::ProbeGridSolid) {
					continue;
				}

				neighbours.Clear();

				for (int32 dz = -1; dz <= 1; dz++) {
					for (int32 dy = -1; dy <= 1; dy++) {
						for (int32 dx = -1; dx <= 1; dx++) {
							const int32 nx = static_cast<int32>(ix) + dx;
							const int32 ny = static_cast<int32>(iy) + dy;
							const int32 nz = static_cast<int32>(iz) + dz;

							if ((dx == 0 && dy == 0 && dz == 0) || nx < 0 || ny < 0 || nz < 0 ||
								nx >= static_cast<int32>(dims[0]) || ny >= static_cast<int32>(dims[1]) ||
								nz >= static_cast<int32>(dims[2])) {
								continue;
							}

							const uint16 neighbour =
								layout.Grid[first_point + static_cast<uint32>(nx) +
											dims[0] * (static_cast<uint32>(ny) + dims[1] * static_cast<uint32>(nz))];

							if (neighbour < Limits::ProbeGridSolid) {
								neighbours.Insert(layout.Positions[neighbour]);
							}
						}
					}
				}

				const Vec3f grid_position = GridPosition(volume_min, cell_size, ix, iy, iz);

				if (AlignToSlantedSurface(layout.Positions[probe], grid_position, cell_size, max_relocation, boxes,
										  neighbours)) {
					num_aligned++;
				}
			}
		}
	}

	ProbeVolumeData& volume = layout.Volumes[volume_index];
	volume = ProbeVolumeData {};

	for (uint32 axis = 0; axis < 3; axis++) {
		volume.MinAndCount[axis] = volume_min.mData[axis];
		volume.InvCellSize[axis] = (volume_size.mData[axis] > 1e-4f) ? (num_cells.mData[axis] / volume_size.mData[axis])
																	 : 1.0f;
		volume.DimsAndFirst[axis] = dims[axis];
	}

	volume.InvCellSize[3] = 0.0f;
	volume.DimsAndFirst[3] = first_point;

	ProbePlacement::FinaliseVolumeBounds(volume);

	layout.Ranges[volume_index] = ProbeVolumeRange { first_probe, layout.GetProbeCount() - first_probe };
	layout.VoidPointCount += num_void;

	LogInfo("Probe volume {} ({}): min={} size={} grid={}x{}x{}, {} of {} grid points needed, {} probes from index {} "
			"({} pushed out, {} hugging, {} lined up with slanted faces, {} relocated, {} unplaced)",
			volume_index, (fill == eProbeFill::Dense) ? "dense" : "surface", volume_min, volume_size, dims[0], dims[1],
			dims[2], num_needed, grid.GetProbeCount(), layout.GetProbeCount() - first_probe, first_probe, num_pushed,
			num_hugged, num_aligned, num_relocated, num_unplaced);

	return true;
}

} // namespace

namespace ProbePlacement {
void AddBound(ProbePlacementBoxes& boxes, const BBox& local_bounds, const Mat4f& local_to_world,
			  const SizedArray<ProbePlacementBoxes::Plane>& planes)
{
	Box& bound = boxes.Bounds.Emplace();

	bound.LocalBounds = local_bounds;
	bound.WorldBounds = OBBox::FromLocalBounds(local_bounds, local_to_world).GetWorldAABB();
	bound.LocalToWorld = local_to_world;
	bound.WorldToLocal = local_to_world.Inverse();
	bound.Planes.InitAsCopyOf(planes);
}

bool AddBox(ProbePlacementBoxes& boxes, const BBox& local_bounds, const Mat4f& local_to_world,
			const SizedArray<ProbePlacementBoxes::Plane>& planes)
{
	const BBox world_bounds = OBBox::FromLocalBounds(local_bounds, local_to_world).GetWorldAABB();

	if ((world_bounds.Max - world_bounds.Min).Length() > scMaxPlacementObjectSize) {
		return false;
	}

	if (boxes.Boxes.Size < ProbePlacementBoxes::scMaxBoxes) {
		Box& box = boxes.Boxes.Emplace();
		box.LocalBounds = local_bounds;
		box.WorldBounds = world_bounds;
		box.LocalToWorld = local_to_world;
		box.WorldToLocal = local_to_world.Inverse();
		box.Planes.InitAsCopyOf(planes);

		constexpr float32 cAxisEpsilon = 1e-5f;

		const Mat4f& m = local_to_world;
		box.bAxisAligned = box.Planes.IsEmpty() && std::abs(m.Rows[0].Y) < cAxisEpsilon &&
						   std::abs(m.Rows[0].Z) < cAxisEpsilon && std::abs(m.Rows[1].X) < cAxisEpsilon &&
						   std::abs(m.Rows[1].Z) < cAxisEpsilon && std::abs(m.Rows[2].X) < cAxisEpsilon &&
						   std::abs(m.Rows[2].Y) < cAxisEpsilon;
	}

	boxes.Min = Vec3f::Min(boxes.Min, world_bounds.Min);
	boxes.Max = Vec3f::Max(boxes.Max, world_bounds.Max);
	boxes.ObjectCount++;

	return true;
}

bool CheckGridSize(const ProbeGridSize& grid)
{
	if (grid.IsValid()) {
		return true;
	}

	LogError("Probe volume rejected: a {}x{}x{} grid needs at least {} probes along each axis", grid.X, grid.Y, grid.Z,
			 Limits::MinProbeGridDim);

	return false;
}

void FinaliseVolumeBounds(ProbeVolumeData& volume)
{
	if (volume.DimsAndFirst[0] == 0 || volume.DimsAndFirst[1] == 0 || volume.DimsAndFirst[2] == 0) {
		volume.MaxAndCellVolume[0] = 0.0f;
		volume.MaxAndCellVolume[1] = 0.0f;
		volume.MaxAndCellVolume[2] = 0.0f;
		volume.MaxAndCellVolume[3] = FLT_MAX;
		return;
	}

	float32 cell_volume = 1.0f;

	for (uint32 axis = 0; axis < 3; axis++) {
		const float32 cell_size = 1.0f / std::max(volume.InvCellSize[axis], 1e-6f);
		const float32 cells = static_cast<float32>(volume.DimsAndFirst[axis] - 1);

		volume.MaxAndCellVolume[axis] = volume.MinAndCount[axis] + (cell_size * cells);
		cell_volume *= cell_size;
	}

	volume.MaxAndCellVolume[3] = cell_volume;
}

bool IsPositionValid(const Vec3f grid_position, const Vec3f position, const ProbePlacementBoxes& boxes)
{
	return IsProbePlacementValid(grid_position, position, boxes);
}

bool FindValidPosition(const Vec3f grid_position, const Vec3f preferred, const Vec3f max_relocation,
					   const ProbePlacementBoxes& boxes, Vec3f& out_position)
{
	return FindValidProbePosition(grid_position, preferred, max_relocation, boxes, out_position);
}

bool AddVolume(ProbeLayout& layout, const Vec3f volume_min, const Vec3f volume_size, const ProbeGridSize& grid,
			   const ProbePlacementBoxes& boxes, eProbeFill fill)
{
	if (!CheckGridSize(grid)) {
		return false;
	}

	if (layout.VolumeCount >= Limits::MaxProbeVolumes) {
		LogError("Probe volume rejected: all {} volume slots are taken", Limits::MaxProbeVolumes);
		return false;
	}

	const uint32 num_points = grid.GetProbeCount();

	if (num_points > Limits::MaxProbeGridPoints - layout.GetGridPointCount()) {
		LogError("Probe volume rejected: its grid has {} points and only {} of the {} grid point budget are left",
				 num_points, Limits::MaxProbeGridPoints - layout.GetGridPointCount(), Limits::MaxProbeGridPoints);
		return false;
	}

	if (!PlaceVolumeProbes(layout, volume_min, volume_size, grid, boxes, fill)) {
		return false;
	}

	layout.VolumeCount++;

	return true;
}

bool AddLevelBaseVolume(ProbeLayout& layout, const ProbePlacementBoxes& boxes)
{
	const Vec3f level_size = boxes.Max - boxes.Min;

	float32 spacing = scBaseProbeSpacing;
	ProbeGridSize grid = GridForSpacing(level_size, spacing, true);

	while (grid.GetProbeCount() > scBaseProbeGridBudget && spacing <= scMaxProbeSpacing) {
		spacing *= 1.5f;
		grid = GridForSpacing(level_size, spacing, true);
	}

	Vec3f volume_min;
	Vec3f volume_size;
	FitCellCentred(boxes.Min, boxes.Max, grid, volume_min, volume_size);

	return AddVolume(layout, volume_min, volume_size, grid, boxes, eProbeFill::Dense);
}

bool AddSurfaceVolumeForBudget(ProbeLayout& layout, const Vec3f region_min, const Vec3f region_max, float32 spacing,
							   const ProbePlacementBoxes& boxes, bool cell_centred)
{
	if (layout.VolumeCount >= Limits::MaxProbeVolumes) {
		LogError("Probe volume rejected: all {} volume slots are taken", Limits::MaxProbeVolumes);
		return false;
	}

	const uint32 probe_budget = Limits::MaxIrradianceProbes - layout.GetProbeCount();
	const uint32 point_budget = Limits::MaxProbeGridPoints - layout.GetGridPointCount();

	SizedArray<uint8> needed;

	for (float32 tried = std::max(spacing, 1e-2f); tried <= scMaxProbeSpacing; tried *= scSurfaceSpacingStep) {
		const ProbeGridSize grid = GridForSpacing(region_max - region_min, tried, cell_centred);

		if (grid.GetProbeCount() > point_budget) {
			continue;
		}

		Vec3f volume_min;
		Vec3f volume_size;
		LayoutGrid(region_min, region_max, grid, cell_centred, volume_min, volume_size);

		const uint32 num_needed = FindNeededGridPoints(volume_min, GridCellSize(volume_size, grid), grid, boxes,
													   eProbeFill::Surface, needed);

		if (num_needed > probe_budget) {
			continue;
		}

		if (!AddVolume(layout, volume_min, volume_size, grid, boxes, eProbeFill::Surface)) {
			continue;
		}

		if (tried > spacing) {
			LogWarning("Probe volume spacing raised from {}m to {}m to fit the {} probes left", spacing, tried,
					   probe_budget);
		}

		return true;
	}

	LogError("Probe volume rejected: no spacing up to {}m fits the {} probes left", scMaxProbeSpacing, probe_budget);
	return false;
}

uint32 BuildLevelLayout(ProbeLayout& layout, const ProbePlacementBoxes& boxes, const Slice<BBox>& brush_regions,
						const ProbePlacementSettings& settings)
{
	layout.Clear();

	AddLevelBaseVolume(layout, boxes);

	uint32 num_brush_volumes = 0;

	for (const BBox& region : brush_regions) {
		if (AddSurfaceVolumeForBudget(layout, region.Min, region.Max, settings.BrushSpacing, boxes, false)) {
			num_brush_volumes++;
		}
		else {
			LogWarning("A probe volume brush was skipped");
		}
	}

	AddSurfaceVolumeForBudget(layout, boxes.Min, boxes.Max, settings.LevelSpacing, boxes, true);

	return num_brush_volumes;
}

} // namespace ProbePlacement

} // namespace fx
