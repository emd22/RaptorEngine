#include "Brush.hpp"

#include <Core/DynArray.hpp>
#include <Math/MathUtil.hpp>
#include <Math/Vec3d.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

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
	Vec3d Normal;
	double Distance = 0.0;

	/// Index of the brush plane this came from, to carry its texture over
	uint32 SourceIndex = 0;

	double SignedDistance(const Vec3d& point) const { return Normal.Dot(point) - Distance; }
};

/// Corners closer than this to a plane are treated as lying on it
constexpr double scOnPlaneEpsilon = 1e-5;

constexpr double scDistanceToMerge = 1e-6;
constexpr double scExtentTooBig = 1e5;

constexpr double scMinVolume = 1e-6;

constexpr double scMinNormalLength = 1e-8;
constexpr double scSameNormalEpsilon = 1e-9;

/// How far a normal component can be from 0 or 1 and still count as axis aligned
constexpr float32 scAxisAlignedEpsilon = 1e-6f;

/// How close to 1 the dot product of two normals has to be for FindPlane() to match them
constexpr float32 scFindPlaneEpsilon = 1e-3f;

/// Offsets closer than this to the default layout count as the default
constexpr float32 scTextureOffsetEpsilon = 1e-4f;

/// Plane index given to the faces of the initial box.
constexpr int32 scBoundingPlane = -1;

struct TextureProjection
{
	Vec3f UAxis;
	Vec3f VAxis;
};

TextureProjection GetTextureProjection(const Vec3f& normal)
{
	const float32 abs_x = std::abs(normal.X);
	const float32 abs_y = std::abs(normal.Y);
	const float32 abs_z = std::abs(normal.Z);

	if (abs_y >= abs_x && abs_y >= abs_z) {
		return { Vec3f(1.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.0f) };
	}

	if (abs_x >= abs_z) {
		return { Vec3f(0.0f, 0.0f, 1.0f), Vec3f(0.0f, -1.0f, 0.0f) };
	}

	return { Vec3f(1.0f, 0.0f, 0.0f), Vec3f(0.0f, -1.0f, 0.0f) };
}

Vec2f GetDefaultTextureOffset(const Vec3f& normal, const Vec3f& bounds_min, const Vec3f& bounds_max)
{
	const TextureProjection projection = GetTextureProjection(normal);

	// V runs down walls, so the texture starts at the top of the brush
	const Vec3f anchor(bounds_min.X, bounds_max.Y, bounds_min.Z);

	return Vec2f(-anchor.Dot(projection.UAxis), -anchor.Dot(projection.VAxis));
}

bool IsNearlyEqual(const Vec3d& a, const Vec3d& b, double epsilon)
{
	return std::abs(a.X - b.X) <= epsilon && std::abs(a.Y - b.Y) <= epsilon && std::abs(a.Z - b.Z) <= epsilon;
}

/// Packs a directed edge between two corners into a single key.
uint64 MakeEdgeKey(uint32 from, uint32 to) { return (static_cast<uint64>(from) << 32) | to; }

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
	DoublingArray<Vec3d> Corners;
	DoublingArray<Loop> Faces;
};

Polyhedron Polyhedron::MakeBox(double half_extent)
{
	Polyhedron box;

	// A corner takes its sign on each axis from bits 0, 1 and 2
	for (uint32 corner = 0; corner < 8; corner++) {
		box.Corners.Insert(Vec3d((corner & 1) ? half_extent : -half_extent, (corner & 2) ? half_extent : -half_extent,
								 (corner & 4) ? half_extent : -half_extent));
	}

	const uint32 faces[6][4] = {
		{ 1, 3, 7, 5 }, { 0, 4, 6, 2 }, { 2, 6, 7, 3 }, { 0, 1, 5, 4 }, { 4, 5, 7, 6 }, { 0, 2, 3, 1 },
	};

	for (const auto& face : faces) {
		Loop loop;
		for (uint32 corner : face) {
			loop.Corners.Insert(corner);
		}

		const Vec3d& a = box.Corners[loop.Corners[0]];
		const Vec3d normal = (box.Corners[loop.Corners[1]] - a).Cross(box.Corners[loop.Corners[2]] - a);

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
	in_use.InitSize(corner_count);
	for (bool& used : in_use) {
		used = false;
	}

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
	std::unordered_map<uint64, uint32> split_corners;

	auto GetSplitCorner = [&](uint32 a, uint32 b)
	{
		// The same edge is walked in opposite directions by its two faces
		const uint64 key = MakeEdgeKey(std::min(a, b), std::max(a, b));

		auto it = split_corners.find(key);
		if (it != split_corners.end()) {
			return it->second;
		}

		const double t = distances[a] / (distances[a] - distances[b]);
		Corners.Insert(Corners[a] + (Corners[b] - Corners[a]) * t);

		const uint32 index = Corners.Size - 1;
		split_corners.emplace(key, index);

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

	std::unordered_set<uint64> edges;

	for (const Loop& face : clipped_faces) {
		for (uint32 i = 0; i < face.Corners.Size; i++) {
			edges.insert(MakeEdgeKey(face.Corners[i], face.Corners[(i + 1) % face.Corners.Size]));
		}
	}

	std::unordered_map<uint32, uint32> cap_next;

	for (const Loop& face : clipped_faces) {
		for (uint32 i = 0; i < face.Corners.Size; i++) {
			const uint32 a = face.Corners[i];
			const uint32 b = face.Corners[(i + 1) % face.Corners.Size];

			if (IsOnPlane(a) && IsOnPlane(b) && !edges.contains(MakeEdgeKey(b, a))) {
				cap_next[b] = a;
			}
		}
	}

	if (cap_next.size() < 3) {
		return eClipResult::Empty;
	}

	Loop cap;
	cap.PlaneIndex = plane_index;

	const uint32 start = cap_next.begin()->first;
	uint32 corner = start;

	do {
		cap.Corners.Insert(corner);

		auto it = cap_next.find(corner);
		if (it == cap_next.end() || cap.Corners.Size > cap_next.size()) {
			// The boundary is not a single loop, which only happens with degenerate input
			return eClipResult::Empty;
		}

		corner = it->second;
	} while (corner != start);

	if (cap.Corners.Size != cap_next.size()) {
		return eClipResult::Empty;
	}

	clipped_faces.Emplace(std::move(cap));
	Faces = std::move(clipped_faces);

	return eClipResult::Clipped;
}

} // namespace


Brush Brush::FromBox(const Vec3f& min, const Vec3f& max)
{
	Brush brush;

	brush.Planes.Insert({ Vec3f(1.0f, 0.0f, 0.0f), max.X });
	brush.Planes.Insert({ Vec3f(-1.0f, 0.0f, 0.0f), -min.X });
	brush.Planes.Insert({ Vec3f(0.0f, 1.0f, 0.0f), max.Y });
	brush.Planes.Insert({ Vec3f(0.0f, -1.0f, 0.0f), -min.Y });
	brush.Planes.Insert({ Vec3f(0.0f, 0.0f, 1.0f), max.Z });
	brush.Planes.Insert({ Vec3f(0.0f, 0.0f, -1.0f), -min.Z });

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

bool Brush::Rebuild()
{
	Faces.Free();
	mVertices.Free();
	mBoundsMin = Vec3f::sZero;
	mBoundsMax = Vec3f::sZero;

	SizedArray<DPlane> planes;
	planes.InitCapacity(Planes.Size);

	// Normalize the planes, dropping ones that aren't valid or exact duplicates
	for (uint32 plane_index = 0; plane_index < Planes.Size; plane_index++) {
		const BrushPlane& plane = Planes[plane_index];
		const Vec3d normal(plane.Normal);
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
	vertex_remap.InitSize(corner_count);
	for (int32& remap : vertex_remap) {
		remap = -1;
	}

	SizedArray<Vec3d> used_vertices;
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

		const Vec3d& origin = polyhedron.Corners[polygon[0]];
		Vec3d area_vector(0.0);

		for (uint32 v = 1; v + 1 < polygon.Size; v++) {
			area_vector = area_vector +
						  (polyhedron.Corners[polygon[v + 1]] - origin).Cross(polyhedron.Corners[polygon[v]] - origin);
		}

		volume += area_vector.Dot(origin) / 6.0;

		Face face;
		face.PlaneIndex = kept_planes.Size;
		face.Vertices.InitCapacity(polygon.Size);

		for (uint32 corner : polygon) {
			face.Vertices.Insert(polyhedron.Corners[corner].ToVec3f());

			if (vertex_remap[corner] < 0) {
				vertex_remap[corner] = static_cast<int32>(used_vertices.Size);
				used_vertices.Insert(polyhedron.Corners[corner]);
			}
		}

		const DPlane& plane = planes[loop.PlaneIndex];
		kept_planes.Insert(
			{ plane.Normal.ToVec3f(), static_cast<float32>(plane.Distance), Planes[plane.SourceIndex].Texture });
		faces.Insert(std::move(face));
	}

	if (faces.Size < 4 || volume < scMinVolume) {
		return false;
	}

	Planes = kept_planes;
	Faces = std::move(faces);

	mVertices.InitCapacity(used_vertices.Size);

	for (const Vec3d& vertex : used_vertices) {
		mVertices.Insert(vertex.ToVec3f());
	}

	mBoundsMin = mVertices[0];
	mBoundsMax = mVertices[0];

	for (const Vec3f& vertex : mVertices) {
		mBoundsMin = Vec3f::Min(mBoundsMin, vertex);
		mBoundsMax = Vec3f::Max(mBoundsMax, vertex);
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

		directions |= 1u << (axis * 2 + (components[axis] < 0.0f ? 1 : 0));
	}

	return directions == 0x3F;
}

bool Brush::ContainsPoint(const Vec3f& point, float32 tolerance) const
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

int32 Brush::FindPlane(const Vec3f& normal) const
{
	if (normal.IsCloseTo(simd::LoadFloat4(0.0f))) {
		return scNoPlane;
	}

	const Vec3f direction = normal.Normalize();

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

float32 Brush::GetSupport(const Vec3f& direction) const
{
	if (mVertices.IsEmpty()) {
		return 0.0f;
	}

	float32 support = direction.Dot(mVertices[0]);

	for (const Vec3f& vertex : mVertices) {
		support = std::max(support, direction.Dot(vertex));
	}

	return support;
}

Vec3f Brush::GetFaceCenter(uint32 plane_index) const
{
	for (const Face& face : Faces) {
		if (face.PlaneIndex != plane_index) {
			continue;
		}

		Vec3f center = Vec3f::sZero;
		for (const Vec3f& vertex : face.Vertices) {
			center = center + vertex;
		}

		return center * (1.0f / static_cast<float32>(face.Vertices.Size));
	}

	return Vec3f::sZero;
}

bool Brush::Raycast(const Vec3f& origin, const Vec3f& direction, float32& out_distance, uint32& out_plane_index) const
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

void Brush::ResetFaceTexture(uint32 plane_index)
{
	BrushPlane& plane = Planes[plane_index];

	const MaterialID material = plane.Texture.Material;

	plane.Texture = BrushFaceTexture {
		.Material = material,
		.Offset = GetDefaultTextureOffset(plane.Normal, mBoundsMin, mBoundsMax),
	};
}

bool Brush::HasDefaultTextures() const
{
	for (const BrushPlane& plane : Planes) {
		const BrushFaceTexture& texture = plane.Texture;
		const Vec2f default_offset = GetDefaultTextureOffset(plane.Normal, mBoundsMin, mBoundsMax);

		if (!texture.Material.IsNull() || texture.Scale.X != 1.0f || texture.Scale.Y != 1.0f ||
			texture.Rotation != 0.0f || std::abs(texture.Offset.X - default_offset.X) > scTextureOffsetEpsilon ||
			std::abs(texture.Offset.Y - default_offset.Y) > scTextureOffsetEpsilon) {
			return false;
		}
	}

	return true;
}

void Brush::GenerateMesh(SizedArray<Vec3f>& positions, SizedArray<Vec3f>& normals, SizedArray<Vec3f>& tangents,
						 SizedArray<Vec2f>& texcoords, SizedArray<uint32>& indices,
						 SizedArray<MeshSection>& sections) const
{
	if (!IsValid()) {
		return;
	}

	size_t vertex_count = 0;
	size_t index_count = 0;

	for (const Face& face : Faces) {
		vertex_count += face.Vertices.Size;
		index_count += (face.Vertices.Size - 2) * 3;
	}

	positions.InitCapacity(vertex_count);
	normals.InitCapacity(vertex_count);
	tangents.InitCapacity(vertex_count);
	texcoords.InitCapacity(vertex_count);
	indices.InitCapacity(index_count);

	// Faces are emitted grouped by material, so each material is drawn as one range of indices
	SizedArray<uint32> face_order;
	face_order.InitCapacity(Faces.Size);

	for (uint32 i = 0; i < Faces.Size; i++) {
		face_order.Insert(i);
	}

	auto GetFaceMaterial = [&](uint32 face_index) { return Planes[Faces[face_index].PlaneIndex].Texture.Material; };

	std::stable_sort(face_order.begin(), face_order.end(),
					 [&](uint32 a, uint32 b) { return GetFaceMaterial(a).GetID() < GetFaceMaterial(b).GetID(); });

	const bool has_face_materials = std::any_of(face_order.begin(), face_order.end(), [&](uint32 face_index)
												{ return !GetFaceMaterial(face_index).IsNull(); });

	if (has_face_materials) {
		sections.InitCapacity(Faces.Size);
	}

	for (uint32 face_index : face_order) {
		const Face& face = Faces[face_index];
		const BrushPlane& plane = Planes[face.PlaneIndex];
		const BrushFaceTexture& texture = plane.Texture;

		const Vec3f normal = plane.Normal;
		const TextureProjection projection = GetTextureProjection(normal);

		float32 sine = 0.0f;
		float32 cosine = 1.0f;

		if (texture.Rotation != 0.0f) {
			const float32 radians = MathUtil::DegreesToRadians(texture.Rotation);
			sine = std::sin(radians);
			cosine = std::cos(radians);
		}

		// How fast U and V change moving across the face. Only the parts along the face matter.
		Vec3f u_gradient = (projection.UAxis * cosine - projection.VAxis * sine) * (1.0f / texture.Scale.X);
		Vec3f v_gradient = (projection.UAxis * sine + projection.VAxis * cosine) * (1.0f / texture.Scale.Y);

		u_gradient = u_gradient - normal * normal.Dot(u_gradient);
		v_gradient = v_gradient - normal * normal.Dot(v_gradient);

		// The tangent is the direction U increases in while V stays the same, which is only along the U gradient when
		// the texture isn't skewed by the projection or an uneven scale
		const float32 uu = u_gradient.Dot(u_gradient);
		const float32 uv = u_gradient.Dot(v_gradient);
		const float32 vv = v_gradient.Dot(v_gradient);

		Vec3f tangent = u_gradient * vv - v_gradient * uv;
		tangent = tangent * (1.0f / (uu * vv - uv * uv));
		tangent.NormalizeIP();

		const uint32 first_index = static_cast<uint32>(indices.Size);
		const uint32 base = static_cast<uint32>(positions.Size);

		for (const Vec3f& vertex : face.Vertices) {
			const float32 u = vertex.Dot(projection.UAxis);
			const float32 v = vertex.Dot(projection.VAxis);

			const Vec2f uv((u * cosine - v * sine) / texture.Scale.X + texture.Offset.X,
						   (u * sine + v * cosine) / texture.Scale.Y + texture.Offset.Y);

			positions.Insert(vertex);
			normals.Insert(normal);
			tangents.Insert(tangent);
			texcoords.Insert(uv);
		}

		for (uint32 v = 1; v + 1 < face.Vertices.Size; v++) {
			indices.Insert(base);
			indices.Insert(base + v);
			indices.Insert(base + v + 1);
		}

		if (!has_face_materials) {
			continue;
		}

		// Extend the current section while the material stays the same
		const uint32 face_index_count = static_cast<uint32>(indices.Size) - first_index;

		if (sections.IsNotEmpty() && sections[sections.Size - 1].Material == texture.Material) {
			sections[sections.Size - 1].IndexCount += face_index_count;
		}
		else {
			sections.Insert(MeshSection {
				.FirstIndex = first_index,
				.IndexCount = face_index_count,
				.Material = texture.Material,
			});
		}
	}
}

} // namespace fx
