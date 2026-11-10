#include "Brush.hpp"

#include <Core/DynArray.hpp>
#include <Core/HashMap.hpp>
#include <Math/DVec3.hpp>
#include <Math/MathUtil.hpp>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>

namespace fx {

namespace {

// Some of the ideas used in here are based off of the ones shown in the Quake BSP tools, as well as shared polyhedron
// stuff to share vertices in Trenchbroom.
//
// Sources for these can be found here:
// https://github.com/id-Software/Quake-Tools/blob/master/qutils/QBSP
// https://github.com/TrenchBroom/TrenchBroom.
//
// Although the algorithms here are loosely inspired by these sources, everything here is written by me.

struct DPlane
{
	DVec3 Normal;
	double Distance = 0.0;

	/// Index of the brush plane this came from, to carry its texture over
	uint32 SourceIndex = 0;

	double SignedDistance(const DVec3& point) const { return Normal.Dot(point) - Distance; }
};

/// Corners closer than this to a plane are treated as lying on it
constexpr double scOnPlaneEpsilon = 1e-5;

constexpr double scMinNormalLength = 1e-8;

/// How far a normal component can be from 0 or 1 and still count as axis aligned
constexpr float32 scAxisAlignedEpsilon = 1e-6f;

/// How close to 1 the dot product of two normals has to be for FindPlane() to match them
constexpr float32 scFindPlaneEpsilon = 1e-3f;

/// Plane index given to the faces of the initial box.
constexpr int32 scBoundingPlane = -1;

struct TextureProjection
{
	FVec3 UAxis;
	FVec3 VAxis;
};

TextureProjection GetTextureProjection(const FVec3 normal)
{
	const float32 abs_x = std::abs(normal.X);
	const float32 abs_y = std::abs(normal.Y);
	const float32 abs_z = std::abs(normal.Z);

	TextureProjection projection {};

	if (abs_y >= abs_x && abs_y >= abs_z) {
		projection = { FVec3(1.0f, 0.0f, 0.0f), FVec3(0.0f, 0.0f, 1.0f) };
	}
	else if (abs_x >= abs_z) {
		projection = { FVec3(0.0f, 0.0f, 1.0f), FVec3(0.0f, -1.0f, 0.0f) };
	}
	else {
		projection = { FVec3(1.0f, 0.0f, 0.0f), FVec3(0.0f, -1.0f, 0.0f) };
	}

	if (projection.UAxis.Cross(projection.VAxis).Dot(normal) < 0.0f) {
		projection.UAxis = projection.UAxis * -1.0f;
	}

	return projection;
}

bool HasSameTextureProjection(const FVec3 a, const FVec3 b)
{
	const TextureProjection projection_a = GetTextureProjection(a);
	const TextureProjection projection_b = GetTextureProjection(b);

	return projection_a.UAxis == projection_b.UAxis && projection_a.VAxis == projection_b.VAxis;
}

FVec2 GetDefaultTextureOffset(const FVec3 normal, const FVec3 bounds_min, const FVec3 bounds_max)
{
	const TextureProjection projection = GetTextureProjection(normal);

	// V runs down walls, so the texture starts at the top of the brush
	const FVec3 direction = projection.UAxis + projection.VAxis;

	float32 anchor_x = bounds_min.X;
	if (direction.X < 0.0f) {
		anchor_x = bounds_max.X;
	}

	float32 anchor_z = bounds_min.Z;
	if (direction.Z < 0.0f) {
		anchor_z = bounds_max.Z;
	}

	const FVec3 anchor(anchor_x, bounds_max.Y, anchor_z);

	static const BrushFaceTexture scDefaultTexture {};

	return FVec2(-anchor.Dot(projection.UAxis) / scDefaultTexture.Scale.X,
				 -anchor.Dot(projection.VAxis) / scDefaultTexture.Scale.Y);
}

/// The layout of a face on a brush at `origin`, lined up with the world grid. UVs use the brush's local corners, which
/// are `origin` away from where they are in the world.
BrushFaceTexture GetWorldAlignedTexture(const FVec3 normal, const FVec3 origin)
{
	const TextureProjection projection = GetTextureProjection(normal);


	BrushFaceTexture texture {};

	texture.Offset = FVec2(origin.Dot(projection.UAxis) / texture.Scale.X,
						   origin.Dot(projection.VAxis) / texture.Scale.Y);

	return texture;
}

bool IsNearlyEqual(const DVec3& a, const DVec3& b, double epsilon)
{
	return std::abs(a.X - b.X) <= epsilon && std::abs(a.Y - b.Y) <= epsilon && std::abs(a.Z - b.Z) <= epsilon;
}

/// Packs a directed edge between two corners into a single key.
uint64 MakeEdgeKey(uint32 from, uint32 to) { return (static_cast<uint64>(from) << 32) | to; }

void GetTextureRotation(const BrushFaceTexture& texture, float32& out_sine, float32& out_cosine)
{
	out_sine = 0.0f;
	out_cosine = 1.0f;

	if (texture.Rotation != 0.0f) {
		const float32 radians = MathUtil::DegreesToRadians(texture.Rotation);
		out_sine = std::sin(radians);
		out_cosine = std::cos(radians);
	}
}

/// The polyhedron's lists grow with every clip so they grow by doubling, leading to less reallocations
template <typename TElementType>
using DoublingArray = DynArray<TElementType, GrowthFunctions::Double>;

/**
 * @brief A convex polyhedron with shared and indexed corners
 */
struct Polyhedron
{
	struct Loop
	{
		int32 PlaneIndex = scBoundingPlane;

		/// Clockwise around the outward normal, matching the winding of the engine's generated geometry
		DoublingArray<uint32> Corners;
	};

	enum class eClipResult
	{
		Unchanged,
		Clipped,
		Empty,
	};


public:
	static Polyhedron MakeBox(double half_extent);

	/**
	 * @brief Cuts away everything in front of the plane, closing the hole with a new face on the plane.
	 */
	eClipResult Clip(const DPlane& plane, int32 plane_index);

public:
	DoublingArray<DVec3> Corners;
	DoublingArray<Loop> Faces;
};

Polyhedron Polyhedron::MakeBox(double half_extent)
{
	Polyhedron box;

	// A corner takes its sign on each axis from bits 0, 1 and 2
	for (uint32 corner = 0; corner < 8; corner++) {
		double x = -half_extent;
		if (corner & 1) {
			x = half_extent;
		}

		double y = -half_extent;
		if (corner & 2) {
			y = half_extent;
		}

		double z = -half_extent;
		if (corner & 4) {
			z = half_extent;
		}

		box.Corners.Insert(DVec3(x, y, z));
	}

	const uint32 faces[6][4] = {
		{ 1, 3, 7, 5 }, { 0, 4, 6, 2 }, { 2, 6, 7, 3 }, { 0, 1, 5, 4 }, { 4, 5, 7, 6 }, { 0, 2, 3, 1 },
	};

	for (const auto& face : faces) {
		Loop loop;
		for (uint32 corner : face) {
			loop.Corners.Insert(corner);
		}

		const DVec3& a = box.Corners[loop.Corners[0]];
		const DVec3 normal = (box.Corners[loop.Corners[1]] - a).Cross(box.Corners[loop.Corners[2]] - a);

		// Make sure the winding is clockwise around the outward normal, so the cross product points inwards
		if (normal.Dot(a) > 0.0) {
			std::reverse(loop.Corners.begin(), loop.Corners.end());
		}

		box.Faces.Emplace(std::move(loop));
	}

	return box;
}

Polyhedron::eClipResult Polyhedron::Clip(const DPlane& plane, int32 plane_index)
{
	enum class eSide : uint8
	{
		Inside,
		On,
		Outside,
	};

	// Corners created by this clip are appended after these, and always lie on the plane
	const uint32 corner_count = Corners.Size;

	SizedArray<eSide> sides;
	sides.InitSize(corner_count);

	SizedArray<double> distances;
	distances.InitSize(corner_count);

	// Corners cut away by earlier clips are left in the list, so only look at the ones still in use
	SizedArray<bool> in_use;
	in_use.InitSize(corner_count, false);

	for (const Loop& face : Faces) {
		for (uint32 corner : face.Corners) {
			in_use[corner] = true;
		}
	}

	bool has_inside = false;
	bool has_outside = false;

	for (uint32 i = 0; i < corner_count; i++) {
		distances[i] = plane.SignedDistance(Corners[i]);

		if (distances[i] > scOnPlaneEpsilon) {
			sides[i] = eSide::Outside;
			has_outside |= in_use[i];
		}
		else if (distances[i] < -scOnPlaneEpsilon) {
			sides[i] = eSide::Inside;
			has_inside |= in_use[i];
		}
		else {
			sides[i] = eSide::On;
		}
	}

	if (!has_outside) {
		return eClipResult::Unchanged;
	}

	if (!has_inside) {
		return eClipResult::Empty;
	}

	// Corners that lie on the plane after the clip, including the ones created by it
	auto IsOnPlane = [&](uint32 corner) { return corner >= corner_count || sides[corner] == eSide::On; };

	// Each edge that crosses the plane is split once, and both faces sharing the edge reuse the new corner
	HashMap<uint64, uint32> split_corners;

	auto GetSplitCorner = [&](uint32 a, uint32 b)
	{
		// The same edge is walked in opposite directions by its two faces
		const uint64 key = MakeEdgeKey(std::min(a, b), std::max(a, b));

		const uint32* existing = split_corners.Find(key);
		if (existing != nullptr) {
			return *existing;
		}

		const double t = distances[a] / (distances[a] - distances[b]);
		Corners.Insert(Corners[a] + (Corners[b] - Corners[a]) * t);

		const uint32 index = Corners.Size - 1;
		split_corners.Insert(key, index);

		return index;
	};

	DoublingArray<Loop> clipped_faces;

	for (const Loop& face : Faces) {
		Loop clipped;
		clipped.PlaneIndex = face.PlaneIndex;

		const uint32 count = face.Corners.Size;

		for (uint32 i = 0; i < count; i++) {
			const uint32 current = face.Corners[i];
			const uint32 next = face.Corners[(i + 1) % count];

			const eSide current_side = sides[current];
			const eSide next_side = sides[next];

			if (current_side != eSide::Outside) {
				clipped.Corners.Insert(current);
			}

			if ((current_side == eSide::Inside && next_side == eSide::Outside) ||
				(current_side == eSide::Outside && next_side == eSide::Inside)) {
				clipped.Corners.Insert(GetSplitCorner(current, next));
			}
		}

		// A face left with nothing but corners on the plane has been cut down to an edge or a point
		const bool is_flat = std::all_of(clipped.Corners.begin(), clipped.Corners.end(), IsOnPlane);

		if (clipped.Corners.Size >= 3 && !is_flat) {
			clipped_faces.Emplace(std::move(clipped));
		}
	}

	// The new face is bounded by the edges on the plane that no longer have a neighbouring face. Each one is
	// walked in reverse, since the new face is on the other side of the edge

	HashMap<uint64, bool> edges;

	for (const Loop& face : clipped_faces) {
		for (uint32 i = 0; i < face.Corners.Size; i++) {
			edges.Insert(MakeEdgeKey(face.Corners[i], face.Corners[(i + 1) % face.Corners.Size]), true);
		}
	}

	HashMap<uint32, uint32> cap_next;

	for (const Loop& face : clipped_faces) {
		for (uint32 i = 0; i < face.Corners.Size; i++) {
			const uint32 a = face.Corners[i];
			const uint32 b = face.Corners[(i + 1) % face.Corners.Size];

			if (IsOnPlane(a) && IsOnPlane(b) && !edges.Contains(MakeEdgeKey(b, a))) {
				cap_next[b] = a;
			}
		}
	}

	if (cap_next.Size < 3) {
		return eClipResult::Empty;
	}

	Loop cap;
	cap.PlaneIndex = plane_index;

	const uint32 start = cap_next.begin()->Key;
	uint32 corner = start;

	do {
		cap.Corners.Insert(corner);

		const uint32* next = cap_next.Find(corner);
		if (next == nullptr || cap.Corners.Size > cap_next.Size) {
			// The boundary is not a single loop, which only happens with degenerate input
			return eClipResult::Empty;
		}

		corner = *next;
	} while (corner != start);

	if (cap.Corners.Size != cap_next.Size) {
		return eClipResult::Empty;
	}

	clipped_faces.Emplace(std::move(cap));
	Faces = std::move(clipped_faces);

	return eClipResult::Clipped;
}

} // namespace


Brush Brush::FromBox(const FVec3 min, const FVec3 max)
{
	Brush brush;

	brush.Planes.Insert({ FVec3(1.0f, 0.0f, 0.0f), max.X });
	brush.Planes.Insert({ FVec3(-1.0f, 0.0f, 0.0f), -min.X });
	brush.Planes.Insert({ FVec3(0.0f, 1.0f, 0.0f), max.Y });
	brush.Planes.Insert({ FVec3(0.0f, -1.0f, 0.0f), -min.Y });
	brush.Planes.Insert({ FVec3(0.0f, 0.0f, 1.0f), max.Z });
	brush.Planes.Insert({ FVec3(0.0f, 0.0f, -1.0f), -min.Z });

	brush.Rebuild();

	for (uint32 i = 0; i < brush.Planes.Size; i++) {
		brush.ResetFaceTexture(i);
	}

	return brush;
}

Brush Brush::FromPlanes(const PlaneList& planes)
{
	Brush brush;
	brush.Planes = planes;
	brush.Rebuild();

	return brush;
}

Brush Brush::FromVertices(const SizedArray<FVec3>& points, const PlaneList& reference)
{
	if (points.Size < 4) {
		return {};
	}

	constexpr double scHullPlaneEpsilon = 1e-4;
	constexpr double scHullPlaneRelativeEpsilon = 1e-6;
	constexpr double scHullSameNormal = 1e-6;

	SizedArray<DVec3> corners;
	corners.InitCapacity(points.Size);

	double extent = 0.0;

	for (const FVec3& point : points) {
		corners.Insert(DVec3(point));
		extent = std::max({ extent, std::fabs(static_cast<double>(point.X)), std::fabs(static_cast<double>(point.Y)),
							std::fabs(static_cast<double>(point.Z)) });
	}

	const double epsilon = scHullPlaneEpsilon + extent * scHullPlaneRelativeEpsilon;

	struct HullPlane
	{
		DVec3 Normal = DVec3(0.0);
		double Distance = 0.0;
	};

	StackArray<HullPlane, scMaxPlanes> hull_planes;

	for (size_t i = 0; i < corners.Size; i++) {
		for (size_t j = i + 1; j < corners.Size; j++) {
			for (size_t k = j + 1; k < corners.Size; k++) {
				DVec3 normal = (corners[j] - corners[i]).Cross(corners[k] - corners[i]);
				const double length = normal.Length();

				if (!(length > scMinNormalLength)) {
					continue;
				}

				normal = normal * (1.0 / length);
				double distance = normal.Dot(corners[i]);

				double lowest = DBL_MAX;
				double highest = -DBL_MAX;

				for (const DVec3& corner : corners) {
					const double side = normal.Dot(corner) - distance;

					lowest = std::min(lowest, side);
					highest = std::max(highest, side);
				}

				if (highest > epsilon) {
					if (lowest < -epsilon) {
						continue;
					}

					normal = normal * -1.0;
					distance = -distance;
				}

				bool is_duplicate = false;

				for (const HullPlane& existing : hull_planes) {
					if (existing.Normal.Dot(normal) > 1.0 - scHullSameNormal &&
						std::fabs(existing.Distance - distance) < epsilon) {
						is_duplicate = true;
						break;
					}
				}

				if (is_duplicate) {
					continue;
				}

				if (hull_planes.Size >= scMaxPlanes) {
					return {};
				}

				hull_planes.Insert(HullPlane { normal, distance });
			}
		}
	}

	if (hull_planes.Size < 4) {
		return {};
	}

	PlaneList planes;

	for (const HullPlane& plane : hull_planes) {
		planes.Insert(BrushPlane { plane.Normal.ToFVec3(), static_cast<float32>(plane.Distance), {} });
	}

	Brush brush = FromPlanes(planes);

	if (!brush.IsValid()) {
		return {};
	}

	for (uint32 i = 0; i < brush.Planes.Size; i++) {
		const FVec3 normal = brush.Planes[i].Normal;

		int32 best = scNoPlane;
		float32 best_dot = 1.0f - scFindPlaneEpsilon;

		for (uint32 j = 0; j < reference.Size; j++) {
			const float32 dot = reference[j].Normal.Dot(normal);

			if (dot > best_dot) {
				best_dot = dot;
				best = static_cast<int32>(j);
			}
		}

		if (best == scNoPlane) {
			best_dot = -FLT_MAX;

			for (uint32 j = 0; j < reference.Size; j++) {
				const float32 dot = reference[j].Normal.Dot(normal);

				if (dot > best_dot && HasSameTextureProjection(reference[j].Normal, normal)) {
					best_dot = dot;
					best = static_cast<int32>(j);
				}
			}
		}

		if (best != scNoPlane) {
			brush.Planes[i].Texture = reference[best].Texture;
			brush.Planes[i].Material = reference[best].Material;
		}
		else {
			brush.ResetFaceTexture(i);
		}
	}

	return brush;
}

bool Brush::Rebuild()
{
	constexpr double scSameNormalEpsilon = 1e-9;
	constexpr double scExtentTooBig = 1e5;
	constexpr double scDistanceToMerge = 1e-6;
	constexpr double scMinVolume = 1e-6;

	Faces.Free();
	mVertices.Free();
	mBounds = BBox();

	SizedArray<DPlane> planes;
	planes.InitCapacity(Planes.Size);

	// Normalize the planes, dropping ones that aren't valid or exact duplicates
	for (uint32 plane_index = 0; plane_index < Planes.Size; plane_index++) {
		const BrushPlane& plane = Planes[plane_index];
		const DVec3 normal(plane.Normal);
		const double length = normal.Length();

		if (!(length > scMinNormalLength) || !std::isfinite(plane.Distance)) {
			continue;
		}

		const DPlane normalized { normal * (1.0 / length), plane.Distance / length, plane_index };

		bool is_duplicate = false;
		for (const DPlane& existing : planes) {
			if (existing.Normal.Dot(normalized.Normal) > 1.0 - scSameNormalEpsilon &&
				std::abs(existing.Distance - normalized.Distance) <= scOnPlaneEpsilon) {
				is_duplicate = true;
				break;
			}
		}

		if (!is_duplicate) {
			planes.Insert(normalized);
		}
	}

	// Carve the brush out of a huge box
	Polyhedron polyhedron = Polyhedron::MakeBox(scExtentTooBig);

	for (uint32 i = 0; i < planes.Size; i++) {
		if (polyhedron.Clip(planes[i], static_cast<int32>(i)) == Polyhedron::eClipResult::Empty) {
			return false;
		}
	}

	// Merge corners that ended up on top of each other, then drop the edges and faces that collapsed
	const uint32 corner_count = polyhedron.Corners.Size;

	SizedArray<uint32> merged;
	merged.InitSize(corner_count);

	for (uint32 i = 0; i < corner_count; i++) {
		merged[i] = i;

		for (uint32 j = 0; j < i; j++) {
			if (merged[j] == j && IsNearlyEqual(polyhedron.Corners[i], polyhedron.Corners[j], scDistanceToMerge)) {
				merged[i] = j;
				break;
			}
		}
	}

	PlaneList kept_planes;
	SizedArray<Face> faces;
	faces.InitCapacity(polyhedron.Faces.Size);

	SizedArray<int32> vertex_remap;
	vertex_remap.InitSize(corner_count, -1);

	SizedArray<DVec3> used_vertices;
	used_vertices.InitCapacity(corner_count);

	double volume = 0.0;

	for (const Polyhedron::Loop& loop : polyhedron.Faces) {
		if (loop.PlaneIndex == scBoundingPlane) {
			// Part of the initial box survived, so the planes do not close the solid
			return false;
		}

		SizedArray<uint32> polygon;
		polygon.InitCapacity(loop.Corners.Size);

		for (uint32 corner : loop.Corners) {
			const uint32 index = merged[corner];
			if (polygon.IsEmpty() || polygon[polygon.Size - 1] != index) {
				polygon.Insert(index);
			}
		}

		while (polygon.Size > 1 && polygon[0] == polygon[polygon.Size - 1]) {
			polygon.RemoveLast();
		}

		if (polygon.Size < 3) {
			continue;
		}

		// Simplified form of the divergence theorem shown in
		// https://www.geometrictools.com/Documentation/PolyhedralMassProperties.pdf.
		// Area vector derived via fan triangulation relative to an arbitrary origin

		const DVec3& origin = polyhedron.Corners[polygon[0]];
		DVec3 area_vector(0.0);

		for (uint32 v = 1; v + 1 < polygon.Size; v++) {
			area_vector = area_vector +
						  (polyhedron.Corners[polygon[v + 1]] - origin).Cross(polyhedron.Corners[polygon[v]] - origin);
		}

		volume += area_vector.Dot(origin) / 6.0;

		Face face;
		face.PlaneIndex = kept_planes.Size;
		face.Vertices.InitCapacity(polygon.Size);

		for (uint32 corner : polygon) {
			face.Vertices.Insert(polyhedron.Corners[corner].ToFVec3());

			if (vertex_remap[corner] < 0) {
				vertex_remap[corner] = static_cast<int32>(used_vertices.Size);
				used_vertices.Insert(polyhedron.Corners[corner]);
			}
		}

		const DPlane& plane = planes[loop.PlaneIndex];
		const BrushPlane& source_plane = Planes[plane.SourceIndex];
		kept_planes.Insert({ plane.Normal.ToFVec3(), static_cast<float32>(plane.Distance), source_plane.Texture,
							 source_plane.Material });
		faces.Insert(std::move(face));
	}

	if (faces.Size < 4 || volume < scMinVolume) {
		return false;
	}

	Planes = kept_planes;
	Faces = std::move(faces);

	mVertices.InitCapacity(used_vertices.Size);

	for (const DVec3& vertex : used_vertices) {
		mVertices.Insert(vertex.ToFVec3());
	}

	mBounds.Min = mVertices[0];
	mBounds.Max = mVertices[0];

	for (const FVec3 vertex : mVertices) {
		mBounds.Min = FVec3::Min(mBounds.Min, vertex);
		mBounds.Max = FVec3::Max(mBounds.Max, vertex);
	}

	return true;
}

bool Brush::IsBox() const
{
	if (Planes.Size != 6) {
		return false;
	}

	// Bit per axis direction (+X, -X, +Y, -Y, +Z, -Z)
	uint32 directions = 0;

	for (const BrushPlane& plane : Planes) {
		const float32 components[3] = { plane.Normal.X, plane.Normal.Y, plane.Normal.Z };

		int32 axis = -1;

		for (int32 c = 0; c < 3; c++) {
			if (std::abs(std::abs(components[c]) - 1.0f) <= scAxisAlignedEpsilon) {
				axis = c;
			}
			else if (std::abs(components[c]) > scAxisAlignedEpsilon) {
				return false;
			}
		}

		if (axis < 0) {
			return false;
		}

		int32 direction_bit = axis * 2;
		if (components[axis] < 0.0f) {
			direction_bit += 1;
		}

		directions |= 1u << direction_bit;
	}

	return directions == 0x3F;
}

bool Brush::ContainsPoint(const FVec3 point, float32 tolerance) const
{
	if (!IsValid()) {
		return false;
	}

	for (const BrushPlane& plane : Planes) {
		if (plane.Normal.Dot(point) - plane.Distance > tolerance) {
			return false;
		}
	}

	return true;
}

int32 Brush::FindPlane(const FVec3 normal) const
{
	if (normal.IsCloseTo(simd::LoadFloat4(0.0f))) {
		return scNoPlane;
	}

	const FVec3 direction = normal.Normalize();

	// Take the closest match, as neighbouring faces can be very nearly parallel
	int32 best_plane = scNoPlane;
	float32 best_dot = 1.0f - scFindPlaneEpsilon;

	for (uint32 i = 0; i < Planes.Size; i++) {
		const float32 dot = Planes[i].Normal.Dot(direction);

		if (dot > best_dot) {
			best_dot = dot;
			best_plane = static_cast<int32>(i);
		}
	}

	return best_plane;
}

float32 Brush::GetSupport(const FVec3 direction) const
{
	if (mVertices.IsEmpty()) {
		return 0.0f;
	}

	float32 support = direction.Dot(mVertices[0]);

	for (const FVec3 vertex : mVertices) {
		support = std::max(support, direction.Dot(vertex));
	}

	return support;
}

FVec3 Brush::GetFaceCenter(uint32 plane_index) const
{
	for (const Face& face : Faces) {
		if (face.PlaneIndex != plane_index) {
			continue;
		}

		FVec3 center = FVec3::sZero;
		for (const FVec3 vertex : face.Vertices) {
			center = center + vertex;
		}

		return center * (1.0f / static_cast<float32>(face.Vertices.Size));
	}

	return FVec3::sZero;
}

bool Brush::Raycast(const FVec3 origin, const FVec3 direction, float32& out_distance, uint32& out_plane_index) const
{
	// Clip the ray against each plane in turn (Cyrus-Beck), keeping the latest entry and the earliest exit
	float32 enter_distance = -std::numeric_limits<float32>::max();
	float32 exit_distance = std::numeric_limits<float32>::max();
	int32 enter_plane = scNoPlane;

	for (uint32 i = 0; i < Planes.Size; i++) {
		const BrushPlane& plane = Planes[i];

		const float32 facing = plane.Normal.Dot(direction);
		const float32 inside_distance = plane.Distance - plane.Normal.Dot(origin);

		if (std::abs(facing) < scAxisAlignedEpsilon) {
			// Parallel to the plane, so the ray is either always in front of it or always behind it
			if (inside_distance < 0.0f) {
				return false;
			}

			continue;
		}

		const float32 distance = inside_distance / facing;

		if (facing < 0.0f) {
			if (distance > enter_distance) {
				enter_distance = distance;
				enter_plane = static_cast<int32>(i);
			}
		}
		else {
			exit_distance = std::min(exit_distance, distance);
		}
	}

	if (enter_plane == scNoPlane || enter_distance < 0.0f || enter_distance > exit_distance) {
		return false;
	}

	out_distance = enter_distance;
	out_plane_index = static_cast<uint32>(enter_plane);

	return true;
}

bool Brush::Split(const FVec3 normal, float32 distance, const FVec3 origin, PlaneList& out_back,
				  PlaneList& out_front) const
{
	if (!IsValid() || Planes.Size >= scMaxPlanes) {
		return false;
	}

	PlaneList back_planes = Planes;
	back_planes.Insert({ normal, distance, GetWorldAlignedTexture(normal, origin) });

	PlaneList front_planes = Planes;
	front_planes.Insert({ -normal, -distance, GetWorldAlignedTexture(-normal, origin) });

	// Both parts have to be left with something, otherwise the plane missed the brush
	const Brush back = FromPlanes(back_planes);
	const Brush front = FromPlanes(front_planes);

	if (!back.IsValid() || !front.IsValid()) {
		return false;
	}

	out_back = back.Planes;
	out_front = front.Planes;

	return true;
}

bool Brush::Subtract(const PlaneList& cutter, const FVec3 origin, PieceList& out_pieces) const
{
	constexpr float32 scSubtractEpsilon = 1e-3f;

	out_pieces.Clear();

	if (!IsValid()) {
		return false;
	}

	PieceList pieces;
	Brush remaining = FromPlanes(Planes);

	for (const BrushPlane& cut : cutter) {
		const float32 length = cut.Normal.Length();
		if (!(length > 0.0f)) {
			continue;
		}

		const FVec3 normal = cut.Normal * (1.0f / length);
		const float32 distance = cut.Distance / length;

		if (remaining.GetSupport(normal) <= distance + scSubtractEpsilon) {
			continue;
		}

		if (-remaining.GetSupport(-normal) >= distance - scSubtractEpsilon || remaining.Planes.Size >= scMaxPlanes) {
			return false;
		}

		PlaneList inside = remaining.Planes;
		inside.Insert({ normal, distance, GetWorldAlignedTexture(normal, origin) });

		PlaneList outside = remaining.Planes;
		outside.Insert({ -normal, -distance, GetWorldAlignedTexture(-normal, origin) });

		Brush inside_brush = FromPlanes(inside);

		if (!inside_brush.IsValid()) {
			return false;
		}

		const Brush outside_brush = FromPlanes(outside);

		if (outside_brush.IsValid()) {
			pieces.Insert(outside_brush.Planes);
		}

		remaining = std::move(inside_brush);
	}

	out_pieces = std::move(pieces);

	return true;
}

void Brush::ResetFaceTexture(uint32 plane_index)
{
	BrushPlane& plane = Planes[plane_index];

	plane.Texture = BrushFaceTexture {
		.Offset = GetDefaultTextureOffset(plane.Normal, mBounds.Min, mBounds.Max),
	};
}

void Brush::AlignTexturesToWorld(const FVec3 origin)
{
	for (BrushPlane& plane : Planes) {
		plane.Texture = GetWorldAlignedTexture(plane.Normal, origin);
	}
}

void Brush::AlignFaceTextureToWorld(uint32 plane_index, const FVec3 origin)
{
	Planes[plane_index].Texture = GetWorldAlignedTexture(Planes[plane_index].Normal, origin);
}

bool Brush::HasDefaultTextures() const
{
	// Offsets closer than this to the default layout count as the default
	constexpr float32 scTextureOffsetEpsilon = 1e-4f;

	static const BrushFaceTexture scDefaultTexture {};

	for (const BrushPlane& plane : Planes) {
		const BrushFaceTexture& texture = plane.Texture;
		const FVec2 default_offset = GetDefaultTextureOffset(plane.Normal, mBounds.Min, mBounds.Max);

		if (texture.Scale.X != scDefaultTexture.Scale.X || texture.Scale.Y != scDefaultTexture.Scale.Y ||
			texture.Rotation != 0.0f || std::abs(texture.Offset.X - default_offset.X) > scTextureOffsetEpsilon ||
			std::abs(texture.Offset.Y - default_offset.Y) > scTextureOffsetEpsilon) {
			return false;
		}
	}

	return true;
}

bool Brush::HasFaceMaterials() const
{
	for (const BrushPlane& plane : Planes) {
		if (!plane.Material.IsEmpty()) {
			return true;
		}
	}

	return false;
}

void Brush::GetFaceMaterials(StackArray<String, scMaxPlanes>& out_materials) const
{
	out_materials.Clear();

	for (const BrushPlane& plane : Planes) {
		if (plane.Material.IsEmpty()) {
			continue;
		}

		bool is_listed = false;

		for (const String& listed : out_materials) {
			if (listed == plane.Material) {
				is_listed = true;
				break;
			}
		}

		if (!is_listed) {
			out_materials.Insert(plane.Material);
		}
	}
}

void Brush::GetTextureGradients(const BrushPlane& plane, FVec3& out_u_gradient, FVec3& out_v_gradient)
{
	const BrushFaceTexture& texture = plane.Texture;
	const TextureProjection projection = GetTextureProjection(plane.Normal);

	float32 sine = 0.0f;
	float32 cosine = 1.0f;
	GetTextureRotation(texture, sine, cosine);

	out_u_gradient = (projection.UAxis * cosine - projection.VAxis * sine) * (1.0f / texture.Scale.X);
	out_v_gradient = (projection.UAxis * sine + projection.VAxis * cosine) * (1.0f / texture.Scale.Y);
}

void Brush::GenerateMesh(SizedArray<FVec3>& positions, SizedArray<FVec3>& normals, SizedArray<FVec3>& tangents,
						 SizedArray<float32>& tangent_handedness, SizedArray<FVec2>& texcoords,
						 SizedArray<uint32>& indices, const String* only_material) const
{
	if (!IsValid()) {
		return;
	}

	const auto is_included = [&](const Face& face)
	{ return only_material == nullptr || Planes[face.PlaneIndex].Material == *only_material; };

	size_t vertex_count = 0;
	size_t index_count = 0;

	for (const Face& face : Faces) {
		if (!is_included(face)) {
			continue;
		}

		vertex_count += face.Vertices.Size;
		index_count += (face.Vertices.Size - 2) * 3;
	}

	positions.InitCapacity(vertex_count);
	normals.InitCapacity(vertex_count);
	tangents.InitCapacity(vertex_count);
	tangent_handedness.InitCapacity(vertex_count);
	texcoords.InitCapacity(vertex_count);
	indices.InitCapacity(index_count);

	for (uint32 face_index = 0; face_index < Faces.Size; face_index++) {
		const Face& face = Faces[face_index];

		if (!is_included(face)) {
			continue;
		}

		const BrushPlane& plane = Planes[face.PlaneIndex];
		const BrushFaceTexture& texture = plane.Texture;

		const FVec3 normal = plane.Normal;
		const TextureProjection projection = GetTextureProjection(normal);

		float32 sine = 0.0f;
		float32 cosine = 1.0f;
		GetTextureRotation(texture, sine, cosine);

		// How fast U and V change moving across the face. Only the parts along the face matter.
		FVec3 u_gradient;
		FVec3 v_gradient;
		GetTextureGradients(plane, u_gradient, v_gradient);

		u_gradient = u_gradient - normal * normal.Dot(u_gradient);
		v_gradient = v_gradient - normal * normal.Dot(v_gradient);

		// The tangent is the direction U increases in while V stays the same, which is only along the U gradient when
		// the texture isn't skewed by the projection or an uneven scale
		const float32 uu = u_gradient.Dot(u_gradient);
		const float32 uv = u_gradient.Dot(v_gradient);
		const float32 vv = v_gradient.Dot(v_gradient);

		FVec3 tangent = u_gradient * vv - v_gradient * uv;
		tangent = tangent * (1.0f / (uu * vv - uv * uv));
		tangent.NormalizeIP();

		FVec3 v_direction = v_gradient * uu - u_gradient * uv;
		v_direction = v_direction * (1.0f / (uu * vv - uv * uv));

		float32 handedness = 1.0f;
		if (normal.Cross(tangent).Dot(v_direction) > 0.0f) {
			handedness = -1.0f;
		}

		const uint32 base = static_cast<uint32>(positions.Size);

		for (const FVec3 vertex : face.Vertices) {
			const float32 u = vertex.Dot(projection.UAxis);
			const float32 v = vertex.Dot(projection.VAxis);

			const FVec2 new_uv((u * cosine - v * sine) / (texture.Scale.X) + texture.Offset.X,
							   (u * sine + v * cosine) / (texture.Scale.Y) + texture.Offset.Y);

			positions.Insert(vertex);
			normals.Insert(normal);
			tangents.Insert(tangent);
			tangent_handedness.Insert(handedness);
			texcoords.Insert(new_uv);
		}

		for (uint32 v = 1; v + 1 < face.Vertices.Size; v++) {
			indices.Insert(base);
			indices.Insert(base + v);
			indices.Insert(base + v + 1);
		}
	}
}

} // namespace fx
