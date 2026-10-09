#pragma once

#include <Core/AnonArray.hpp>
#include <Core/SizedArray.hpp>
#include <Math/BBox.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/VertexList.hpp>
#include <cfloat>
#include <cmath>

namespace fx {

class MeshUtil
{
public:
	static BBox CalculateBounds(const renderer::VertexList& vertex_list)
	{
		const AnonArray& vertices = vertex_list.GetLocalBuffer();

		if (vertices.IsEmpty()) {
			LogWarning(LC_ASSET, "Cannot calculate dimensions as there are no vertices!");
			return BBox {};
		}

		Vec3f min_vertex = Vec3f(FLT_MAX);
		Vec3f max_vertex = Vec3f(-FLT_MAX);

		for (uint32 i = 0; i < vertices.Size; i++) {
			// Since the position resides at the same location for each vertex type (see static_assert in
			// VertexUtil::GetPosition), the vertex type used here can be anything, and offsets are preserved as the
			// size of the object is stored in the anonymous buffer.
			Vec3f position = renderer::VertexUtil::GetPosition(
				*static_cast<const renderer::Vertex<renderer::eVertexType::Default>*>(vertices.GetRaw(i)));

			min_vertex = Vec3f::Min(min_vertex, position);
			max_vertex = Vec3f::Max(max_vertex, position);
		}

		return BBox { min_vertex, max_vertex };
	}

	template <typename TElementType>
	static void ExpandByIndices(SizedArray<TElementType>& array, uint32 components, const SizedArray<uint32>& indices)
	{
		if (array.IsEmpty()) {
			return;
		}

		SizedArray<TElementType> expanded;
		expanded.InitSize(static_cast<size_t>(indices.Size) * components);

		for (uint32 i = 0; i < indices.Size; i++) {
			for (uint32 c = 0; c < components; c++) {
				expanded[static_cast<size_t>(i) * components + c] =
					array[static_cast<size_t>(indices[i]) * components + c];
			}
		}

		array = std::move(expanded);
	}

	static void GenerateFlatNormals(const SizedArray<float32>& positions, SizedArray<float32>& out_normals)
	{
		const uint32 vertex_count = static_cast<uint32>(positions.Size / 3);

		out_normals.InitSize(static_cast<size_t>(vertex_count) * 3);

		for (uint32 vertex = 0; vertex + 2 < vertex_count; vertex += 3) {
			const Vec3f a(positions[vertex * 3], positions[vertex * 3 + 1], positions[vertex * 3 + 2]);
			const Vec3f b(positions[vertex * 3 + 3], positions[vertex * 3 + 4], positions[vertex * 3 + 5]);
			const Vec3f c(positions[vertex * 3 + 6], positions[vertex * 3 + 7], positions[vertex * 3 + 8]);

			Vec3f normal = (b - a).Cross(c - a);

			if (normal.Length() < 1e-20f) {
				normal = Vec3f(0.0f, 1.0f, 0.0f);
			}
			else {
				normal.NormalizeIP();
			}

			for (uint32 corner = 0; corner < 3; corner++) {
				out_normals[(vertex + corner) * 3] = normal.X;
				out_normals[(vertex + corner) * 3 + 1] = normal.Y;
				out_normals[(vertex + corner) * 3 + 2] = normal.Z;
			}
		}
	}

	static void GenerateTangents(const SizedArray<float32>& positions, const SizedArray<float32>& normals,
								 const SizedArray<float32>& uvs, const SizedArray<uint32>& indices,
								 SizedArray<float32>& out_tangents)
	{
		const uint32 vertex_count = static_cast<uint32>(positions.Size / 3);

		SizedArray<Vec3f> tangent_sum;
		tangent_sum.InitSize(vertex_count);

		SizedArray<Vec3f> bitangent_sum;
		bitangent_sum.InitSize(vertex_count);

		for (uint32 i = 0; i < vertex_count; i++) {
			tangent_sum[i] = Vec3f::sZero;
			bitangent_sum[i] = Vec3f::sZero;
		}

		auto position_at = [&](uint32 i)
		{ return Vec3f(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]); };

		for (uint32 corner = 0; corner + 2 < indices.Size; corner += 3) {
			const uint32 i0 = indices[corner];
			const uint32 i1 = indices[corner + 1];
			const uint32 i2 = indices[corner + 2];

			if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
				continue;
			}

			const Vec3f edge1 = position_at(i1) - position_at(i0);
			const Vec3f edge2 = position_at(i2) - position_at(i0);

			const float32 du1 = uvs[i1 * 2] - uvs[i0 * 2];
			const float32 dv1 = uvs[i1 * 2 + 1] - uvs[i0 * 2 + 1];
			const float32 du2 = uvs[i2 * 2] - uvs[i0 * 2];
			const float32 dv2 = uvs[i2 * 2 + 1] - uvs[i0 * 2 + 1];

			const float32 determinant = du1 * dv2 - du2 * dv1;

			if (std::abs(determinant) < 1e-18f) {
				continue;
			}

			const float32 inverse = 1.0f / determinant;

			const Vec3f tangent = (edge1 * dv2 - edge2 * dv1) * inverse;
			const Vec3f bitangent = (edge2 * du1 - edge1 * du2) * inverse;

			for (const uint32 index : { i0, i1, i2 }) {
				tangent_sum[index] = tangent_sum[index] + tangent;
				bitangent_sum[index] = bitangent_sum[index] + bitangent;
			}
		}

		out_tangents.InitSize(static_cast<size_t>(vertex_count) * 4);

		for (uint32 i = 0; i < vertex_count; i++) {
			const Vec3f normal(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);

			Vec3f tangent = tangent_sum[i] - normal * normal.Dot(tangent_sum[i]);

			if (tangent.Length() < 1e-12f) {
				const Vec3f axis = (std::abs(normal.X) < 0.9f) ? Vec3f(1.0f, 0.0f, 0.0f) : Vec3f(0.0f, 1.0f, 0.0f);
				tangent = axis - normal * normal.Dot(axis);
			}

			tangent.NormalizeIP();

			const Vec3f image_up = Vec3f::sZero - bitangent_sum[i];
			const float32 handedness = (normal.Cross(tangent).Dot(image_up) < 0.0f) ? -1.0f : 1.0f;

			out_tangents[i * 4] = tangent.X;
			out_tangents[i * 4 + 1] = tangent.Y;
			out_tangents[i * 4 + 2] = tangent.Z;
			out_tangents[i * 4 + 3] = handedness;
		}
	}
};

} // namespace fx
