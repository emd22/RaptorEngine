#include "Brush.hpp"

#include <Util/RustInterop.hpp>
#include <raptor_ffi.h>
#include <vector>

namespace fx {

namespace {

RxBrushPlane ToRust(const BrushPlane& plane)
{
	return RxBrushPlane { .normal = { plane.Normal.X, plane.Normal.Y, plane.Normal.Z },
						  .distance = plane.Distance,
						  .offset = { plane.Texture.Offset.X, plane.Texture.Offset.Y },
						  .scale = { plane.Texture.Scale.X, plane.Texture.Scale.Y },
						  .rotation = plane.Texture.Rotation };
}

BrushPlane FromRust(const RxBrushPlane& plane)
{
	BrushPlane out;
	out.Normal = Vec3f(plane.normal[0], plane.normal[1], plane.normal[2]);
	out.Distance = plane.distance;
	out.Texture.Offset = Vec2f(plane.offset[0], plane.offset[1]);
	out.Texture.Scale = Vec2f(plane.scale[0], plane.scale[1]);
	out.Texture.Rotation = plane.rotation;
	return out;
}

Brush::PlaneList PlanesFromRust(const RxBrushPlane* planes, size_t count)
{
	Brush::PlaneList list;

	for (size_t i = 0; i < count; i++) {
		list.Insert(FromRust(planes[i]));
	}

	return list;
}

struct BrushMarshal
{
	explicit BrushMarshal(const Brush& brush)
	{
		for (const BrushPlane& plane : brush.Planes) {
			Planes.push_back(ToRust(plane));
		}

		FaceVertices.reserve(brush.Faces.Size);

		for (const Brush::Face& face : brush.Faces) {
			std::vector<float>& packed = FaceVertices.emplace_back();

			for (const Vec3f& vertex : face.Vertices) {
				packed.insert(packed.end(), { vertex.X, vertex.Y, vertex.Z });
			}

			Faces.push_back(RxBrushFace { .plane_index = face.PlaneIndex,
										  .vertices = packed.data(),
										  .vertex_count = face.Vertices.Size });
		}

		for (const Vec3f& vertex : brush.GetVertices()) {
			Vertices.insert(Vertices.end(), { vertex.X, vertex.Y, vertex.Z });
		}

		const Vec3f bounds_min = brush.GetBoundsMin();
		const Vec3f bounds_max = brush.GetBoundsMax();

		View = RxBrushView { .planes = Planes.data(),
							 .plane_count = Planes.size(),
							 .faces = Faces.data(),
							 .face_count = Faces.size(),
							 .vertices = Vertices.data(),
							 .vertex_count = Vertices.size() / 3,
							 .bounds_min = { bounds_min.X, bounds_min.Y, bounds_min.Z },
							 .bounds_max = { bounds_max.X, bounds_max.Y, bounds_max.Z } };
	}

	std::vector<RxBrushPlane> Planes;
	std::vector<std::vector<float>> FaceVertices;
	std::vector<RxBrushFace> Faces;
	std::vector<float> Vertices;
	RxBrushView View = {};
};

} // namespace


Brush Brush::FromBox(const Vec3f& min, const Vec3f& max)
{
	const float min_values[3] = { min.X, min.Y, min.Z };
	const float max_values[3] = { max.X, max.Y, max.Z };

	RxBrushResult* result = rx_brush_from_box(min_values, max_values);

	Brush brush;
	brush.Apply(*rx_brush_result_view(result), true);

	rx_brush_result_free(result);

	return brush;
}

Brush Brush::FromPlanes(const PlaneList& planes)
{
	Brush brush;
	brush.Planes = planes;
	brush.Rebuild();

	return brush;
}

void Brush::Apply(const RxBrushView& view, bool replace_planes)
{
	Faces.Free();
	mVertices.Free();

	if (replace_planes) {
		Planes = PlanesFromRust(view.planes, view.plane_count);
	}

	Faces.InitCapacity(view.face_count);

	for (size_t i = 0; i < view.face_count; i++) {
		Face face;
		face.PlaneIndex = view.faces[i].plane_index;
		RustInterop::ReadVec3s(face.Vertices, view.faces[i].vertices, view.faces[i].vertex_count);

		Faces.Insert(std::move(face));
	}

	RustInterop::ReadVec3s(mVertices, view.vertices, view.vertex_count);

	mBoundsMin = Vec3f(view.bounds_min[0], view.bounds_min[1], view.bounds_min[2]);
	mBoundsMax = Vec3f(view.bounds_max[0], view.bounds_max[1], view.bounds_max[2]);
}

bool Brush::Rebuild()
{
	std::vector<RxBrushPlane> planes;
	for (const BrushPlane& plane : Planes) {
		planes.push_back(ToRust(plane));
	}

	RxBrushResult* result = rx_brush_rebuild(planes.data(), planes.size());

	const bool ok = rx_brush_result_ok(result) != 0;
	Apply(*rx_brush_result_view(result), ok);

	rx_brush_result_free(result);

	return ok;
}

bool Brush::IsBox() const
{
	const BrushMarshal marshal(*this);
	return rx_brush_is_box(&marshal.View) != 0;
}

bool Brush::ContainsPoint(const Vec3f& point, float32 tolerance) const
{
	const BrushMarshal marshal(*this);
	const float values[3] = { point.X, point.Y, point.Z };

	return rx_brush_contains_point(&marshal.View, values, tolerance) != 0;
}

int32 Brush::FindPlane(const Vec3f& normal) const
{
	const BrushMarshal marshal(*this);
	const float values[3] = { normal.X, normal.Y, normal.Z };

	return rx_brush_find_plane(&marshal.View, values);
}

float32 Brush::GetSupport(const Vec3f& direction) const
{
	const BrushMarshal marshal(*this);
	const float values[3] = { direction.X, direction.Y, direction.Z };

	return rx_brush_support(&marshal.View, values);
}

Vec3f Brush::GetFaceCenter(uint32 plane_index) const
{
	const BrushMarshal marshal(*this);

	float center[3] = {};
	rx_brush_face_center(&marshal.View, plane_index, center);

	return Vec3f(center[0], center[1], center[2]);
}

bool Brush::Raycast(const Vec3f& origin, const Vec3f& direction, float32& out_distance, uint32& out_plane_index) const
{
	const BrushMarshal marshal(*this);
	const float origin_values[3] = { origin.X, origin.Y, origin.Z };
	const float direction_values[3] = { direction.X, direction.Y, direction.Z };

	float distance = 0.0f;
	uint32 plane_index = 0;

	if (!rx_brush_raycast(&marshal.View, origin_values, direction_values, &distance, &plane_index)) {
		return false;
	}

	out_distance = distance;
	out_plane_index = plane_index;

	return true;
}

bool Brush::Split(const Vec3f& normal, float32 distance, const Vec3f& origin, PlaneList& out_back,
				  PlaneList& out_front) const
{
	const BrushMarshal marshal(*this);
	const float normal_values[3] = { normal.X, normal.Y, normal.Z };
	const float origin_values[3] = { origin.X, origin.Y, origin.Z };

	RxBrushSplit* split = rx_brush_split(&marshal.View, normal_values, distance, origin_values);

	if (split == nullptr) {
		return false;
	}

	size_t count = 0;
	const RxBrushPlane* back = rx_brush_split_planes(split, 0, &count);
	out_back = PlanesFromRust(back, count);

	const RxBrushPlane* front = rx_brush_split_planes(split, 1, &count);
	out_front = PlanesFromRust(front, count);

	rx_brush_split_free(split);

	return true;
}

void Brush::ResetFaceTexture(uint32 plane_index)
{
	const BrushMarshal marshal(*this);

	float offset[2] = {};
	rx_brush_default_texture_offset(&marshal.View, plane_index, offset);

	Planes[plane_index].Texture = BrushFaceTexture { .Offset = Vec2f(offset[0], offset[1]) };
}

void Brush::AlignTexturesToWorld(const Vec3f& origin)
{
	const float origin_values[3] = { origin.X, origin.Y, origin.Z };

	for (BrushPlane& plane : Planes) {
		const float normal_values[3] = { plane.Normal.X, plane.Normal.Y, plane.Normal.Z };

		float offset[2] = {};
		rx_brush_world_aligned_offset(normal_values, origin_values, offset);

		plane.Texture = BrushFaceTexture { .Offset = Vec2f(offset[0], offset[1]) };
	}
}

bool Brush::HasDefaultTextures() const
{
	const BrushMarshal marshal(*this);
	return rx_brush_has_default_textures(&marshal.View) != 0;
}

void Brush::GenerateMesh(SizedArray<Vec3f>& positions, SizedArray<Vec3f>& normals, SizedArray<Vec3f>& tangents,
						 SizedArray<Vec2f>& texcoords, SizedArray<uint32>& indices) const
{
	if (!IsValid()) {
		return;
	}

	const BrushMarshal marshal(*this);

	RxMesh* mesh = rx_brush_generate_mesh(&marshal.View);

	if (mesh == nullptr) {
		LogError(LC_ASSET, "Brush mesh generation failed");
		return;
	}

	RustInterop::ReadMesh(mesh, positions, normals, tangents, texcoords, indices);

	rx_mesh_free(mesh);
}

} // namespace fx
