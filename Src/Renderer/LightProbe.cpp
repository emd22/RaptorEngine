/*
 * File:        LightProbe.cpp
 * Author:      emd22
 * Created:     10/09/2026
 * Description: All light probe and light probe grid logic. Irradiance probes with baked L2 SH and depth moments for
 * visibility (VSM & DDGI-ish).
 */


#include "LightProbe.hpp"

#include <Asset/AssetManager.hpp>
#include <Blockout.hpp>
#include <CVar.hpp>
#include <Core/File.hpp>
#include <Engine.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Backend/BarrierHelper.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/ReflectionFilter.hpp>
#include <Util/RustInterop.hpp>
#include <World.hpp>
#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

/// Bump whenever the layout or meaning of the cached data changes
#define FX_PROBE_CACHE_FILE_VERSION 12

namespace fx {

struct ProbePlacementBoxes
{
	struct Plane
	{
		Vec3f Normal;
		float32 Distance;
	};

	struct Box
	{
		AABB LocalBounds;
		AABB WorldBounds;
		bool bAxisAligned = false;
		Mat4f LocalToWorld;
		Mat4f WorldToLocal;

		/// The bounding planes of a brush that isn't a box, in local space. Without them the solid is LocalBounds,
		/// which for a slanted brush would also take in the empty space beside the slope.
		std::vector<Plane> Planes;
	};

	static constexpr uint32 scMaxBoxes = 256;

	Box Boxes[scMaxBoxes];
	uint32 Count = 0;

	Vec3f Min = Vec3f(std::numeric_limits<float32>::max());
	Vec3f Max = Vec3f(-std::numeric_limits<float32>::max());

	bool IsEmpty() const { return Min.X > Max.X; }
};

static_assert(RX_PROBE_FACES == ProbeManager::scCaptureFaces);
static_assert(RX_PROBE_SH_FLOATS == Limits::ProbeSHCoeffCount * 4);
static_assert(RX_PROBE_MOMENT_FLOATS == Limits::ProbeDepthFloatCount);
static_assert(RX_PROBE_DEPTH_SIZE == Limits::ProbeDepthSize);
static_assert(RX_PROBE_DEPTH_MAX_DISTANCE == Limits::ProbeDepthMaxDistance);

namespace {

///////////////////////////////////
// Spherical harmonics
///////////////////////////////////

ProbeSHData MakeSkyGradientProbe(const float32 sky[3], const float32 ground[3])
{
	ProbeSHData probe {};
	rx_probe_sky_gradient(sky, ground, &probe.SH[0][0]);
	return probe;
}

///////////////////////////////////
// Capture
///////////////////////////////////

/// Brightest radiance a capture texel can contribute, so that a few very bright texels can't dominate a probe
constexpr float32 scRadianceClamp = 65000.0f;

constexpr int64 scDefaultProbeBounces = 3;
constexpr int64 scMaxProbeBounces = 8;

/// Surfaces closer than this to a probe are clipped out of its capture, so it sees straight through them
constexpr float32 scCaptureNearPlane = 0.1f;

PerspectiveCamera MakeCaptureCamera(const Vec3f& position, uint32 face)
{
	static const Vec3f scFaceDirections[ProbeManager::scCaptureFaces] = {
		Vec3f(1.0f, 0.0f, 0.0f),  Vec3f(-1.0f, 0.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f),
		Vec3f(0.0f, -1.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.0f),	Vec3f(0.0f, 0.0f, -1.0f),
	};

	static const Vec3f scFaceUps[ProbeManager::scCaptureFaces] = {
		Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.0f),
		Vec3f(0.0f, 0.0f, 1.0f), Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f),
	};

	PerspectiveCamera camera(90.0f, 1.0f, 200.0f, scCaptureNearPlane);

	camera.Position = position;
	camera.ViewMatrix.LookAt(position, position + scFaceDirections[face], scFaceUps[face]);
	camera.UpdateCameraMatrix();

	return camera;
}

/// Decodes an IEEE half float. Subnormals flush to zero, and NaNs decode as zero so that a bad texel can't light a
/// probe.
float32 HalfToFloat(uint16 half)
{
	const uint32 exponent = (half >> 10) & 0x1F;
	const uint32 mantissa = half & 0x3FF;

	if (exponent == 0 || (exponent == 31 && mantissa != 0)) {
		return 0.0f;
	}

	uint32 bits = static_cast<uint32>(half & 0x8000) << 16;
	bits |= (exponent == 31) ? 0x7F800000 : (((exponent + 112) << 23) | (mantissa << 13));

	return std::bit_cast<float32>(bits);
}

///////////////////////////////////
// Placement
///////////////////////////////////

/// How far probes sit off a surface once they're pushed out of it
constexpr float32 scProbeSurfaceOffset = 0.25f;
constexpr float32 scProbeHugSurfaceOffset = 1.0f;

/// Probes this close to the outside of a box still count as inside it

constexpr float32 scProbeInsideSkin = 0.05f;

/// Probes in open space further than this from a surface are left where they are
constexpr float32 scProbeHugMaxDistance = 1.5f;
constexpr uint32 scMaxPushOutIterations = 8;

/// Furthest a probe can move from its grid point
constexpr float32 scMaxProbeRelocation = 0.45f;

constexpr float32 scProbeCrossingSkin = 0.01f;
constexpr float32 scProbeMinClearance = 2.0f * scCaptureNearPlane;
constexpr uint32 scRelocationSearchSteps = 5;

/// Objects bigger than this (the sky) are left out of placement so that they can't blow up the volume
constexpr float32 scMaxPlacementObjectSize = 100.0f;

constexpr float32 scDefaultProbeSpacing = 2.5f;
constexpr float32 scDefaultLevelProbeSpacing = 0.5f;
constexpr float32 scMaxProbeSpacing = 64.0f;
constexpr float32 scSurfaceSpacingStep = 1.25f;

constexpr float32 scBaseProbeSpacing = 2.0f;
constexpr uint32 scBaseProbeGridBudget = 512;

constexpr float32 scSurfaceCellMargin = 0.05f;

constexpr float32 scShaderNormalBiasScale = 0.2f;
constexpr float32 scShaderNormalBiasMin = 0.02f;
constexpr float32 scShaderNormalBiasMax = 0.5f;

void GetObjectWorldBounds(Object& object, Vec3f& out_min, Vec3f& out_max)
{
	// OBB::FromLocalBounds() does the same per-corner transform this used to do inline
	const AABB world_bounds = object.GetWorldOBB().GetWorldAABB();

	out_min = world_bounds.Min;
	out_max = world_bounds.Max;
}

uint32 SlicesForExtent(float32 extent, float32 spacing)
{
	const float32 slices = std::max(extent, 0.0f) / std::max(spacing, 1e-3f);
	return std::max(static_cast<uint32>(std::lround(slices)), Limits::MinProbeGridDim);
}

ProbePlacementBoxes GatherPlacementBoxes()
{
	ProbePlacementBoxes boxes;
	uint32 num_objects = 0;

	for (Object& object : gObjectManager->GetCache()) {
		// Only the level's lit geometry. Unlit objects (the sky) and anything not probe visible (the view model) don't
		// count.
		if (!object.pMesh.IsValid() || object.IsUnlit() || !object.IsProbeVisible()) {
			continue;
		}

		const AABB world_bounds = object.GetWorldOBB().GetWorldAABB();

		if ((world_bounds.Max - world_bounds.Min).Length() > scMaxPlacementObjectSize) {
			continue;
		}

		if (boxes.Count < ProbePlacementBoxes::scMaxBoxes) {
			const Mat4f& local_to_world = object.GetWorldMatrix();

			ProbePlacementBoxes::Box& box = boxes.Boxes[boxes.Count++];
			box.LocalBounds = object.Bounds;
			box.WorldBounds = world_bounds;
			box.LocalToWorld = local_to_world;
			box.WorldToLocal = local_to_world.Inverse();

			// Blockouts are brushes, whose bounds are the box around the whole hull. Use the hull itself when it isn't
			// a box
			const Brush* brush = (gWorld != nullptr && gWorld->pBlockout != nullptr)
									 ? gWorld->pBlockout->GetBrush(&object)
									 : nullptr;

			if (brush != nullptr && brush->IsValid() && !brush->IsBox()) {
				for (const BrushPlane& plane : brush->Planes) {
					box.Planes.push_back({ plane.Normal, plane.Distance });
				}
			}

			constexpr float32 cAxisEpsilon = 1e-5f;

			const Mat4f& m = local_to_world;
			box.bAxisAligned = box.Planes.empty() && std::abs(m.Rows[0].Y) < cAxisEpsilon &&
							   std::abs(m.Rows[0].Z) < cAxisEpsilon && std::abs(m.Rows[1].X) < cAxisEpsilon &&
							   std::abs(m.Rows[1].Z) < cAxisEpsilon && std::abs(m.Rows[2].X) < cAxisEpsilon &&
							   std::abs(m.Rows[2].Y) < cAxisEpsilon;
		}

		boxes.Min = Vec3f::Min(boxes.Min, world_bounds.Min);
		boxes.Max = Vec3f::Max(boxes.Max, world_bounds.Max);
		num_objects++;
	}

	if (num_objects > boxes.Count) {
		LogWarning("Probe placement only keeps probes out of the first {} of {} objects", boxes.Count, num_objects);
	}

	return boxes;
}

Vec3f ToBoxLocal(const Vec3f& point, const ProbePlacementBoxes::Box& box)
{
	const Vec4f local = box.WorldToLocal * Vec4f(point.X, point.Y, point.Z, 1.0f);
	return Vec3f(local.X, local.Y, local.Z);
}

bool IsInsideBox(const Vec3f& point, const ProbePlacementBoxes::Box& box, float32 skin)
{
	const Vec3f local = ToBoxLocal(point, box);

	if (!box.Planes.empty()) {
		for (const ProbePlacementBoxes::Plane& plane : box.Planes) {
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

bool IsInsideAnyBox(const Vec3f& point, const ProbePlacementBoxes& boxes)
{
	for (uint32 i = 0; i < boxes.Count; i++) {
		if (IsInsideBox(point, boxes.Boxes[i], scProbeInsideSkin)) {
			return true;
		}
	}

	return false;
}

bool IsInsideLevel(const Vec3f& point, const ProbePlacementBoxes& boxes)
{
	for (uint32 axis = 0; axis < 3; axis++) {
		if (point.mData[axis] < boxes.Min.mData[axis] || point.mData[axis] > boxes.Max.mData[axis]) {
			return false;
		}
	}

	return true;
}

/// The point of the box's solid nearest to `local`, in the box's local space
Vec3f ClosestPointInBox(const Vec3f& local, const ProbePlacementBoxes::Box& box)
{
	if (box.Planes.empty()) {
		return Vec3f::Clamp(local, box.LocalBounds.Min, box.LocalBounds.Max);
	}

	// Projecting onto the planes one after another gives a point inside the hull, but not necessarily the nearest one.
	// Dykstra's correction terms make it converge on the nearest, and points that are already inside stop at once.
	constexpr uint32 cMaxIterations = 32;
	constexpr float32 cConvergedSquared = 1e-8f;

	Vec3f corrections[Brush::scMaxPlanes] = {};

	const uint32 plane_count = std::min(static_cast<uint32>(box.Planes.size()), Brush::scMaxPlanes);

	Vec3f closest = local;

	for (uint32 iteration = 0; iteration < cMaxIterations; iteration++) {
		float32 moved_squared = 0.0f;

		for (uint32 i = 0; i < plane_count; i++) {
			const ProbePlacementBoxes::Plane& plane = box.Planes[i];

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

float32 DistanceToBox(const Vec3f& point, const ProbePlacementBoxes::Box& box)
{
	const Vec3f closest_local = ClosestPointInBox(ToBoxLocal(point, box), box);
	const Vec4f closest = box.LocalToWorld * Vec4f(closest_local.X, closest_local.Y, closest_local.Z, 1.0f);

	return (point - Vec3f(closest.X, closest.Y, closest.Z)).Length();
}

bool SegmentCrossesBox(const Vec3f& from, const Vec3f& to, const ProbePlacementBoxes::Box& box, float32 skin)
{
	const Vec3f local_from = ToBoxLocal(from, box);
	const Vec3f local_to = ToBoxLocal(to, box);

	float32 t_enter = 0.0f;
	float32 t_exit = 1.0f;

	if (!box.Planes.empty()) {
		// Clip the segment against each plane pulled in by the skin
		for (const ProbePlacementBoxes::Plane& plane : box.Planes) {
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

bool IsProbePlacementValid(const Vec3f& grid_position, const Vec3f& position, const ProbePlacementBoxes& boxes)
{
	if (boxes.IsEmpty()) {
		return true;
	}

	if (!IsInsideLevel(position, boxes)) {
		return false;
	}

	for (uint32 i = 0; i < boxes.Count; i++) {
		const ProbePlacementBoxes::Box& box = boxes.Boxes[i];

		if (DistanceToBox(position, box) < scProbeMinClearance) {
			return false;
		}

		// Leaving the box the grid point started in is what the push out is for
		if (!IsInsideBox(grid_position, box, scProbeInsideSkin) &&
			SegmentCrossesBox(grid_position, position, box, scProbeCrossingSkin)) {
			return false;
		}
	}

	return true;
}

/// Boxes have six faces, and a brush has one per plane
uint32 GetFaceCount(const ProbePlacementBoxes::Box& box)
{
	return box.Planes.empty() ? 6 : static_cast<uint32>(box.Planes.size());
}

/// How far `local` has to move to leave through a face. Box faces are numbered axis * 2 + (0 for min, 1 for max).
float32 GetFacePenetration(const Vec3f& local, const ProbePlacementBoxes::Box& box, uint32 face)
{
	if (!box.Planes.empty()) {
		const ProbePlacementBoxes::Plane& plane = box.Planes[face];
		return plane.Distance - plane.Normal.Dot(local);
	}

	const uint32 axis = face / 2;

	return ((face & 1) != 0) ? box.LocalBounds.Max.mData[axis] - local.mData[axis]
							 : local.mData[axis] - box.LocalBounds.Min.mData[axis];
}

Vec3f FaceExitPoint(const Vec3f& local, const ProbePlacementBoxes::Box& box, uint32 face, Vec3f& out_world_normal)
{
	Vec3f exit_local = local;
	Vec3f local_normal = Vec3f::sZero;

	if (!box.Planes.empty()) {
		const ProbePlacementBoxes::Plane& plane = box.Planes[face];

		exit_local = local + plane.Normal * (plane.Distance - plane.Normal.Dot(local));
		local_normal = plane.Normal;
	}
	else {
		const uint32 axis = face / 2;
		const bool through_max = (face & 1) != 0;

		exit_local.mData[axis] = through_max ? box.LocalBounds.Max.mData[axis] : box.LocalBounds.Min.mData[axis];
		local_normal.mData[axis] = through_max ? 1.0f : -1.0f;
	}

	const Vec4f exit_world = box.LocalToWorld * Vec4f(exit_local.X, exit_local.Y, exit_local.Z, 1.0f);

	const Vec4f world_normal4 = box.LocalToWorld * Vec4f(local_normal.X, local_normal.Y, local_normal.Z, 0.0f);
	out_world_normal = Vec3f(world_normal4.X, world_normal4.Y, world_normal4.Z).Normalize();

	return Vec3f(exit_world.X, exit_world.Y, exit_world.Z);
}

Vec3f ExitThroughFace(const Vec3f& local, const ProbePlacementBoxes::Box& box, uint32 face)
{
	Vec3f world_normal;
	const Vec3f exit = FaceExitPoint(local, box, face, world_normal);

	return exit + world_normal * scProbeSurfaceOffset;
}

float32 DistanceToBoxSurface(const Vec3f& point, const ProbePlacementBoxes::Box& box)
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

bool CellTouchesSurface(const Vec3f& cell_min, const Vec3f& cell_max, float32 pad, const ProbePlacementBoxes& boxes)
{
	const Vec3f padded_min = cell_min - Vec3f(pad);
	const Vec3f padded_max = cell_max + Vec3f(pad);

	const Vec3f center = (cell_min + cell_max) * 0.5f;
	const float32 radius = ((cell_max - cell_min) * 0.5f).Length() + pad;

	for (uint32 i = 0; i < boxes.Count; i++) {
		const ProbePlacementBoxes::Box& box = boxes.Boxes[i];
		const AABB& bounds = box.WorldBounds;

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

		if (DistanceToBoxSurface(center, box) <= radius) {
			return true;
		}
	}

	return false;
}

float32 ShaderNormalBias(const Vec3f& cell_size)
{
	const float32 min_spacing = std::min({ cell_size.X, cell_size.Y, cell_size.Z });
	return std::clamp(scShaderNormalBiasScale * min_spacing, scShaderNormalBiasMin, scShaderNormalBiasMax);
}

bool IsBuriedTooDeep(const Vec3f& point, const Vec3f& max_relocation, const ProbePlacementBoxes& boxes)
{
	for (uint32 i = 0; i < boxes.Count; i++) {
		const ProbePlacementBoxes::Box& box = boxes.Boxes[i];

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

uint32 FindNeededGridPoints(const Vec3f& volume_min, const Vec3f& cell_size, const ProbeGridSize& grid,
							const ProbePlacementBoxes& boxes, eProbeFill fill, std::vector<uint8>& out_needed)
{
	const uint32 dims[3] = { grid.X, grid.Y, grid.Z };
	const uint32 num_points = grid.GetProbeCount();

	out_needed.assign(num_points, (fill == eProbeFill::Dense || boxes.Count == 0) ? 1 : 0);

	if (boxes.Count == 0) {
		return num_points;
	}

	const float32 pad = ShaderNormalBias(cell_size) + scSurfaceCellMargin;

	for (uint32 iz = 0; fill == eProbeFill::Surface && iz + 1 < dims[2]; iz++) {
		for (uint32 iy = 0; iy + 1 < dims[1]; iy++) {
			for (uint32 ix = 0; ix + 1 < dims[0]; ix++) {
				const Vec3f cell_min = volume_min + cell_size * Vec3f(static_cast<float32>(ix),
																	  static_cast<float32>(iy),
																	  static_cast<float32>(iz));

				if (!CellTouchesSurface(cell_min, cell_min + cell_size, pad, boxes)) {
					continue;
				}

				for (uint32 corner = 0; corner < 8; corner++) {
					const uint32 cx = ix + (corner & 1);
					const uint32 cy = iy + ((corner >> 1) & 1);
					const uint32 cz = iz + ((corner >> 2) & 1);

					out_needed[cx + dims[0] * (cy + dims[1] * cz)] = 1;
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
				if (out_needed[point] == 0) {
					continue;
				}

				const Vec3f position = volume_min + cell_size * Vec3f(static_cast<float32>(ix),
																	  static_cast<float32>(iy),
																	  static_cast<float32>(iz));

				if (IsBuriedTooDeep(position, max_relocation, boxes)) {
					out_needed[point] = 0;
					continue;
				}

				count++;
			}
		}
	}

	return count;
}

bool PushOutOfBox(Vec3f& point, const ProbePlacementBoxes::Box& box)
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

	for (uint32 i = 0; i < boxes.Count; i++) {
		const ProbePlacementBoxes::Box& box = boxes.Boxes[i];

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

	// Every face leads into more geometry, so step out one box at a time and leave the result to the placement check
	for (uint32 iteration = 0; iteration < scMaxPushOutIterations; iteration++) {
		bool pushed = false;

		for (uint32 i = 0; i < boxes.Count && !pushed; i++) {
			pushed = PushOutOfBox(point, boxes.Boxes[i]);
		}

		if (!pushed) {
			break;
		}
	}

	return true;
}

/// Distance from `point` to the nearest box other than `skip_box`
float32 DistanceToOtherBoxes(const Vec3f& point, const ProbePlacementBoxes& boxes, uint32 skip_box)
{
	float32 nearest = std::numeric_limits<float32>::max();

	for (uint32 i = 0; i < boxes.Count; i++) {
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

	for (uint32 i = 0; i < boxes.Count; i++) {
		const ProbePlacementBoxes::Box& box = boxes.Boxes[i];

		const Vec3f closest_local = ClosestPointInBox(ToBoxLocal(point, box), box);

		const Vec4f closest_world4 = box.LocalToWorld * Vec4f(closest_local.X, closest_local.Y, closest_local.Z, 1.0f);
		const Vec3f closest(closest_world4.X, closest_world4.Y, closest_world4.Z);

		const float32 distance = (point - closest).Length();

		// Points on or inside a box are left to the push out
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

bool FindValidProbePosition(const Vec3f& grid_position, const Vec3f& preferred, const Vec3f& max_relocation,
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

void FitCellCentred(const Vec3f& region_min, const Vec3f& region_max, const ProbeGridSize& grid, Vec3f& out_min,
					Vec3f& out_size)
{
	const Vec3f region_size = region_max - region_min;
	const Vec3f slices(static_cast<float32>(grid.X), static_cast<float32>(grid.Y), static_cast<float32>(grid.Z));
	const Vec3f inset = region_size / (slices * 2.0f);

	out_min = region_min + inset;
	out_size = region_size - inset * 2.0f;
}

ProbeGridSize GridForSpacing(const Vec3f& region_size, float32 spacing, bool cell_centred)
{
	const uint32 extra = cell_centred ? 0 : 1;

	return ProbeGridSize {
		SlicesForExtent(region_size.X, spacing) + extra,
		SlicesForExtent(region_size.Y, spacing) + extra,
		SlicesForExtent(region_size.Z, spacing) + extra,
	};
}

void LayoutGrid(const Vec3f& region_min, const Vec3f& region_max, const ProbeGridSize& grid, bool cell_centred,
				Vec3f& out_min, Vec3f& out_size)
{
	if (cell_centred) {
		FitCellCentred(region_min, region_max, grid, out_min, out_size);
		return;
	}

	out_min = region_min;
	out_size = region_max - region_min;
}

Vec3f GridCellSize(const Vec3f& volume_size, const ProbeGridSize& grid)
{
	const Vec3f num_cells(static_cast<float32>(grid.X - 1), static_cast<float32>(grid.Y - 1),
						  static_cast<float32>(grid.Z - 1));

	return volume_size / num_cells;
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

void SetProbePosition(ProbeInfo& info, const Vec3f& position)
{
	info.ProbePosition[0] = position.X;
	info.ProbePosition[1] = position.Y;
	info.ProbePosition[2] = position.Z;
	info.ProbePosition[3] = 1.0f;
}

void ResetDepthMoments(ProbeInfo& info)
{
	for (uint32 texel = 0; texel < Limits::ProbeDepthFaces * Limits::ProbeDepthTexelsPerFace; texel++) {
		info.DepthMoments[texel * 2 + 0] = Limits::ProbeDepthMaxDistance;
		info.DepthMoments[texel * 2 + 1] = Limits::ProbeDepthMaxDistance * Limits::ProbeDepthMaxDistance;
	}
}

///////////////////////////////////
// GPU and file IO
///////////////////////////////////

void UploadRange(renderer::RawGpuBuffer& buffer, const void* data, uint64 offset, uint64 size)
{
	uint8* mapped = static_cast<uint8*>(buffer.GetMapped());
	if (mapped == nullptr) {
		return;
	}

	memcpy(mapped + offset, static_cast<const uint8*>(data) + offset, size);
	buffer.FlushToGpu(static_cast<uint32>(offset), static_cast<uint32>(size));
}


struct ProbeFileLayout
{
	char Magic[4] = { 'R', 'P', 'P', 'V' };
	uint32 Version = FX_PROBE_CACHE_FILE_VERSION;
	uint32 SHCoeffCount = Limits::ProbeSHCoeffCount;
	uint32 DepthFloatCount = Limits::ProbeDepthFloatCount;
	uint32 MaxVolumes = Limits::MaxProbeVolumes;
};

struct ProbeFileHeader
{
	ProbeFileLayout Layout;

	uint32 VolumeCount = 0;
	uint32 ProbeCount = 0;
	uint32 GridPointCount = 0;
};


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

static constexpr uint32 scOldestReadableCacheVersion = FX_PROBE_CACHE_FILE_VERSION;

uint64 GetProbeFileSize(uint32 probe_count, uint32 grid_point_count)
{
	return sizeof(ProbeFileHeader) + Limits::MaxProbeVolumes * (sizeof(ProbeVolumeData) + sizeof(ProbeVolumeRange)) +
		   static_cast<uint64>(grid_point_count) * sizeof(uint16) +
		   static_cast<uint64>(probe_count) * (sizeof(ProbeSHData) + sizeof(ProbeInfo));
}

bool IsCacheLayoutReadable(const ProbeFileLayout& layout)
{
	const ProbeFileLayout expected {};

	return memcmp(layout.Magic, expected.Magic, sizeof(expected.Magic)) == 0 &&
		   layout.SHCoeffCount == expected.SHCoeffCount && layout.DepthFloatCount == expected.DepthFloatCount &&
		   layout.MaxVolumes == expected.MaxVolumes && layout.Version >= scOldestReadableCacheVersion &&
		   layout.Version <= FX_PROBE_CACHE_FILE_VERSION;
}

String GetProbeFilePath()
{
	if (gWorld != nullptr && gWorld->BlockoutPath.GetLength() > 0) {
		std::filesystem::path path(gWorld->BlockoutPath.CStr());
		path.replace_extension(".fxprobe");

		return String(path.string().c_str());
	}

	return String::Fmt("{}/probes.fxprobe", gAssetManager->GetScenePath().CStr());
}

/// Reads exactly `size` bytes from the current position in `file` into `out`
bool ReadExact(File& file, void* out, uint64 size)
{
	return file.Read(MakeSlice(static_cast<uint8*>(out), size)).Size == size;
}

} // namespace

void ProbeManager::Create()
{
	constexpr float32 cSky[3] = { 0.055f, 0.062f, 0.075f };
	constexpr float32 cGround[3] = { 0.025f, 0.022f, 0.020f };

	std::fill(std::begin(mProbes), std::end(mProbes), MakeSkyGradientProbe(cSky, cGround));


	for (ProbeInfo& info : mProbeInfos) {
		ResetDepthMoments(info);
	}

	ClearVolumes();

	AddVolumeAndPlaceProbes(Vec3f(-20.0f, -2.0f, -20.0f), Vec3f(150.0f, 15.0f, 150.0f), ProbeGridSize { 4, 2, 4 },
							ProbePlacementBoxes {}, eProbeFill::Dense);

	UploadReflectionProbes(0);
}

void ProbeManager::Destroy()
{
	mBakeState = eBakeState::Idle;
	mbCapturingFaces = false;
	mCurrentProbe = 0;

	if (!mbCaptureResourcesCreated) {
		return;
	}

	for (uint32 slot = 0; slot < scProbesPerFrame; slot++) {
		for (uint32 face = 0; face < scCaptureFaces; face++) {
			mColorStaging[slot][face].Destroy();
			mDepthStaging[slot][face].Destroy();
		}
	}

	for (renderer::RawGpuBuffer& staging : mReflectionStaging) {
		staging.Destroy();
	}

	rx_probe_capture_free(mpCapture);
	mpCapture = nullptr;

	mbCaptureResourcesCreated = false;
}

Vec3f ProbeManager::GetProbePosition(uint32 index) const
{
	const float32* position = mProbeInfos[index].ProbePosition;
	return Vec3f(position[0], position[1], position[2]);
}

///////////////////////////////////
// Placement
///////////////////////////////////

void ProbeManager::ClearVolumes()
{
	if (IsBaking()) {
		LogWarning("Cannot change the probe volumes while they are baking");
		return;
	}

	mVolumeCount = 0;
	mProbeCount = 0;
	mGridPointCount = 0;
	mbGridDirty = true;

	RefreshVolumeCounts();
	UploadToGpu(0, 0);
}

void ProbeManager::GetVolumeProbeRange(uint32 volume, uint32& out_first_probe, uint32& out_count) const
{
	Assert(volume < mVolumeCount);

	out_first_probe = mVolumeRanges[volume].FirstProbe;
	out_count = mVolumeRanges[volume].ProbeCount;
}

uint32 ProbeManager::GetVolumeOfProbe(uint32 probe_index) const
{
	for (uint32 volume = 0; volume < mVolumeCount; volume++) {
		const ProbeVolumeRange& range = mVolumeRanges[volume];

		if (probe_index >= range.FirstProbe && probe_index < range.FirstProbe + range.ProbeCount) {
			return volume;
		}
	}

	return Limits::MaxProbeVolumes;
}

bool ProbeManager::AddVolume(const Vec3f& center, const Vec3f& size, const ProbeGridSize& grid)
{
	if (IsBaking()) {
		LogWarning("Cannot add a probe volume while the probes are baking");
		return false;
	}

	return AddVolumeAndPlaceProbes(center - size * 0.5f, size, grid, GatherPlacementBoxes(), eProbeFill::Dense);
}

bool ProbeManager::AddLevelVolumes()
{
	if (IsBaking()) {
		LogWarning("Cannot add a probe volume while the probes are baking");
		return false;
	}

	const ProbePlacementBoxes boxes = GatherPlacementBoxes();

	if (boxes.IsEmpty()) {
		LogError("Cannot fit a probe volume to the level: there is no geometry to fit it to");
		return false;
	}

	const bool added_base = AddLevelBaseVolume(boxes);
	const bool added_surface = AddLevelSurfaceVolume(boxes);

	return added_base && added_surface;
}

bool ProbeManager::AddLevelBaseVolume(const ProbePlacementBoxes& boxes)
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

	return AddVolumeAndPlaceProbes(volume_min, volume_size, grid, boxes, eProbeFill::Dense);
}

bool ProbeManager::AddLevelSurfaceVolume(const ProbePlacementBoxes& boxes)
{
	const float32 spacing = gCVars->Get("r_probe_level_spacing", scDefaultLevelProbeSpacing);
	return AddSurfaceVolumeForBudget(boxes.Min, boxes.Max, spacing, boxes, true);
}

bool ProbeManager::AddSurfaceVolumeForBudget(const Vec3f& region_min, const Vec3f& region_max, float32 spacing,
											 const ProbePlacementBoxes& boxes, bool cell_centred)
{
	if (mVolumeCount >= Limits::MaxProbeVolumes) {
		LogError("Probe volume rejected: all {} volume slots are taken", Limits::MaxProbeVolumes);
		return false;
	}

	const uint32 probe_budget = Limits::MaxIrradianceProbes - mProbeCount;
	const uint32 point_budget = Limits::MaxProbeGridPoints - mGridPointCount;

	std::vector<uint8> needed;

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

		if (!AddVolumeAndPlaceProbes(volume_min, volume_size, grid, boxes, eProbeFill::Surface)) {
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

uint32 ProbeManager::RebuildVolumesFromWorld()
{
	if (IsBaking()) {
		LogWarning("Cannot rebuild the probe volumes while they are baking");
		return 0;
	}

	ClearVolumes();

	const ProbePlacementBoxes boxes = GatherPlacementBoxes();

	if (boxes.IsEmpty()) {
		LogError("Cannot fit a probe volume to the level: there is no geometry to fit it to");
		return 0;
	}

	AddLevelBaseVolume(boxes);

	const float32 spacing = gCVars->Get("r_probe_spacing", scDefaultProbeSpacing);

	uint32 num_brush_volumes = 0;

	for (Object& object : gObjectManager->GetCache()) {
		if (!object.IsProbeVolume() || object.IsReflectionProbe()) {
			continue;
		}

		Vec3f min;
		Vec3f max;
		GetObjectWorldBounds(object, min, max);

		if (AddSurfaceVolumeForBudget(min, max, spacing, boxes, false)) {
			num_brush_volumes++;
		}
		else {
			LogWarning("Probe volume brush '{}' was skipped", object.Name.Get());
		}
	}

	AddLevelSurfaceVolume(boxes);

	RebuildReflectionProbesFromWorld();

	LogInfo("Probe volumes rebuilt: {} from editor brushes, {} in total, {} of {} probes and {} of {} grid points used",
			num_brush_volumes, mVolumeCount, mProbeCount, Limits::MaxIrradianceProbes, mGridPointCount,
			Limits::MaxProbeGridPoints);

	return num_brush_volumes;
}

void ProbeManager::BeginBake()
{
	if (IsBaking()) {
		LogWarning("A probe bake is already in progress");
		return;
	}

	if (mVolumeCount == 0 || mProbeCount == 0) {
		LogError("Probe bake failed: no probes have been placed");
		return;
	}

	mCurrentProbe = 0;
	mCurrentBounce = 0;
	mBakePhase = eBakePhase::Irradiance;
	mBounceCount = static_cast<uint32>(
		std::clamp<int64>(gCVars->Get("r_probe_bounces", scDefaultProbeBounces), 1, scMaxProbeBounces));
	mBakeState = eBakeState::CapturePending;

	LogInfo("Probe bake started ({} probes across {} volume(s), {} per frame, {} bounce(s))", mProbeCount, mVolumeCount,
			scProbesPerFrame, mBounceCount);
}

void ProbeManager::BeginGridBake()
{
	if (IsBaking()) {
		LogWarning("A probe bake is already in progress");
		return;
	}

	ClearVolumes();

	if (AddLevelVolumes()) {
		BeginBake();
	}
}

void ProbeManager::BeginGridBakeAt(const Vec3f& center, const Vec3f& size, const ProbeGridSize& grid)
{
	if (IsBaking()) {
		LogWarning("A probe bake is already in progress");
		return;
	}

	if (!CheckGridSize(grid)) {
		return;
	}

	ClearVolumes();

	if (AddVolume(center, size, grid)) {
		BeginBake();
	}
}

void ProbeManager::RefreshVolumeCounts()
{
	// The shader reads the list's length out of whichever descriptor it has, so every entry carries it
	for (ProbeVolumeData& volume : mVolumes) {
		volume.MinAndCount[3] = static_cast<float32>(mVolumeCount);
	}
}

bool ProbeManager::AddVolumeAndPlaceProbes(const Vec3f& volume_min, const Vec3f& volume_size, const ProbeGridSize& grid,
										   const ProbePlacementBoxes& boxes, eProbeFill fill)
{
	if (!CheckGridSize(grid)) {
		return false;
	}

	if (mVolumeCount >= Limits::MaxProbeVolumes) {
		LogError("Probe volume rejected: all {} volume slots are taken", Limits::MaxProbeVolumes);
		return false;
	}

	const uint32 num_points = grid.GetProbeCount();

	if (num_points > Limits::MaxProbeGridPoints - mGridPointCount) {
		LogError("Probe volume rejected: its grid has {} points and only {} of the {} grid point budget are left",
				 num_points, Limits::MaxProbeGridPoints - mGridPointCount, Limits::MaxProbeGridPoints);
		return false;
	}

	const uint32 first_probe = mProbeCount;

	if (!PlaceVolumeProbes(mVolumeCount, volume_min, volume_size, grid, boxes, fill)) {
		return false;
	}

	mVolumeCount++;
	mbGridDirty = true;

	RefreshVolumeCounts();
	UploadToGpu(first_probe, mProbeCount - first_probe);

	return true;
}

bool ProbeManager::PlaceVolumeProbes(uint32 volume_index, const Vec3f& volume_min, const Vec3f& volume_size,
									 const ProbeGridSize& grid, const ProbePlacementBoxes& boxes, eProbeFill fill)
{
	const uint32 dims[3] = { grid.X, grid.Y, grid.Z };

	// Probes sit on the volume's boundary, so there are (dim - 1) cells along each axis
	const Vec3f num_cells(static_cast<float32>(dims[0] - 1), static_cast<float32>(dims[1] - 1),
						  static_cast<float32>(dims[2] - 1));

	const Vec3f cell_size = volume_size / num_cells;

	const float32 hug_distance = std::min(
		{ scProbeHugMaxDistance, 0.5f * cell_size.X, 0.5f * cell_size.Y, 0.5f * cell_size.Z });

	const Vec3f max_relocation = cell_size * scMaxProbeRelocation;

	std::vector<uint8> needed;
	const uint32 num_needed = FindNeededGridPoints(volume_min, cell_size, grid, boxes, fill, needed);

	const uint32 first_probe = mProbeCount;
	const uint32 first_point = mGridPointCount;

	uint32 num_pushed = 0;
	uint32 num_hugged = 0;
	uint32 num_relocated = 0;
	uint32 num_unplaced = 0;
	uint32 probe_index = first_probe;
	uint32 point_index = first_point;

	for (uint32 iz = 0; iz < dims[2]; iz++) {
		for (uint32 iy = 0; iy < dims[1]; iy++) {
			for (uint32 ix = 0; ix < dims[0]; ix++, point_index++) {
				mGridProbes[point_index] = Limits::ProbeGridEmpty;

				if (needed[point_index - first_point] == 0) {
					continue;
				}

				const Vec3f grid_position = volume_min + cell_size * Vec3f(static_cast<float32>(ix),
																		   static_cast<float32>(iy),
																		   static_cast<float32>(iz));
				Vec3f position = grid_position;

				if (PushOutOfBoxes(position, boxes)) {
					num_pushed++;
				}

				if (HugNearestSurface(position, boxes, hug_distance)) {
					// The surface of one box can be inside another
					PushOutOfBoxes(position, boxes);
					num_hugged++;
				}

				position = Vec3f::Clamp(position, grid_position - max_relocation, grid_position + max_relocation);

				if (!IsProbePlacementValid(grid_position, position, boxes)) {
					Vec3f fallback;

					if (!FindValidProbePosition(grid_position, position, max_relocation, boxes, fallback)) {
						num_unplaced++;
						continue;
					}

					position = fallback;
					num_relocated++;
				}

				if (probe_index >= Limits::MaxIrradianceProbes) {
					LogWarning("Probe volume rejected: it needs more than the {} probes left of the {} probe budget",
							   Limits::MaxIrradianceProbes - first_probe, Limits::MaxIrradianceProbes);
					return false;
				}

				// The probes have moved, so whatever depth moments they hold were captured somewhere else. Reset
				// them until the bake fills them in, or unbaked probes would report occluders that aren't there.
				ResetDepthMoments(mProbeInfos[probe_index]);
				SetProbePosition(mProbeInfos[probe_index], position);

				mGridProbes[point_index] = static_cast<uint16>(probe_index++);
			}
		}
	}

	ProbeVolumeData& volume = mVolumes[volume_index];

	for (uint32 axis = 0; axis < 3; axis++) {
		volume.MinAndCount[axis] = volume_min.mData[axis];
		volume.InvCellSize[axis] = (volume_size.mData[axis] > 1e-4f) ? (num_cells.mData[axis] / volume_size.mData[axis])
																	 : 1.0f;
		volume.DimsAndFirst[axis] = dims[axis];
	}

	volume.InvCellSize[3] = 0.0f;
	volume.DimsAndFirst[3] = first_point;

	FinaliseVolumeBounds(volume);

	mVolumeRanges[volume_index] = ProbeVolumeRange { first_probe, probe_index - first_probe };

	mProbeCount = probe_index;
	mGridPointCount = point_index;

	LogInfo("Probe volume {} ({}): min={} size={} grid={}x{}x{}, {} of {} grid points needed, {} probes from index {} "
			"({} pushed out, {} hugging, {} relocated, {} unplaced)",
			volume_index, (fill == eProbeFill::Dense) ? "dense" : "surface", volume_min, volume_size, dims[0], dims[1],
			dims[2], num_needed, grid.GetProbeCount(), probe_index - first_probe, first_probe, num_pushed, num_hugged,
			num_relocated, num_unplaced);

	return true;
}

///////////////////////////////////
// Capture
///////////////////////////////////

void ProbeManager::CreateCaptureResources()
{
	if (mbCaptureResourcesCreated) {
		return;
	}

	mCaptureStage.Create("ProbeCapture", Vec2u(scCaptureSize, scCaptureSize), eSizeDivisor::FullRes);

	mCaptureStage.AddTarget(eImageFormat::RGBA16_Float,
							VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
								VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
							eImageAspectFlag::Color);

	mCaptureStage.AddTarget(eImageFormat::D32_Float,
							VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
								VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
							eImageAspectFlag::Depth);

	mCaptureStage.BuildRenderStage();

	constexpr uint64 cPixels = static_cast<uint64>(scCaptureSize) * scCaptureSize;

	for (uint32 slot = 0; slot < scProbesPerFrame; slot++) {
		for (uint32 face = 0; face < scCaptureFaces; face++) {
			mColorStaging[slot][face].Create(renderer::eGpuBufferType::Transfer, cPixels * sizeof(uint16) * 4,
											 RX_MEMORY_GPU_TO_CPU, eGpuBufferFlags::TransferReceiver);
			mDepthStaging[slot][face].Create(renderer::eGpuBufferType::Transfer, cPixels * sizeof(float32),
											 RX_MEMORY_GPU_TO_CPU, eGpuBufferFlags::TransferReceiver);
		}
	}

	constexpr uint32 cReflectionSize = Limits::ReflectionProbeSize;

	mReflectionStage.Create("ReflectionCapture", Vec2u(cReflectionSize, cReflectionSize), eSizeDivisor::FullRes);

	mReflectionStage.AddTarget(eImageFormat::RGBA16_Float,
							   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
								   VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
							   eImageAspectFlag::Color);

	mReflectionStage.AddTarget(eImageFormat::D32_Float,
							   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
							   eImageAspectFlag::Depth);

	mReflectionStage.BuildRenderStage();

	for (renderer::RawGpuBuffer& staging : mReflectionStaging) {
		staging.Create(renderer::eGpuBufferType::Transfer,
					   static_cast<uint64>(cReflectionSize) * cReflectionSize * sizeof(uint16) * 4,
					   RX_MEMORY_GPU_TO_CPU, eGpuBufferFlags::TransferReceiver);
	}

	BuildCaptureTexels();

	mbCaptureResourcesCreated = true;
}

void ProbeManager::BuildCaptureTexels()
{
	float32 face_inv_view_projection[scCaptureFaces][16];

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		const PerspectiveCamera camera = MakeCaptureCamera(Vec3f::sZero, face);
		const Mat4f inv_view_projection = camera.InvProjectionMatrix * camera.InvViewMatrix;

		mCaptureInvProjection = camera.InvProjectionMatrix;
		mCaptureFaceToClip[face] = inv_view_projection.Inverse();

		std::memcpy(face_inv_view_projection[face], inv_view_projection.RawData, sizeof(float32) * 16);
	}

	rx_probe_capture_free(mpCapture);
	mpCapture = rx_probe_capture_new(scCaptureSize, mCaptureInvProjection.RawData, &face_inv_view_projection[0][0]);

	Assert(mpCapture != nullptr);
}

void ProbeManager::RecordCaptureBatch(renderer::CommandBuffer& cmd, const RenderFaceFunc& render_face)
{
	if (mBakeState != eBakeState::CapturePending) {
		return;
	}

	CreateCaptureResources();

	if (mBakePhase == eBakePhase::Reflection) {
		RecordReflectionCapture(cmd, render_face);
		return;
	}

	mBatchStart = mCurrentProbe;
	const uint32 batch_end = std::min(mCurrentProbe + scProbesPerFrame, mProbeCount);

	mbCapturingFaces = true;

	for (; mCurrentProbe < batch_end; mCurrentProbe++) {
		const uint32 slot = mCurrentProbe - mBatchStart;

		for (uint32 face = 0; face < scCaptureFaces; face++) {
			PerspectiveCamera camera = MakeCaptureCamera(GetProbePosition(mCurrentProbe), face);

			render_face(camera, mCaptureStage);

			CopyTargetToStaging(cmd, mCaptureStage, scCaptureSize, eImageFormat::RGBA16_Float,
								mColorStaging[slot][face]);
			CopyTargetToStaging(cmd, mCaptureStage, scCaptureSize, eImageFormat::D32_Float, mDepthStaging[slot][face]);
		}
	}

	mbCapturingFaces = false;
	mBakeState = eBakeState::CaptureRecorded;
}

void ProbeManager::CopyTargetToStaging(renderer::CommandBuffer& cmd, renderer::RenderStage& stage, uint32 size,
									   eImageFormat format, renderer::RawGpuBuffer& staging)
{
	renderer::Target* target = stage.GetTarget(format);
	Assert(target != nullptr);

	Image& image = target->Image;

	const VkImageAspectFlags aspect = (format == eImageFormat::D32_Float) ? VK_IMAGE_ASPECT_DEPTH_BIT
																		  : VK_IMAGE_ASPECT_COLOR_BIT;

	renderer::BarrierHelper::ImageLayoutTransition(&image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cmd, 0, 1);

	const VkBufferImageCopy copy {
		.imageSubresource { .aspectMask = aspect, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
		.imageExtent { .width = size, .height = size, .depth = 1 },
	};

	vkCmdCopyImageToBuffer(cmd, image.Get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.Get(), 1, &copy);

	renderer::BarrierHelper::ImageLayoutTransition(&image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 0, 1);
}

void ProbeManager::ServiceCaptureBake()
{
	if (mBakeState != eBakeState::CaptureRecorded) {
		return;
	}

	// The batch was recorded into this frame's command buffer, so wait for it to land in the staging buffers
	renderer::gGraphics->GetDevice()->WaitForIdle();

	if (mBakePhase == eBakePhase::Reflection) {
		ServiceReflectionBake();
		return;
	}

	for (uint32 probe = mBatchStart; probe < mCurrentProbe; probe++) {
		if (!ReadBackProbe(probe - mBatchStart, probe)) {
			LogError("Probe grid bake failed: could not read back probe {}", probe);
			mBakeState = eBakeState::Idle;
			return;
		}
	}

	UploadToGpu(mBatchStart, mCurrentProbe - mBatchStart);

	if (mCurrentProbe < mProbeCount) {
		mBakeState = eBakeState::CapturePending;
		LogInfo("Probe bake progress: {}/{}", mCurrentProbe, mProbeCount);
		return;
	}

	if (mCurrentBounce + 1 < mBounceCount) {
		mCurrentBounce++;
		mCurrentProbe = 0;
		mBakeState = eBakeState::CapturePending;
		LogInfo("Probe bounce {}/{} done, starting the next", mCurrentBounce, mBounceCount);
		return;
	}

	mBakeState = eBakeState::Idle;
	LogInfo("Probe bake complete ({} probes across {} volume(s), {} bounce(s))", mProbeCount, mVolumeCount,
			mBounceCount);

	if (mReflectionProbeCount > 0) {
		StartReflectionPhase();
	}
}

bool ProbeManager::ReadBackProbe(uint32 batch_slot, uint32 probe_index)
{
	const uint16* colors[scCaptureFaces];
	const float32* depths[scCaptureFaces];

	bool mapped = true;

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		for (renderer::RawGpuBuffer* staging : { &mColorStaging[batch_slot][face], &mDepthStaging[batch_slot][face] }) {
			staging->Map();
			staging->InvalidateFromGpu();
		}

		colors[face] = static_cast<const uint16*>(mColorStaging[batch_slot][face].GetMapped());
		depths[face] = static_cast<const float32*>(mDepthStaging[batch_slot][face].GetMapped());

		mapped &= (colors[face] != nullptr && depths[face] != nullptr);
	}

	if (mapped) {
		ProjectCapture(colors, depths, mProbes[probe_index], mProbeInfos[probe_index]);
	}

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		mColorStaging[batch_slot][face].UnMap();
		mDepthStaging[batch_slot][face].UnMap();
	}

	return mapped;
}

void ProbeManager::ProjectCapture(const uint16* const colors[scCaptureFaces],
								  const float32* const depths[scCaptureFaces], ProbeSHData& out_sh,
								  ProbeInfo& out_info) const
{
	const int32 projected =
		rx_probe_capture_project(mpCapture, colors, depths, &out_sh.SH[0][0], out_info.DepthMoments);

	Assert(projected != 0);
}

void ProbeManager::UploadToGpu(uint32 first_probe, uint32 count)
{
	renderer::GraphicsBackend* graphics = renderer::gGraphics;

	// The probe buffers are shared by every frame in flight, so wait until none of them are reading
	graphics->GetDevice()->WaitForIdle();

	UploadRange(graphics->ProbeVolumeBuffer, mVolumes, 0, sizeof(mVolumes));

	if (mbGridDirty && mGridPointCount > 0) {
		const uint64 grid_bytes = static_cast<uint64>((mGridPointCount + 1) / 2) * sizeof(uint32);
		UploadRange(graphics->ProbeGridBuffer, mGridProbes, 0, grid_bytes);
	}

	mbGridDirty = false;

	if (count == 0) {
		return;
	}


	for (uint32 probe = first_probe; probe < first_probe + count; probe++) {
		// Position in the first three w lanes
		for (uint32 lane = 0; lane < 3; lane++) {
			mProbes[probe].SH[lane][3] = mProbeInfos[probe].ProbePosition[lane];
		}
	}

	UploadRange(graphics->ProbeBuffer, mProbes, first_probe * sizeof(ProbeSHData), count * sizeof(ProbeSHData));
	UploadMomentsAtlas(first_probe, count);
}

/// Encodes `value` (already in 0..1) the way VK_FORMAT_R16G16_UNORM decodes it.
static uint16 EncodeUNorm16(float32 value)
{
	return static_cast<uint16>(std::clamp(value, 0.0f, 1.0f) * 65535.0f + 0.5f);
}

void ProbeManager::UploadMomentsAtlas(uint32 first_probe, uint32 count)
{
	using namespace renderer;

	if (count == 0) {
		return;
	}

	GraphicsBackend* graphics = gGraphics;
	Assert(graphics->pProbeMomentsAtlas != nullptr);

	const uint32 first_row = first_probe / Limits::ProbeAtlasColumns;
	const uint32 last_row = (first_probe + count - 1) / Limits::ProbeAtlasColumns;

	constexpr uint32 cRowTexels = Limits::ProbeAtlasWidth * Limits::ProbeDepthSize;

	SizedArray<uint16> row;
	row.InitSize(static_cast<uint64>(cRowTexels) * 2);

	for (uint32 atlas_row = first_row; atlas_row <= last_row; atlas_row++) {
		// Probes past mProbeCount keep the max distance sentinel, which reads as unoccluded
		memset(row.pData, 0xFF, row.Size * sizeof(uint16));

		for (uint32 column = 0; column < Limits::ProbeAtlasColumns; column++) {
			const uint32 probe = (atlas_row * Limits::ProbeAtlasColumns) + column;

			if (probe >= mProbeCount) {
				break;
			}

			const float32* moments = mProbeInfos[probe].DepthMoments;
			const uint32 strip_x = column * Limits::ProbeAtlasProbeWidth;

			for (uint32 face = 0; face < Limits::ProbeDepthFaces; face++) {
				for (uint32 y = 0; y < Limits::ProbeDepthSize; y++) {
					for (uint32 x = 0; x < Limits::ProbeDepthSize; x++) {
						const uint32 src = ((face * Limits::ProbeDepthTexelsPerFace) + (y * Limits::ProbeDepthSize) +
											x) *
										   2;

						const uint32 dst = ((y * Limits::ProbeAtlasWidth) + strip_x + (face * Limits::ProbeDepthSize) +
											x) *
										   2;

						const float32 mean = moments[src + 0];

						// Stored as a standard deviation rather than the raw second moment: at 16 bits the shader's
						// mean_sq - mean * mean would lose the whole variance to cancellation at any distance.
						const float32 stddev = std::sqrt(std::max(moments[src + 1] - (mean * mean), 0.0f));

						row[dst + 0] = EncodeUNorm16(mean / Limits::ProbeDepthMaxDistance);
						row[dst + 1] = EncodeUNorm16(stddev / Limits::ProbeDepthMaxDistance);
					}
				}
			}
		}

		RawGpuBuffer staging;
		staging.Create(eGpuBufferType::Transfer, row.Size * sizeof(uint16), RX_MEMORY_CPU_TO_GPU,
					   eGpuBufferFlags::TransferReceiver);
		staging.Upload(row.pData, row.Size * sizeof(uint16));

		const Vec2u row_size(Limits::ProbeAtlasWidth, Limits::ProbeDepthSize);
		const Vec2u row_offset(0, atlas_row * Limits::ProbeDepthSize);

		graphics->SubmitImmediateUploadCmd(
			[&](CommandBuffer& cmd)
			{
				graphics->pProbeMomentsAtlas->CopyFromBuffer(cmd, staging, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
															 row_size, 0, 0, row_offset);
			});

		staging.Destroy();
	}
}

/////////////////////////////////////
// Probe cache (.fxprobe files)
/////////////////////////////////////

bool ProbeManager::SaveProbes()
{
	const bool saved = SaveIrradianceProbes();

	if (saved) {
		SaveReflectionProbes();
	}

	return saved;
}

bool ProbeManager::LoadProbes()
{
	const bool loaded = LoadIrradianceProbes();

	if (!loaded || !LoadReflectionProbes()) {
		mReflectionProbeCount = 0;
		mbReflectionsBaked = false;
		UploadReflectionProbes(0);
	}

	return loaded;
}

bool ProbeManager::SaveIrradianceProbes()
{
	if (IsBaking()) {
		LogWarning("Cannot save the probes while they are baking");
		return false;
	}

	const String path = GetProbeFilePath();

	File file(path, File::eModType::Write, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		LogError("Could not open {} to save the probes", path.CStr());
		return false;
	}

	ProbeFileHeader header;
	header.VolumeCount = mVolumeCount;
	header.ProbeCount = mProbeCount;
	header.GridPointCount = mGridPointCount;

	// Only the probes the volumes use are written, so a scene with a handful of small volumes gets a small file
	file.WriteRaw(&header, sizeof(header));
	file.WriteRaw(mVolumes, sizeof(mVolumes));
	file.WriteRaw(mVolumeRanges, sizeof(mVolumeRanges));
	file.WriteRaw(mGridProbes, mGridPointCount * sizeof(uint16));
	file.WriteRaw(mProbes, mProbeCount * sizeof(ProbeSHData));
	file.WriteRaw(mProbeInfos, mProbeCount * sizeof(ProbeInfo));

	LogInfo("Saved {} light probes across {} volume(s) to {}", mProbeCount, mVolumeCount, path.CStr());
	return true;
}

bool ProbeManager::LoadIrradianceProbes()
{
	const String path = GetProbeFilePath();

	File file(path, File::eModType::Read, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		LogInfo("No probe file at {}, using procedural probes", path.CStr());
		return false;
	}

	ProbeFileHeader header;

	if (!ReadExact(file, &header, sizeof(header)) || !IsCacheLayoutReadable(header.Layout)) {
		LogWarning("Probe file {} is out of date or not a probe file, rebake the probes", path.CStr());
		return false;
	}

	if (header.VolumeCount > Limits::MaxProbeVolumes || header.ProbeCount > Limits::MaxIrradianceProbes ||
		header.GridPointCount > Limits::MaxProbeGridPoints) {
		LogError("Probe file {} holds {} volumes, {} probes and {} grid points, past the limits of {}, {} and {}",
				 path.CStr(), header.VolumeCount, header.ProbeCount, header.GridPointCount, Limits::MaxProbeVolumes,
				 Limits::MaxIrradianceProbes, Limits::MaxProbeGridPoints);
		return false;
	}

	if (file.GetFileSize() != GetProbeFileSize(header.ProbeCount, header.GridPointCount)) {
		LogError("Probe file {} is {} bytes, not the {} its {} probes need", path.CStr(), file.GetFileSize(),
				 GetProbeFileSize(header.ProbeCount, header.GridPointCount), header.ProbeCount);
		return false;
	}

	ProbeVolumeData volumes[Limits::MaxProbeVolumes] {};
	ProbeVolumeRange ranges[Limits::MaxProbeVolumes] {};
	std::vector<uint16> grid(header.GridPointCount);

	if (!ReadExact(file, volumes, sizeof(volumes)) || !ReadExact(file, ranges, sizeof(ranges)) ||
		!ReadExact(file, grid.data(), grid.size() * sizeof(uint16))) {
		LogError("Could not read the probe volumes from {}", path.CStr());
		return false;
	}

	for (uint32 volume = 0; volume < header.VolumeCount; volume++) {
		const uint32* dims = volumes[volume].DimsAndFirst;
		const uint64 last_point = static_cast<uint64>(dims[3]) + static_cast<uint64>(dims[0]) * dims[1] * dims[2];
		const uint64 last_probe = static_cast<uint64>(ranges[volume].FirstProbe) + ranges[volume].ProbeCount;

		if (last_point > header.GridPointCount || last_probe > header.ProbeCount) {
			LogError("Probe file {} has a volume that runs past the probes it holds, rebake the probes", path.CStr());
			return false;
		}
	}

	for (uint16 probe : grid) {
		if (probe != Limits::ProbeGridEmpty && probe >= header.ProbeCount) {
			LogError("Probe file {} points at probe {} of {}, rebake the probes", path.CStr(), probe,
					 header.ProbeCount);
			return false;
		}
	}

	if (!ReadExact(file, mProbes, header.ProbeCount * sizeof(ProbeSHData)) ||
		!ReadExact(file, mProbeInfos, header.ProbeCount * sizeof(ProbeInfo))) {
		LogError("Could not read the probes from {}", path.CStr());
		return false;
	}

	memcpy(mVolumes, volumes, sizeof(mVolumes));
	memcpy(mVolumeRanges, ranges, sizeof(mVolumeRanges));
	std::copy(grid.begin(), grid.end(), mGridProbes);
	mVolumeCount = header.VolumeCount;
	mProbeCount = header.ProbeCount;
	mGridPointCount = header.GridPointCount;
	mbGridDirty = true;

	RefreshVolumeCounts();

	UploadToGpu(0, mProbeCount);

	LogInfo("Loaded {} light probes across {} volume(s) from {}", mProbeCount, mVolumeCount, path.CStr());
	return true;
}

namespace {

constexpr float32 scReflectionBlendDistance = 0.25f;
constexpr float32 scReflectionMinHalfExtent = 0.05f;
constexpr float32 scReflectionRelocation = 0.9f;
constexpr uint32 scReflectionFileVersion = 1;

constexpr uint64 scReflectionTexelsPerProbe = 6ull * (128 * 128 + 64 * 64 + 32 * 32 + 16 * 16 + 8 * 8 + 4 * 4);
constexpr uint64 scReflectionHalfsPerProbe = scReflectionTexelsPerProbe * 4;

static_assert(Limits::ReflectionProbeSize == 128 && Limits::ReflectionProbeMips == 6);

struct ReflectionBox
{
	Mat4f BoxToWorld;
	Vec3f HalfExtent;
	float32 Volume;
};

struct ReflectionFileHeader
{
	char Magic[4] = { 'R', 'P', 'R', 'F' };
	uint32 Version = scReflectionFileVersion;
	uint32 Size = Limits::ReflectionProbeSize;
	uint32 Mips = Limits::ReflectionProbeMips;
	uint32 ProbeCount = 0;
};

ReflectionBox MakeReflectionBox(const Vec3f& local_min, const Vec3f& local_max, const Mat4f& local_to_world)
{
	const Vec3f half_extent = Vec3f::Max((local_max - local_min) * 0.5f, Vec3f(scReflectionMinHalfExtent));
	const Vec3f center = (local_max + local_min) * 0.5f;

	ReflectionBox box;
	box.BoxToWorld = Mat4f::AsScale(half_extent) * Mat4f::AsTranslation(center) * local_to_world;
	box.HalfExtent = half_extent;
	box.Volume = half_extent.X * half_extent.Y * half_extent.Z;

	return box;
}

String GetReflectionFilePath()
{
	std::filesystem::path path(GetProbeFilePath().CStr());
	path.replace_extension(".fxrefl");

	return String(path.string().c_str());
}

void SampleCaptureFace(const std::vector<float32>& face, uint32 size, float32 ndc_x, float32 ndc_y, float32 out[3])
{
	const float32 max_coord = static_cast<float32>(size - 1);

	const float32 x = std::clamp((ndc_x * 0.5f + 0.5f) * static_cast<float32>(size) - 0.5f, 0.0f, max_coord);
	const float32 y = std::clamp((ndc_y * 0.5f + 0.5f) * static_cast<float32>(size) - 0.5f, 0.0f, max_coord);

	const uint32 x0 = static_cast<uint32>(x);
	const uint32 y0 = static_cast<uint32>(y);
	const uint32 x1 = std::min(x0 + 1, size - 1);
	const uint32 y1 = std::min(y0 + 1, size - 1);

	const float32 fx = x - static_cast<float32>(x0);
	const float32 fy = y - static_cast<float32>(y0);

	const float32* t00 = face.data() + (y0 * size + x0) * 3;
	const float32* t10 = face.data() + (y0 * size + x1) * 3;
	const float32* t01 = face.data() + (y1 * size + x0) * 3;
	const float32* t11 = face.data() + (y1 * size + x1) * 3;

	for (uint32 c = 0; c < 3; c++) {
		const float32 top = t00[c] + (t10[c] - t00[c]) * fx;
		const float32 bottom = t01[c] + (t11[c] - t01[c]) * fx;

		out[c] = top + (bottom - top) * fy;
	}
}

} // namespace

Vec3f ProbeManager::GetReflectionProbePosition(uint32 index) const
{
	const float32* position = mReflectionProbes[index].PositionAndCount;
	return Vec3f(position[0], position[1], position[2]);
}

uint32 ProbeManager::RebuildReflectionProbesFromWorld()
{
	if (IsBaking()) {
		LogWarning("Cannot rebuild the reflection probes while the probes are baking");
		return 0;
	}

	const ProbePlacementBoxes boxes = GatherPlacementBoxes();

	std::vector<ReflectionBox> reflection_boxes;

	for (Object& object : gObjectManager->GetCache()) {
		if (object.IsReflectionProbe()) {
			reflection_boxes.push_back(
				MakeReflectionBox(object.Bounds.Min, object.Bounds.Max, object.GetWorldMatrix()));
		}
	}

	std::sort(reflection_boxes.begin(), reflection_boxes.end(),
			  [](const ReflectionBox& a, const ReflectionBox& b) { return a.Volume < b.Volume; });


	const uint32 num_brush_probes = static_cast<uint32>(reflection_boxes.size());
	const bool has_level_probe = !boxes.IsEmpty() && gCVars->Get("r_reflection_level_probe", int64(0)) != 0;

	uint32 max_brush_probes = Limits::MaxReflectionProbes;

	if (has_level_probe) {
		--max_brush_probes;
	}

	if (reflection_boxes.size() > max_brush_probes) {
		LogWarning("{} reflection probe brushes are placed but only {} fit, the largest are left out",
				   reflection_boxes.size(), max_brush_probes);
		reflection_boxes.resize(max_brush_probes);
	}

	if (has_level_probe) {
		reflection_boxes.push_back(MakeReflectionBox(boxes.Min, boxes.Max, Mat4f::scIdentity));
	}

	mReflectionProbeCount = static_cast<uint32>(reflection_boxes.size());
	mbReflectionsBaked = false;

	for (uint32 i = 0; i < mReflectionProbeCount; i++) {
		const ReflectionBox& box = reflection_boxes[i];
		ReflectionProbeData& probe = mReflectionProbes[i];

		const Vec4f center_h = box.BoxToWorld * Vec4f(0.0f, 0.0f, 0.0f, 1.0f);
		const Vec3f center(center_h.X, center_h.Y, center_h.Z);

		Vec3f position = center;

		if (!IsProbePlacementValid(center, center, boxes) &&
			!FindValidProbePosition(center, center, box.HalfExtent * scReflectionRelocation, boxes, position)) {
			LogWarning("Reflection probe {} is captured from inside of the level's geometry, move its brush", i);
			position = center;
		}

		memcpy(probe.WorldToBox, box.BoxToWorld.Inverse().RawData, sizeof(probe.WorldToBox));

		probe.PositionAndCount[0] = position.X;
		probe.PositionAndCount[1] = position.Y;
		probe.PositionAndCount[2] = position.Z;
		probe.PositionAndCount[3] = 0.0f;

		probe.Fade[0] = box.HalfExtent.X / scReflectionBlendDistance;
		probe.Fade[1] = box.HalfExtent.Y / scReflectionBlendDistance;
		probe.Fade[2] = box.HalfExtent.Z / scReflectionBlendDistance;
		probe.Fade[3] = 0.0f;
	}

	UploadReflectionProbes(0);

	LogInfo("Reflection probes rebuilt: {} from editor brushes, {} in total",
			std::min(num_brush_probes, max_brush_probes), mReflectionProbeCount);

	return mReflectionProbeCount;
}

void ProbeManager::BeginReflectionBake()
{
	if (IsBaking()) {
		LogWarning("A probe bake is already in progress");
		return;
	}

	if (mReflectionProbeCount == 0) {
		LogError("Reflection bake failed: no reflection probes have been placed");
		return;
	}

	StartReflectionPhase();
}

void ProbeManager::StartReflectionPhase()
{
	mBakePhase = eBakePhase::Reflection;
	mCurrentProbe = 0;
	mbReflectionsBaked = false;
	mBakeState = eBakeState::CapturePending;

	mReflectionTexels.assign(mReflectionProbeCount * scReflectionHalfsPerProbe, 0);

	UploadReflectionProbes(0);

	LogInfo("Reflection probe bake started ({} probes)", mReflectionProbeCount);
}

void ProbeManager::RecordReflectionCapture(renderer::CommandBuffer& cmd, const RenderFaceFunc& render_face)
{
	mBatchStart = mCurrentProbe;

	const Vec3f position = GetReflectionProbePosition(mCurrentProbe);

	mbCapturingFaces = true;

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		PerspectiveCamera camera = MakeCaptureCamera(position, face);

		render_face(camera, mReflectionStage);

		CopyTargetToStaging(cmd, mReflectionStage, Limits::ReflectionProbeSize, eImageFormat::RGBA16_Float,
							mReflectionStaging[face]);
	}

	mbCapturingFaces = false;
	mCurrentProbe++;
	mBakeState = eBakeState::CaptureRecorded;
}

void ProbeManager::ServiceReflectionBake()
{
	const uint32 probe = mBatchStart;

	if (!ReadBackReflectionProbe(probe)) {
		LogError("Reflection probe bake failed: could not read back probe {}", probe);
		mBakePhase = eBakePhase::Irradiance;
		mBakeState = eBakeState::Idle;
		return;
	}

	UploadReflectionCubemap(probe);

	if (mCurrentProbe < mReflectionProbeCount) {
		mBakeState = eBakeState::CapturePending;
		LogInfo("Reflection probe bake progress: {}/{}", mCurrentProbe, mReflectionProbeCount);
		return;
	}

	mBakePhase = eBakePhase::Irradiance;
	mBakeState = eBakeState::Idle;
	mbReflectionsBaked = true;

	UploadReflectionProbes(mReflectionProbeCount);

	LogInfo("Reflection probe bake complete ({} probes)", mReflectionProbeCount);
}

bool ProbeManager::ReadBackReflectionProbe(uint32 probe_index)
{
	constexpr uint32 cSize = Limits::ReflectionProbeSize;
	constexpr uint32 cPixels = cSize * cSize;

	std::vector<float32> captures[scCaptureFaces];
	bool mapped = true;

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		renderer::RawGpuBuffer& staging = mReflectionStaging[face];

		staging.Map();
		staging.InvalidateFromGpu();

		const uint16* rgba = static_cast<const uint16*>(staging.GetMapped());

		if (rgba == nullptr) {
			mapped = false;
		}
		else {
			captures[face].resize(cPixels * 3);

			for (uint32 pixel = 0; pixel < cPixels; pixel++) {
				for (uint32 c = 0; c < 3; c++) {
					captures[face][pixel * 3 + c] = std::clamp(HalfToFloat(rgba[pixel * 4 + c]), 0.0f, scRadianceClamp);
				}
			}
		}

		staging.UnMap();
	}

	if (!mapped) {
		return false;
	}

	std::vector<float32> cube[scCaptureFaces];
	const float32* cube_faces[scCaptureFaces];

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		cube[face].resize(cPixels * 3);
		float32* out = cube[face].data();

		for (uint32 y = 0; y < cSize; y++) {
			for (uint32 x = 0; x < cSize; x++, out += 3) {
				const float32 u = ((static_cast<float32>(x) + 0.5f) / static_cast<float32>(cSize)) * 2.0f - 1.0f;
				const float32 v = ((static_cast<float32>(y) + 0.5f) / static_cast<float32>(cSize)) * 2.0f - 1.0f;

				const Vec3f direction = ReflectionFilter::FaceUVToDirection(face, u, v);

				uint32 capture_face;
				float32 unused_u;
				float32 unused_v;
				ReflectionFilter::DirectionToFaceUV(direction, capture_face, unused_u, unused_v);

				const Vec4f clip = mCaptureFaceToClip[capture_face] *
								   Vec4f(direction.X, direction.Y, direction.Z, 1.0f);

				SampleCaptureFace(captures[capture_face], cSize, clip.X / clip.W, clip.Y / clip.W, out);
			}
		}

		cube_faces[face] = cube[face].data();
	}

	ReflectionFilter::Prefilter(cube_faces, cSize, Limits::ReflectionProbeMips,
								mReflectionTexels.data() + probe_index * scReflectionHalfsPerProbe);

	return true;
}

void ProbeManager::UploadReflectionProbes(uint32 visible_count)
{
	renderer::GraphicsBackend* graphics = renderer::gGraphics;

	graphics->GetDevice()->WaitForIdle();

	ReflectionProbeData gpu_probes[Limits::MaxReflectionProbes];
	memcpy(gpu_probes, mReflectionProbes, sizeof(gpu_probes));

	for (ReflectionProbeData& probe : gpu_probes) {
		probe.PositionAndCount[3] = static_cast<float32>(visible_count);
		probe.Fade[3] = static_cast<float32>(mReflectionProbeCount);
	}

	UploadRange(graphics->ReflectionProbeBuffer, gpu_probes, 0, sizeof(gpu_probes));
}

void ProbeManager::UploadReflectionCubemap(uint32 probe_index)
{
	using namespace renderer;

	GraphicsBackend* graphics = gGraphics;
	Image* image = graphics->pReflectionProbes;
	Assert(image != nullptr);

	graphics->GetDevice()->WaitForIdle();

	const uint64 bytes = scReflectionHalfsPerProbe * sizeof(uint16);

	RawGpuBuffer staging;
	staging.Create(eGpuBufferType::Transfer, bytes, RX_MEMORY_CPU_TO_GPU, eGpuBufferFlags::TransferReceiver);
	staging.Upload(mReflectionTexels.data() + probe_index * scReflectionHalfsPerProbe, bytes);

	VkBufferImageCopy regions[Limits::ReflectionProbeMips];
	uint64 offset = 0;

	for (uint32 mip = 0; mip < Limits::ReflectionProbeMips; mip++) {
		const uint32 mip_size = Limits::ReflectionProbeSize >> mip;

		regions[mip] = VkBufferImageCopy {
			.bufferOffset = offset,
			.imageSubresource {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = mip,
				.baseArrayLayer = probe_index * Limits::ReflectionProbeFaces,
				.layerCount = Limits::ReflectionProbeFaces,
			},
			.imageExtent { .width = mip_size, .height = mip_size, .depth = 1 },
		};

		offset += static_cast<uint64>(mip_size) * mip_size * Limits::ReflectionProbeFaces * 4 * sizeof(uint16);
	}

	graphics->SubmitImmediateUploadCmd(
		[&](CommandBuffer& cmd)
		{
			BarrierHelper::ImageLayoutTransition(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd, 0,
												 Limits::ReflectionProbeMips);

			vkCmdCopyBufferToImage(cmd, staging.Get(), image->Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
								   Limits::ReflectionProbeMips, regions);

			BarrierHelper::ImageLayoutTransition(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 0,
												 Limits::ReflectionProbeMips);
		});

	staging.Destroy();
}

bool ProbeManager::SaveReflectionProbes()
{
	if (mReflectionProbeCount > 0 && !mbReflectionsBaked) {
		LogWarning("The reflection probes are not baked, so they were not saved");
		return false;
	}

	const String path = GetReflectionFilePath();

	if (mReflectionProbeCount == 0 && !File(path, File::eModType::Read, File::eDataType::Binary).IsFileOpen()) {
		return true;
	}

	File file(path, File::eModType::Write, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		LogError("Could not open {} to save the reflection probes", path.CStr());
		return false;
	}

	ReflectionFileHeader header;
	header.ProbeCount = mReflectionProbeCount;

	file.WriteRaw(&header, sizeof(header));
	file.WriteRaw(mReflectionProbes, mReflectionProbeCount * sizeof(ReflectionProbeData));
	file.WriteRaw(mReflectionTexels.data(), mReflectionProbeCount * scReflectionHalfsPerProbe * sizeof(uint16));

	LogInfo("Saved {} reflection probes to {}", mReflectionProbeCount, path.CStr());
	return true;
}

bool ProbeManager::LoadReflectionProbes()
{
	const String path = GetReflectionFilePath();

	File file(path, File::eModType::Read, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		LogInfo("No reflection probe file at {}", path.CStr());
		return false;
	}

	const ReflectionFileHeader expected {};
	ReflectionFileHeader header;

	if (!ReadExact(file, &header, sizeof(header)) || memcmp(header.Magic, expected.Magic, sizeof(header.Magic)) != 0 ||
		header.Version != expected.Version || header.Size != expected.Size || header.Mips != expected.Mips ||
		header.ProbeCount > Limits::MaxReflectionProbes) {
		LogWarning("Reflection probe file {} is out of date or not a reflection probe file, rebake the probes",
				   path.CStr());
		return false;
	}

	const uint64 texel_bytes = header.ProbeCount * scReflectionHalfsPerProbe * sizeof(uint16);
	const uint64 expected_size = sizeof(header) + header.ProbeCount * sizeof(ReflectionProbeData) + texel_bytes;

	if (file.GetFileSize() != expected_size) {
		LogError("Reflection probe file {} is {} bytes, not the {} its {} probes need", path.CStr(), file.GetFileSize(),
				 expected_size, header.ProbeCount);
		return false;
	}

	ReflectionProbeData probes[Limits::MaxReflectionProbes] {};
	std::vector<uint16> texels(header.ProbeCount * scReflectionHalfsPerProbe);

	if (!ReadExact(file, probes, header.ProbeCount * sizeof(ReflectionProbeData)) ||
		!ReadExact(file, texels.data(), texel_bytes)) {
		LogError("Could not read the reflection probes from {}", path.CStr());
		return false;
	}

	memcpy(mReflectionProbes, probes, sizeof(mReflectionProbes));
	mReflectionTexels = std::move(texels);
	mReflectionProbeCount = header.ProbeCount;
	mbReflectionsBaked = true;

	for (uint32 probe = 0; probe < mReflectionProbeCount; probe++) {
		UploadReflectionCubemap(probe);
	}

	UploadReflectionProbes(mReflectionProbeCount);

	LogInfo("Loaded {} reflection probes from {}", mReflectionProbeCount, path.CStr());
	return true;
}

} // namespace fx
