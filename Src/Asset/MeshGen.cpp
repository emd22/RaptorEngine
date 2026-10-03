#include "MeshGen.hpp"

#include <Core/RefUtil.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/Backend/GraphicsBackendFwd.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/PrimitiveMesh.hpp>
#include <raptor_ffi.h>

namespace fx {

Ref<PrimitiveMesh> MeshGen::GeneratedMesh::AsSlimMesh()
{
	Ref<PrimitiveMesh> mesh = MakeRef<PrimitiveMesh>();

	SizedArray<renderer::Vertex<renderer::eVertexType::Slim>> points(Positions.Size);

	for (uint32 i = 0; i < Positions.Size; i++) {
		auto* vertex = points.Insert();
		Vec3f& vec = Positions[i];

		vertex->Position[0] = vec.X;
		vertex->Position[1] = vec.Y;
		vertex->Position[2] = vec.Z;
	}

	mesh->VertexList.CreateFrom<renderer::eVertexType::Slim>(std::move(points));

	// Upload on the immediate transfer cmd as slim meshes are created lazily mid-frame, and copies recorded into the
	// frame cmd would land inside an active render pass. Gotta replace this with something else.
	renderer::gGraphics->SubmitImmediateUploadCmd(
		[&](renderer::CommandBuffer& cmd)
		{
			mesh->UploadIndices(cmd, Indices);
			mesh->UploadVertices(cmd);
		});

	mesh->bIsReady.store(true);

	return mesh;
}

Ref<PrimitiveMesh> MeshGen::GeneratedMesh::AsDefaultMesh()
{
	// Create a mesh using the default vertex format (positions, normals, uvs)
	Ref<PrimitiveMesh> mesh = MakeRef<PrimitiveMesh>();

	mesh->bKeepInMemory = true;

	mesh->VertexList.CreateFrom(Positions, Normals, Texcoords, Tangents, {}, {}, eVertexCreateFlags::None, 1.0f);
	renderer::gGraphics->SubmitImmediateUploadCmd(
		[&](renderer::CommandBuffer& cmd)
		{
			mesh->UploadIndices(cmd, Indices);
			mesh->UploadVertices(cmd);
		});

	mesh->bIsReady.store(true);

	return mesh;
}

Ref<PrimitiveMesh> MeshGen::GeneratedMesh::AsMesh(renderer::eVertexType vertex_type)
{
	switch (vertex_type) {
	case renderer::eVertexType::Default:
		return AsDefaultMesh();
	case renderer::eVertexType::Slim:
		return AsSlimMesh();
	default:
		LogError(LC_ASSET, "Mesh type is not supported!");
	}

	return nullptr;
}

namespace {

RxFaceOptions ToRust(const MeshGenOptions& options)
{
	return RxFaceOptions { .scale = options.Scale,
						   .uv_min = { options.UvMin.X, options.UvMin.Y },
						   .uv_max = { options.UvMax.X, options.UvMax.Y } };
}

void ReadVec3s(SizedArray<Vec3f>& out, const float* data, size_t count)
{
	if (count == 0) {
		return;
	}

	out.InitCapacity(count);

	for (size_t i = 0; i < count; i++) {
		out.Insert(Vec3f(data[i * 3], data[i * 3 + 1], data[i * 3 + 2]));
	}
}

Ref<MeshGen::GeneratedMesh> TakeRustMesh(RxMesh* rust_mesh)
{
	Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();

	if (rust_mesh == nullptr) {
		LogError(LC_ASSET, "Mesh generation failed");
		return mesh;
	}

	size_t count = 0;

	const float* positions = rx_mesh_positions(rust_mesh, &count);
	ReadVec3s(mesh->Positions, positions, count);

	const float* normals = rx_mesh_normals(rust_mesh, &count);
	ReadVec3s(mesh->Normals, normals, count);

	const float* tangents = rx_mesh_tangents(rust_mesh, &count);
	ReadVec3s(mesh->Tangents, tangents, count);

	const float* texcoords = rx_mesh_texcoords(rust_mesh, &count);
	if (count > 0) {
		mesh->Texcoords.InitCapacity(count);

		for (size_t i = 0; i < count; i++) {
			mesh->Texcoords.Insert(Vec2f(texcoords[i * 2], texcoords[i * 2 + 1]));
		}
	}

	const uint32* indices = rx_mesh_indices(rust_mesh, &count);
	if (count > 0) {
		mesh->Indices.InitCapacity(count);

		for (size_t i = 0; i < count; i++) {
			mesh->Indices.Insert(indices[i]);
		}
	}

	rx_mesh_free(rust_mesh);

	return mesh;
}

} // namespace

Ref<MeshGen::GeneratedMesh> MeshGen::MakeIcoSphere(int resolution)
{
	return TakeRustMesh(rx_mesh_icosphere(resolution));
}

Ref<MeshGen::GeneratedMesh> MeshGen::MakeCube(CubeGenOptions options)
{
	const RxCubeOptions rust_options = {
		.left = ToRust(options.Left),
		.right = ToRust(options.Right),
		.top = ToRust(options.Top),
		.bottom = ToRust(options.Bottom),
		.front = ToRust(options.Front),
		.back = ToRust(options.Back),
		.align_uvs = static_cast<uint8>(options.bAlignUVs ? 1 : 0),
	};

	return TakeRustMesh(rx_mesh_cube(&rust_options));
}

Ref<MeshGen::GeneratedMesh> MeshGen::MakeWireframeBox() { return TakeRustMesh(rx_mesh_wireframe_box()); }

Ref<MeshGen::GeneratedMesh> MeshGen::MakeLine() { return TakeRustMesh(rx_mesh_line()); }

Ref<MeshGen::GeneratedMesh> MeshGen::MakeQuad(Vec2f scale) { return TakeRustMesh(rx_mesh_quad(scale.X, scale.Y)); }

} // namespace fx
