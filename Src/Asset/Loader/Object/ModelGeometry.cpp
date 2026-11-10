#include "ModelGeometry.hpp"

#include "RxModelConfig.hpp"

#include <Core/FilesystemIO.hpp>
#include <Math/Float16.hpp>
#include <Math/Mat4.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

namespace fx::loader {

void TransformModelGeometry(SizedArray<float32>& positions, SizedArray<float32>& normals, SizedArray<float32>& tangents,
							SizedArray<uint32>& indices, const float32* m)
{
	const float32 a00 = m[0];
	const float32 a10 = m[1];
	const float32 a20 = m[2];
	const float32 a01 = m[4];
	const float32 a11 = m[5];
	const float32 a21 = m[6];
	const float32 a02 = m[8];
	const float32 a12 = m[9];
	const float32 a22 = m[10];

	const float32 c00 = a11 * a22 - a12 * a21;
	const float32 c01 = a12 * a20 - a10 * a22;
	const float32 c02 = a10 * a21 - a11 * a20;
	const float32 c10 = a02 * a21 - a01 * a22;
	const float32 c11 = a00 * a22 - a02 * a20;
	const float32 c12 = a01 * a20 - a00 * a21;
	const float32 c20 = a01 * a12 - a02 * a11;
	const float32 c21 = a02 * a10 - a00 * a12;
	const float32 c22 = a00 * a11 - a01 * a10;

	const float32 determinant = a00 * c00 + a01 * c01 + a02 * c02;
	const float32 sign = (determinant < 0.0f) ? -1.0f : 1.0f;

	for (size_t i = 0; i + 2 < positions.Size; i += 3) {
		float32* p = &positions[i];

		const float32 x = p[0];
		const float32 y = p[1];
		const float32 z = p[2];

		p[0] = a00 * x + a01 * y + a02 * z + m[12];
		p[1] = a10 * x + a11 * y + a12 * z + m[13];
		p[2] = a20 * x + a21 * y + a22 * z + m[14];
	}

	for (size_t i = 0; i + 2 < normals.Size; i += 3) {
		float32* n = &normals[i];

		const float32 x = n[0];
		const float32 y = n[1];
		const float32 z = n[2];

		float32 nx = (c00 * x + c01 * y + c02 * z) * sign;
		float32 ny = (c10 * x + c11 * y + c12 * z) * sign;
		float32 nz = (c20 * x + c21 * y + c22 * z) * sign;

		const float32 length = std::sqrt(nx * nx + ny * ny + nz * nz);

		if (length > 1e-20f) {
			nx /= length;
			ny /= length;
			nz /= length;
		}

		n[0] = nx;
		n[1] = ny;
		n[2] = nz;
	}

	for (size_t i = 0; i + 3 < tangents.Size; i += 4) {
		float32* t = &tangents[i];

		const float32 x = t[0];
		const float32 y = t[1];
		const float32 z = t[2];

		float32 tx = a00 * x + a01 * y + a02 * z;
		float32 ty = a10 * x + a11 * y + a12 * z;
		float32 tz = a20 * x + a21 * y + a22 * z;

		const float32 length = std::sqrt(tx * tx + ty * ty + tz * tz);

		if (length > 1e-20f) {
			tx /= length;
			ty /= length;
			tz /= length;
		}

		t[0] = tx;
		t[1] = ty;
		t[2] = tz;
		t[3] *= sign;
	}

	if (determinant < 0.0f) {
		for (size_t i = 0; i + 2 < indices.Size; i += 3) {
			std::swap(indices[i + 1], indices[i + 2]);
		}
	}
}

bool IsIdentityMatrix(const float32* m)
{
	for (uint32 i = 0; i < 16; i++) {
		const float32 expected = (i % 5 == 0) ? 1.0f : 0.0f;

		if (std::abs(m[i] - expected) > 1e-6f) {
			return false;
		}
	}

	return true;
}

namespace gltf {

bool UnpackFloats(const cgltf_accessor* accessor, cgltf_type expected_type, SizedArray<float32>& out)
{
	if (accessor == nullptr || accessor->type != expected_type) {
		return false;
	}

	const cgltf_size float_count = cgltf_accessor_unpack_floats(accessor, nullptr, 0);

	if (float_count == 0) {
		return false;
	}

	SizedArray<float32> unpacked;
	unpacked.InitSize(float_count);

	if (cgltf_accessor_unpack_floats(accessor, unpacked.pData, float_count) != float_count) {
		return false;
	}

	out = std::move(unpacked);

	return true;
}

bool IsPrimitiveSupported(const cgltf_primitive* primitive)
{
	return primitive->type == cgltf_primitive_type_triangles ||
		   primitive->type == cgltf_primitive_type_triangle_strip ||
		   primitive->type == cgltf_primitive_type_triangle_fan;
}

bool BuildTriangleIndices(const cgltf_primitive* primitive, uint32 vertex_count, SizedArray<uint32>& out_indices)
{
	SizedArray<uint32> source;

	if (primitive->indices != nullptr) {
		const cgltf_size count = primitive->indices->count;

		if (count == 0) {
			return false;
		}

		source.InitSize(count);

		if (cgltf_accessor_unpack_indices(primitive->indices, source.pData, sizeof(uint32), count) != count) {
			return false;
		}
	}
	else {
		source.InitSize(vertex_count);

		for (uint32 i = 0; i < vertex_count; i++) {
			source[i] = i;
		}
	}

	for (uint32 i = 0; i < source.Size; i++) {
		if (source[i] >= vertex_count) {
			return false;
		}
	}

	if (primitive->type == cgltf_primitive_type_triangles) {
		const uint32 usable = static_cast<uint32>((source.Size / 3) * 3);

		if (usable == 0) {
			return false;
		}

		out_indices.InitSize(usable);
		memcpy(out_indices.pData, source.pData, usable * sizeof(uint32));

		return true;
	}

	if (source.Size < 3) {
		return false;
	}

	out_indices.InitCapacity((source.Size - 2) * 3);

	for (uint32 i = 0; i + 2 < source.Size; i++) {
		uint32 a = source[i];
		uint32 b = source[i + 1];
		uint32 c = source[i + 2];

		if (primitive->type == cgltf_primitive_type_triangle_strip) {
			if ((i % 2) == 1) {
				std::swap(a, b);
			}
		}
		else {
			a = source[0];
			b = source[i + 1];
			c = source[i + 2];
		}

		if (a == b || b == c || a == c) {
			continue;
		}

		out_indices.Insert(a);
		out_indices.Insert(b);
		out_indices.Insert(c);
	}

	return out_indices.Size > 0;
}

void ApplyUvTransform(SizedArray<float32>& uvs, const cgltf_texture_transform& transform)
{
	const float32 cosine = std::cos(transform.rotation);
	const float32 sine = std::sin(transform.rotation);

	for (size_t i = 0; i + 1 < uvs.Size; i += 2) {
		const float32 u = uvs[i] * transform.scale[0];
		const float32 v = uvs[i + 1] * transform.scale[1];

		uvs[i] = cosine * u - sine * v + transform.offset[0];
		uvs[i + 1] = sine * u + cosine * v + transform.offset[1];
	}
}

static int32 GetViewUvSet(const cgltf_texture_view& view)
{
	if (view.has_transform && view.transform.has_texcoord) {
		return view.transform.texcoord;
	}

	return view.texcoord;
}

static bool IsSameTransform(const cgltf_texture_view& a, const cgltf_texture_view& b)
{
	if (a.has_transform != b.has_transform) {
		return false;
	}

	if (!a.has_transform) {
		return true;
	}

	const cgltf_texture_transform& ta = a.transform;
	const cgltf_texture_transform& tb = b.transform;

	return std::abs(ta.offset[0] - tb.offset[0]) < 1e-6f && std::abs(ta.offset[1] - tb.offset[1]) < 1e-6f &&
		   std::abs(ta.rotation - tb.rotation) < 1e-6f && std::abs(ta.scale[0] - tb.scale[0]) < 1e-6f &&
		   std::abs(ta.scale[1] - tb.scale[1]) < 1e-6f;
}

UvChoice ChooseUvSet(const cgltf_material* material)
{
	constexpr uint32 scMaxViews = 5;

	UvChoice choice;

	if (material == nullptr) {
		return choice;
	}

	const cgltf_texture_view* views[scMaxViews] = {};
	uint32 view_count = 0;

	const auto add_view = [&](const cgltf_texture_view& view)
	{
		if (view.texture != nullptr) {
			views[view_count++] = &view;
		}
	};

	if (material->has_pbr_specular_glossiness) {
		add_view(material->pbr_specular_glossiness.diffuse_texture);
		add_view(material->pbr_specular_glossiness.specular_glossiness_texture);
	}
	else if (material->has_pbr_metallic_roughness) {
		add_view(material->pbr_metallic_roughness.base_color_texture);
		add_view(material->pbr_metallic_roughness.metallic_roughness_texture);
	}

	add_view(material->normal_texture);
	add_view(material->occlusion_texture);
	add_view(material->emissive_texture);

	if (view_count == 0) {
		return choice;
	}

	const cgltf_texture_view& primary = *views[0];

	choice.Set = static_cast<uint32>(std::max(GetViewUvSet(primary), 0));

	if (primary.has_transform) {
		choice.bHasTransform = true;
		choice.Transform = primary.transform;
	}

	for (uint32 i = 0; i < view_count; i++) {
		if (GetViewUvSet(*views[i]) != GetViewUvSet(primary) || !IsSameTransform(*views[i], primary)) {
			choice.bMismatch = true;
		}
	}

	return choice;
}

} // namespace gltf

namespace rx {

void ReadFloats(const AttributeView& view, uint32 vertex, float32* out)
{
	const uint8* src = view.pData + static_cast<size_t>(vertex) * view.Stride;
	const uint32 components = GetFormatComponents(view.Format);

	switch (view.Format) {
	case VF_F32x1:
	case VF_F32x2:
	case VF_F32x3:
	case VF_F32x4:
		memcpy(out, src, components * sizeof(float32));
		break;
	case VF_F16x2:
	case VF_F16x4:
		for (uint32 i = 0; i < components; i++) {
			uint16 half;
			memcpy(&half, src + i * 2, sizeof(half));
			out[i] = Float16::FromBits(half).ToFloat();
		}
		break;
	case VF_UNorm8x4:
		for (uint32 i = 0; i < components; i++) {
			out[i] = static_cast<float32>(src[i]) / 255.0f;
		}
		break;
	case VF_SNorm8x4:
		for (uint32 i = 0; i < components; i++) {
			out[i] = std::max(static_cast<float32>(static_cast<int8>(src[i])) / 127.0f, -1.0f);
		}
		break;
	case VF_UInt8x4:
		for (uint32 i = 0; i < components; i++) {
			out[i] = static_cast<float32>(src[i]);
		}
		break;
	case VF_UNorm16x2:
	case VF_UNorm16x4:
		for (uint32 i = 0; i < components; i++) {
			uint16 value;
			memcpy(&value, src + i * 2, sizeof(value));
			out[i] = static_cast<float32>(value) / 65535.0f;
		}
		break;
	case VF_SNorm16x4:
		for (uint32 i = 0; i < components; i++) {
			int16 value;
			memcpy(&value, src + i * 2, sizeof(value));
			out[i] = std::max(static_cast<float32>(value) / 32767.0f, -1.0f);
		}
		break;
	case VF_UInt16x4:
		for (uint32 i = 0; i < components; i++) {
			uint16 value;
			memcpy(&value, src + i * 2, sizeof(value));
			out[i] = static_cast<float32>(value);
		}
		break;
	default:
		break;
	}
}

bool IsFormatAllowed(uint8 semantic, uint8 format)
{
	switch (semantic) {
	case VS_Position:
		return format == VF_F32x3;
	case VS_Normal:
		return format == VF_F32x3 || format == VF_SNorm16x4 || format == VF_SNorm8x4;
	case VS_Tangent:
		return format == VF_F32x4 || format == VF_SNorm16x4 || format == VF_SNorm8x4;
	case VS_TexCoord:
		return format == VF_F32x2 || format == VF_F16x2 || format == VF_UNorm16x2;
	case VS_Color:
		return format == VF_UNorm8x4 || format == VF_F16x4 || format == VF_F32x4;
	case VS_Joints:
		return format == VF_UInt8x4 || format == VF_UInt16x4;
	case VS_Weights:
		return format == VF_UNorm8x4 || format == VF_UNorm16x4 || format == VF_F32x4;
	default:
		return GetFormatSize(format) != 0;
	}
}

void ReadAttribute(const AttributeView& view, uint32 base_vertex, uint32 vertex_count, uint32 components,
				   SizedArray<float32>& out)
{
	if (!view.IsValid()) {
		return;
	}

	out.InitSize(static_cast<size_t>(vertex_count) * components);

	for (uint32 i = 0; i < vertex_count; i++) {
		float32 values[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		ReadFloats(view, base_vertex + i, values);

		memcpy(&out[static_cast<size_t>(i) * components], values, components * sizeof(float32));
	}
}

bool ReadSubmeshIndices(const MeshViews& views, const Submesh& submesh, SizedArray<uint32>& out_indices)
{
	out_indices.InitSize(submesh.IndexCount);

	for (uint32 i = 0; i < submesh.IndexCount; i++) {
		const uint8* src = views.pIndices + static_cast<size_t>(submesh.FirstIndex + i) * views.IndexSize;

		uint32 index = 0;

		if (views.IndexSize == 2) {
			uint16 value;
			memcpy(&value, src, sizeof(value));
			index = value;
		}
		else {
			memcpy(&index, src, sizeof(index));
		}

		if (index >= submesh.VertexCount) {
			return false;
		}

		out_indices[i] = index;
	}

	return true;
}

bool ReadMeshViews(const RxBinaryFile& mesh_file, MeshViews& out_views, String& out_error)
{
	const String& mesh_path = mesh_file.GetPath();

	uint32 attribute_count = 0;

	out_views.pHeader = mesh_file.GetSingle<MeshHeader>("MHDR");

	const VertexAttribute* attributes = mesh_file.GetChunk<VertexAttribute>("VATR", attribute_count);
	out_views.pSubmeshes = mesh_file.GetChunk<Submesh>("SUBM", out_views.SubmeshCount);

	const ChunkEntry* index_chunk = mesh_file.FindChunk("IDXB");

	if (out_views.pHeader == nullptr || attributes == nullptr || out_views.pSubmeshes == nullptr) {
		out_error = String::Fmt("'{}' is missing its mesh header, attributes or submeshes", mesh_path);
		return false;
	}

	const MeshHeader& header = *out_views.pHeader;

	if (index_chunk == nullptr || (header.IndexSize != 2 && header.IndexSize != 4) ||
		index_chunk->Stride != header.IndexSize || index_chunk->Count != header.IndexCount) {
		out_error = String::Fmt("'{}' has a missing or malformed index buffer", mesh_path);
		return false;
	}

	out_views.pIndices = mesh_file.GetChunkData(*index_chunk);
	out_views.IndexSize = header.IndexSize;

	if (attribute_count != header.AttributeCount || out_views.SubmeshCount != header.SubmeshCount ||
		header.StreamCount > scMaxStreams || header.VertexCount == 0) {
		out_error = String::Fmt("'{}' has chunks that do not match its mesh header", mesh_path);
		return false;
	}

	const ChunkEntry* streams[scMaxStreams] = {};

	for (uint32 i = 0; i < header.StreamCount; i++) {
		const String id = String::Fmt("VB{:02}", i);

		streams[i] = mesh_file.FindChunk(id.CStr());

		if (streams[i] == nullptr || streams[i]->Count != header.VertexCount) {
			out_error = String::Fmt("'{}' has a missing or malformed vertex stream '{}'", mesh_path, id);
			return false;
		}
	}

	uint32 position_count = 0;

	for (uint32 i = 0; i < attribute_count; i++) {
		const VertexAttribute& attribute = attributes[i];
		const uint32 format_size = GetFormatSize(attribute.Format);

		if (attribute.Stream >= header.StreamCount || format_size == 0 ||
			attribute.Offset + format_size > streams[attribute.Stream]->Stride ||
			!IsFormatAllowed(attribute.Semantic, attribute.Format)) {
			out_error = String::Fmt("'{}' vertex attribute {} (semantic {}, format {}) is malformed", mesh_path, i,
									attribute.Semantic, attribute.Format);
			return false;
		}

		AttributeView view;
		view.pData = mesh_file.GetChunkData(*streams[attribute.Stream]) + attribute.Offset;
		view.Stride = streams[attribute.Stream]->Stride;
		view.Format = attribute.Format;

		switch (attribute.Semantic) {
		case VS_Position:
			out_views.Position = view;
			position_count++;
			break;
		case VS_Normal:
			out_views.Normal = view;
			break;
		case VS_Tangent:
			out_views.Tangent = view;
			break;
		case VS_TexCoord:
			if (attribute.SemanticIndex == 0) {
				out_views.TexCoord = view;
			}
			break;
		case VS_Joints:
			if (attribute.SemanticIndex < scMaxWeightSets) {
				out_views.Joints[attribute.SemanticIndex] = view;
			}
			break;
		case VS_Weights:
			if (attribute.SemanticIndex < scMaxWeightSets) {
				out_views.Weights[attribute.SemanticIndex] = view;
			}
			break;
		case VS_Color:
			out_views.bHasColors = true;
			break;
		default:
			break;
		}
	}

	if (position_count != 1) {
		out_error = String::Fmt("'{}' must have exactly one POSITION attribute, found {}", mesh_path, position_count);
		return false;
	}

	return true;
}

} // namespace rx

namespace {

String GetDirectory(const String& path)
{
	const uint32 slash_index = path.FindLast('/');

	if (slash_index == String::scNotFound) {
		return String();
	}

	return String(path.CStr(), slash_index + 1);
}

bool HasExtension(const String& path, const char* extension)
{
	const size_t length = strlen(extension);

	if (path.GetLength() < length) {
		return false;
	}

	const char* tail = path.CStr() + path.GetLength() - length;

	for (size_t i = 0; i < length; i++) {
		if (std::tolower(static_cast<unsigned char>(tail[i])) != extension[i]) {
			return false;
		}
	}

	return true;
}

void ExpandByIndices(SizedArray<float32>& values, uint32 components, const SizedArray<uint32>& indices)
{
	if (values.IsEmpty()) {
		return;
	}

	SizedArray<float32> expanded;
	expanded.InitSize(indices.Size * components);

	for (size_t i = 0; i < indices.Size; i++) {
		memcpy(&expanded[i * components], &values[static_cast<size_t>(indices[i]) * components],
			   components * sizeof(float32));
	}

	values = std::move(expanded);
}

/// Matches MeshUtil::ExpandByIndices + GenerateFlatNormals, which the loaders run when a mesh has no normals
void MakeFlatNormals(SizedArray<float32>& positions, SizedArray<float32>& uvs, SizedArray<uint32>& indices,
					 SizedArray<float32>& out_normals)
{
	ExpandByIndices(positions, 3, indices);
	ExpandByIndices(uvs, 2, indices);

	for (uint32 i = 0; i < indices.Size; i++) {
		indices[i] = i;
	}

	const uint32 vertex_count = static_cast<uint32>(positions.Size / 3);

	out_normals.InitSize(static_cast<size_t>(vertex_count) * 3);

	for (uint32 vertex = 0; vertex + 2 < vertex_count; vertex += 3) {
		const FVec3 a(positions[vertex * 3], positions[vertex * 3 + 1], positions[vertex * 3 + 2]);
		const FVec3 b(positions[vertex * 3 + 3], positions[vertex * 3 + 4], positions[vertex * 3 + 5]);
		const FVec3 c(positions[vertex * 3 + 6], positions[vertex * 3 + 7], positions[vertex * 3 + 8]);

		FVec3 normal = (b - a).Cross(c - a);

		if (normal.Length() < 1e-20f) {
			normal = FVec3(0.0f, 1.0f, 0.0f);
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

const cgltf_image* GetTextureImage(const cgltf_texture* texture)
{
	if (texture->has_basisu && texture->basisu_image != nullptr) {
		return texture->basisu_image;
	}

	return texture->image;
}

void ReadGltfMaterial(const cgltf_material& source, const String& directory, StaticModelMaterial& out)
{
	out.Name = (source.name != nullptr) ? String(source.name) : String("<unnamed>");
	out.bBlend = (source.alpha_mode == cgltf_alpha_mode_blend);

	const cgltf_texture_view* diffuse = nullptr;

	if (source.has_pbr_specular_glossiness) {
		const float32* factor = source.pbr_specular_glossiness.diffuse_factor;
		out.BaseColor = FVec3(factor[0], factor[1], factor[2]);
		diffuse = &source.pbr_specular_glossiness.diffuse_texture;
	}
	else if (source.has_pbr_metallic_roughness) {
		const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;

		out.BaseColor = FVec3(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
		out.Metallic = pbr.metallic_factor;
		out.bHasMetallicTexture = (pbr.metallic_roughness_texture.texture != nullptr);
		diffuse = &pbr.base_color_texture;
	}

	if (diffuse == nullptr || diffuse->texture == nullptr) {
		return;
	}

	const cgltf_image* image = GetTextureImage(diffuse->texture);

	if (image == nullptr) {
		return;
	}

	if (image->buffer_view != nullptr) {
		const uint8* data = cgltf_buffer_view_data(image->buffer_view);

		if (data != nullptr && image->buffer_view->size > 0) {
			out.DiffuseData.InitSize(image->buffer_view->size);
			memcpy(out.DiffuseData.pData, data, image->buffer_view->size);
		}

		return;
	}

	if (image->uri == nullptr || strncmp(image->uri, "data:", 5) == 0) {
		return;
	}

	std::string uri = image->uri;
	uri.resize(cgltf_decode_uri(uri.data()));

	out.DiffusePath = directory + uri.c_str();
}

bool HasJointAncestor(const cgltf_node* node, const SizedArray<uint8>& is_joint, const cgltf_data& data)
{
	for (const cgltf_node* ancestor = node->parent; ancestor != nullptr; ancestor = ancestor->parent) {
		if (is_joint[static_cast<size_t>(ancestor - data.nodes)] != 0) {
			return true;
		}
	}

	return false;
}

bool ReadGltfMesh(const cgltf_primitive& primitive, const float32* world_matrix, bool bake_transform, uint32 material,
				  StaticModelMesh& out_mesh)
{
	const cgltf_accessor* position_accessor = nullptr;
	const cgltf_accessor* normal_accessor = nullptr;
	const cgltf_accessor* uv_accessors[gltf::scMaxUvSets] = {};

	for (cgltf_size i = 0; i < primitive.attributes_count; i++) {
		const cgltf_attribute& attribute = primitive.attributes[i];
		const cgltf_int index = attribute.index;

		if (attribute.type == cgltf_attribute_type_position && index == 0) {
			position_accessor = attribute.data;
		}
		else if (attribute.type == cgltf_attribute_type_normal && index == 0) {
			normal_accessor = attribute.data;
		}
		else if (attribute.type == cgltf_attribute_type_texcoord && index >= 0 &&
				 index < static_cast<cgltf_int>(gltf::scMaxUvSets)) {
			uv_accessors[index] = attribute.data;
		}
	}

	if (!gltf::UnpackFloats(position_accessor, cgltf_type_vec3, out_mesh.Positions)) {
		return false;
	}

	const uint32 vertex_count = static_cast<uint32>(out_mesh.Positions.Size / 3);

	if (!gltf::BuildTriangleIndices(&primitive, vertex_count, out_mesh.Indices)) {
		return false;
	}

	if (gltf::UnpackFloats(normal_accessor, cgltf_type_vec3, out_mesh.Normals) &&
		out_mesh.Normals.Size != static_cast<size_t>(vertex_count) * 3) {
		out_mesh.Normals.Free();
	}

	const gltf::UvChoice uv_choice = gltf::ChooseUvSet(primitive.material);

	uint32 uv_set = (uv_choice.Set < gltf::scMaxUvSets) ? uv_choice.Set : 0;

	if (uv_accessors[uv_set] == nullptr) {
		for (uint32 set = 0; set < gltf::scMaxUvSets; set++) {
			if (uv_accessors[set] != nullptr) {
				uv_set = set;
				break;
			}
		}
	}

	if (gltf::UnpackFloats(uv_accessors[uv_set], cgltf_type_vec2, out_mesh.Uvs) &&
		out_mesh.Uvs.Size != static_cast<size_t>(vertex_count) * 2) {
		out_mesh.Uvs.Free();
	}

	SizedArray<float32> no_tangents;

	if (bake_transform) {
		TransformModelGeometry(out_mesh.Positions, out_mesh.Normals, no_tangents, out_mesh.Indices, world_matrix);
	}

	if (uv_choice.bHasTransform && !out_mesh.Uvs.IsEmpty()) {
		gltf::ApplyUvTransform(out_mesh.Uvs, uv_choice.Transform);
	}

	if (out_mesh.Normals.IsEmpty()) {
		MakeFlatNormals(out_mesh.Positions, out_mesh.Uvs, out_mesh.Indices, out_mesh.Normals);
	}

	// The engine flips X when it creates the vertex buffers (eVertexCreateFlags::NegativeX)
	for (size_t i = 0; i + 2 < out_mesh.Positions.Size; i += 3) {
		out_mesh.Positions[i] = -out_mesh.Positions[i];
		out_mesh.Normals[i] = -out_mesh.Normals[i];
	}

	out_mesh.Material = material;

	return true;
}

bool ReadGltfModel(const String& path, StaticModel& out_model, String& out_error)
{
	const String resolved = FilesystemIO::ResolvePath(path);

	cgltf_options options {};
	cgltf_data* data = nullptr;

	if (cgltf_parse_file(&options, resolved.CStr(), &data) != cgltf_result_success) {
		out_error = String::Fmt("could not parse '{}'", path);
		return false;
	}

	if (cgltf_load_buffers(&options, data, resolved.CStr()) != cgltf_result_success) {
		out_error = String::Fmt("could not load the buffers of '{}'", path);
		cgltf_free(data);
		return false;
	}

	const String directory = GetDirectory(path);

	// The last material stands in for primitives that have none
	const uint32 default_material = static_cast<uint32>(data->materials_count);

	out_model.Materials.InitSize(data->materials_count + 1);

	for (cgltf_size i = 0; i < data->materials_count; i++) {
		ReadGltfMaterial(data->materials[i], directory, out_model.Materials[i]);
	}

	out_model.Materials[default_material].Name = "<default>";

	SizedArray<uint8> is_joint;
	is_joint.InitSize(data->nodes_count, static_cast<uint8>(0));

	for (cgltf_size skin_index = 0; skin_index < data->skins_count; skin_index++) {
		const cgltf_skin& skin = data->skins[skin_index];

		for (cgltf_size joint_index = 0; joint_index < skin.joints_count; joint_index++) {
			is_joint[static_cast<size_t>(skin.joints[joint_index] - data->nodes)] = 1;
		}
	}

	for (cgltf_size node_index = 0; node_index < data->nodes_count; node_index++) {
		const cgltf_node* node = &data->nodes[node_index];

		if (node->mesh == nullptr) {
			continue;
		}

		if (node->skin != nullptr || HasJointAncestor(node, is_joint, *data)) {
			out_model.SkippedMeshCount++;
			continue;
		}

		float32 world_matrix[16];
		cgltf_node_transform_world(node, world_matrix);

		const bool bake_transform = !IsIdentityMatrix(world_matrix);

		for (cgltf_size i = 0; i < node->mesh->primitives_count; i++) {
			const cgltf_primitive& primitive = node->mesh->primitives[i];

			if (!gltf::IsPrimitiveSupported(&primitive)) {
				continue;
			}

			uint32 material = default_material;

			if (primitive.material != nullptr) {
				material = static_cast<uint32>(primitive.material - data->materials);
			}

			StaticModelMesh mesh;

			if (ReadGltfMesh(primitive, world_matrix, bake_transform, material, mesh)) {
				out_model.Meshes.Emplace(std::move(mesh));
			}
		}
	}

	cgltf_free(data);

	return true;
}

float32 GetRxParam(const RxMaterialDesc& desc, const StringView name, float32 fallback)
{
	const RxMaterialParam* param = desc.FindParam(name);

	if (param == nullptr || param->Values.Size == 0) {
		return fallback;
	}

	return param->Values[0];
}

bool ReadRxModel(const String& path, StaticModel& out_model, String& out_error)
{
	RxModelDesc desc;

	if (!LoadRxModelDesc(path, desc)) {
		out_error = String::Fmt("could not parse '{}'", path);
		return false;
	}

	const String directory = GetDirectory(path);

	RxBinaryFile mesh_file;

	if (!mesh_file.Open(FilesystemIO::ResolvePath(directory + desc.Mesh.Path), "MESH")) {
		out_error = String::Fmt("could not open the mesh of '{}'", path);
		return false;
	}

	rx::MeshViews views;

	if (!rx::ReadMeshViews(mesh_file, views, out_error)) {
		return false;
	}

	out_model.Materials.InitSize(desc.Materials.Size);

	for (uint32 i = 0; i < desc.Materials.Size; i++) {
		const RxMaterialDesc& source = desc.Materials[i];
		StaticModelMaterial& material = out_model.Materials[i];

		material.Name = source.MaterialName;
		material.bBlend = (source.Blend == eRxBlendMode::Blend);
		material.bHasMetallicTexture = (source.FindTexture("orm") != nullptr);
		material.Metallic = GetRxParam(source, "metallic", material.bHasMetallicTexture ? 1.0f : 0.0f);

		if (const RxMaterialParam* base_color = source.FindParam("base_color")) {
			if (base_color->Values.Size >= 3) {
				material.BaseColor = FVec3(base_color->Values[0], base_color->Values[1], base_color->Values[2]);
			}
		}

		if (const String* albedo = source.FindTexture("albedo")) {
			material.DiffusePath = directory + *albedo;
		}
	}

	const Mat4f root_transform = Mat4f::AsScale(desc.Root.Scale) * Mat4f::AsRotation(desc.Root.Rotation) *
								 Mat4f::AsTranslation(desc.Root.Position);

	const rx::MeshHeader& header = *views.pHeader;

	for (uint32 i = 0; i < views.SubmeshCount; i++) {
		const rx::Submesh& submesh = views.pSubmeshes[i];

		const bool has_valid_ranges = submesh.IndexCount > 0 && (submesh.IndexCount % 3) == 0 &&
									  submesh.FirstIndex <= header.IndexCount &&
									  submesh.IndexCount <= header.IndexCount - submesh.FirstIndex &&
									  submesh.VertexCount > 0 && submesh.BaseVertex <= header.VertexCount &&
									  submesh.VertexCount <= header.VertexCount - submesh.BaseVertex;

		if (!has_valid_ranges || submesh.Lod != 0) {
			continue;
		}

		if ((submesh.Flags & rx::scSubmeshSkinned) != 0 || submesh.ParentJoint != rx::scNoJoint) {
			out_model.SkippedMeshCount++;
			continue;
		}

		const RxMaterialDesc* material = desc.FindMaterial(MakeStringView(mesh_file.GetString(submesh.Material)));

		if (material == nullptr) {
			continue;
		}

		StaticModelMesh mesh;
		mesh.Material = static_cast<uint32>(material - desc.Materials.pData);

		if (!rx::ReadSubmeshIndices(views, submesh, mesh.Indices)) {
			continue;
		}

		rx::ReadAttribute(views.Position, submesh.BaseVertex, submesh.VertexCount, 3, mesh.Positions);
		rx::ReadAttribute(views.Normal, submesh.BaseVertex, submesh.VertexCount, 3, mesh.Normals);
		rx::ReadAttribute(views.TexCoord, submesh.BaseVertex, submesh.VertexCount, 2, mesh.Uvs);

		if (mesh.Normals.IsEmpty()) {
			MakeFlatNormals(mesh.Positions, mesh.Uvs, mesh.Indices, mesh.Normals);
		}

		if (!desc.Root.bIsIdentity) {
			SizedArray<float32> no_tangents;
			TransformModelGeometry(mesh.Positions, mesh.Normals, no_tangents, mesh.Indices, root_transform.RawData);
		}

		out_model.Meshes.Emplace(std::move(mesh));
	}

	return true;
}

} // namespace

bool ReadStaticModel(const String& path, StaticModel& out_model, String& out_error)
{
	out_model = StaticModel {};

	if (HasExtension(path, ".rxmodel")) {
		return ReadRxModel(path, out_model, out_error);
	}

	if (HasExtension(path, ".glb") || HasExtension(path, ".gltf")) {
		return ReadGltfModel(path, out_model, out_error);
	}

	out_error = String::Fmt("'{}' is not a .glb, .gltf or .rxmodel file", path);

	return false;
}

} // namespace fx::loader
