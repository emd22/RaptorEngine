#include "MeshGen.hpp"

#include <Core/RefUtil.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/Backend/GraphicsBackendFwd.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/PrimitiveMesh.hpp>
#include <unordered_map>

namespace fx {

static const float Z = (1.0f + sqrt(5.0f)) / 2.0f;	// Golden ratio
static const Vec2f UV = Vec2f(1 / 11.0f, 1 / 3.0f); // The UV coordinates are laid out in a 11x3 grid

static const int IcoVertexCount = 22;
static const int IcoIndexCount = 60;

static const Vec3f IcoVerts[] = {
	Vec3f(0, -1, -Z), Vec3f(-1, -Z, 0), Vec3f(Z, 0, -1), Vec3f(1, -Z, 0),  Vec3f(1, Z, 0), Vec3f(-1, -Z, 0),
	Vec3f(Z, 0, 1),	  Vec3f(0, -1, Z),	Vec3f(1, Z, 0),	 Vec3f(-1, -Z, 0), Vec3f(0, 1, Z), Vec3f(-Z, 0, 1),
	Vec3f(1, Z, 0),	  Vec3f(-1, -Z, 0), Vec3f(-1, Z, 0), Vec3f(-Z, 0, -1), Vec3f(1, Z, 0), Vec3f(-1, -Z, 0),
	Vec3f(0, 1, -Z),  Vec3f(0, -1, -Z), Vec3f(1, Z, 0),	 Vec3f(Z, 0, -1),
};

static const Vec2f IcoUvs[] = {
	UV * Vec2f(0, 1), UV* Vec2f(1, 0),	UV* Vec2f(1, 2),  UV* Vec2f(2, 1),	UV* Vec2f(2, 3), UV* Vec2f(3, 0),
	UV* Vec2f(3, 2),  UV* Vec2f(4, 1),	UV* Vec2f(4, 3),  UV* Vec2f(5, 0),	UV* Vec2f(5, 2), UV* Vec2f(6, 1),
	UV* Vec2f(6, 3),  UV* Vec2f(7, 0),	UV* Vec2f(7, 2),  UV* Vec2f(8, 1),	UV* Vec2f(8, 3), UV* Vec2f(9, 0),
	UV* Vec2f(9, 2),  UV* Vec2f(10, 1), UV* Vec2f(10, 3), UV* Vec2f(11, 2),
};

static const int32 IcoIndex[] = {
	2, 6,  4, // Top
	6, 10, 8, 10, 14, 12, 14, 18, 16, 18, 21, 20,

	0, 3,  2, // Middle
	2, 3,  6, 3,  7,  6,  6,  7,  10, 7,  11, 10, 10, 11, 14, 11, 15, 14, 14, 15, 18, 15, 19, 18, 18, 19, 21,

	0, 1,  3, // Bottom
	3, 5,  7, 7,  9,  11, 11, 13, 15, 15, 17, 19,
};


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

	mesh->UploadIndices(renderer::GraphicsBackendFwd::GetCmd(), Indices);

	mesh->VertexList.CreateFrom<renderer::eVertexType::Slim>(std::move(points));
	mesh->UploadVertices(renderer::GraphicsBackendFwd::GetCmd());

	mesh->bIsReady.store(true);

	return mesh;
}

Ref<PrimitiveMesh> MeshGen::GeneratedMesh::AsDefaultMesh()
{
	// Create a mesh using the default vertex format (positions, normals, uvs)
	Ref<PrimitiveMesh> mesh = MakeRef<PrimitiveMesh>();

	mesh->bKeepInMemory = true;

	mesh->VertexList.CreateFrom(Positions, Normals, Uvs, {}, {}, {}, eVertexCreateFlags::None);
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

// Implementation is based on code from https://winter.dev/projects/mesh/icosphere
Ref<MeshGen::GeneratedMesh> MeshGen::MakeIcoSphere(int resolution)
{
	const int rn = static_cast<int>(pow(4, resolution));

	const int total_index_count = IcoIndexCount * rn;
	const int total_vertex_count = IcoVertexCount + IcoIndexCount * (1 - rn) / (1 - 4);

	SizedArray<Vec3f> positions;
	positions.InitSize(total_vertex_count);

	SizedArray<uint32> indices;
	indices.InitSize(total_index_count);

	for (int i = 0; i < IcoVertexCount; i++) { // Copy in initial mesh
		positions[i] = IcoVerts[i];
	}

	for (int i = 0; i < IcoIndexCount; i++) {
		indices[i] = IcoIndex[i];
	}

	uint32 current_index_count = IcoIndexCount;
	uint32 current_vert_count = IcoVertexCount;

	for (int r = 0; r < resolution; r++) {
		std::unordered_map<uint64_t, int> triangleFromEdge;
		uint32 index_count = current_index_count;

		for (int t = 0; t < index_count; t += 3) {
			int midpoints[3] {};

			for (int e = 0; e < 3; e++) {
				uint32 first = indices[t + e];
				uint32 second = indices[t + (t + e + 1) % 3];

				if (first > second) {
					std::swap(first, second);
				}

				uint64_t hash = (uint64_t)first | (uint64_t)second << (sizeof(uint32_t) * 8);

				auto [triangle, was_new_edge] = triangleFromEdge.insert({ hash, current_vert_count });

				if (was_new_edge) {
					positions[current_vert_count] = (positions[first] + positions[second]) / 2.0f;

					current_vert_count += 1;
				}

				midpoints[e] = triangle->second;
			}

			int mid0 = midpoints[0];
			int mid1 = midpoints[1];
			int mid2 = midpoints[2];

			indices[current_index_count++] = indices[t];
			indices[current_index_count++] = mid0;
			indices[current_index_count++] = mid2;

			indices[current_index_count++] = indices[t + 1];
			indices[current_index_count++] = mid1;
			indices[current_index_count++] = mid0;

			indices[current_index_count++] = indices[t + 2];
			indices[current_index_count++] = mid2;
			indices[current_index_count++] = mid1;

			indices[t] = mid0; // Overwrite the original triangle with the 4th new triangle
			indices[t + 1] = mid1;
			indices[t + 2] = mid2;
		}
	}

	// Normalize all of the positions
	for (Vec3f& pos : positions) {
		pos.NormalizeIP();
	}

	Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();
	mesh->Positions = std::move(positions);
	mesh->Indices = std::move(indices);

	return mesh;
}

static void MC_EmitQuad(SizedArray<Vec3f>& positions, SizedArray<Vec3f>& normals, SizedArray<Vec2f>& uvs,
						SizedArray<uint32>& indices, const Vec3f (&verts)[4], const MeshGenOptions& options)
{
	const Vec3f surface_normal = Vec3f::GetSurfaceNormal(verts[0], verts[1], verts[2]);
	const uint32 base = static_cast<uint32>(positions.Size);

	for (int i = 0; i < 4; i++) {
		positions.Insert(verts[i]);
		normals.Insert(surface_normal);
	}

	// Top Left
	uvs.Insert(Vec2f(options.UvMin.X, options.UvMin.Y));
	// Top Right
	uvs.Insert(Vec2f(options.UvMax.X, options.UvMin.Y));
	// Bottom Right
	uvs.Insert(Vec2f(options.UvMax.X, options.UvMax.Y));
	// Bottom Left
	uvs.Insert(Vec2f(options.UvMin.X, options.UvMax.Y));

	// Top left triangle
	indices.Insert(base + 0);
	indices.Insert(base + 1);
	indices.Insert(base + 3);
	// Bottom right triangle
	indices.Insert(base + 1);
	indices.Insert(base + 2);
	indices.Insert(base + 3);
}

Ref<MeshGen::GeneratedMesh> MeshGen::MakeCube(CubeGenOptions options)
{
	Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();

	// Scales for each face
	const float32 l_s = options.Left.Scale;
	const float32 r_s = options.Right.Scale;
	const float32 t_s = options.Top.Scale;
	const float32 b_s = options.Bottom.Scale;
	const float32 fr_s = options.Front.Scale;
	const float32 ba_s = options.Back.Scale;

	// 4 verts per face and 36 indices (2 tris per face)
	mesh->Positions.InitCapacity(24);
	mesh->Normals.InitCapacity(24);
	mesh->Uvs.InitCapacity(24);
	mesh->Indices.InitCapacity(36);

	const Vec3f f_tl { -l_s, t_s, fr_s };	// Front TL
	const Vec3f f_tr { r_s, t_s, fr_s };	// Front TR
	const Vec3f f_bl { -l_s, -b_s, fr_s };	// Front BL
	const Vec3f f_br { r_s, -b_s, fr_s };	// Front BR
	const Vec3f b_tl { -l_s, t_s, -ba_s };	// Back TL
	const Vec3f b_tr { r_s, t_s, -ba_s };	// Back TR
	const Vec3f b_bl { -l_s, -b_s, -ba_s }; // Back BL
	const Vec3f b_br { r_s, -b_s, -ba_s };	// Back BR

	// Front face (+Z)
	MC_EmitQuad(mesh->Positions, mesh->Normals, mesh->Uvs, mesh->Indices, { f_tl, f_tr, f_br, f_bl }, options.Front);

	// Back face (-Z)
	MC_EmitQuad(mesh->Positions, mesh->Normals, mesh->Uvs, mesh->Indices, { b_tr, b_tl, b_bl, b_br }, options.Back);

	// Top face (+Y)
	MC_EmitQuad(mesh->Positions, mesh->Normals, mesh->Uvs, mesh->Indices, { b_tl, b_tr, f_tr, f_tl }, options.Top);

	// Bottom face (-Y)
	MC_EmitQuad(mesh->Positions, mesh->Normals, mesh->Uvs, mesh->Indices, { f_bl, f_br, b_br, b_bl }, options.Bottom);

	// Right face (+X)
	MC_EmitQuad(mesh->Positions, mesh->Normals, mesh->Uvs, mesh->Indices, { f_tr, b_tr, b_br, f_br }, options.Right);

	// Left face (-X)
	MC_EmitQuad(mesh->Positions, mesh->Normals, mesh->Uvs, mesh->Indices, { b_tl, f_tl, f_bl, b_bl }, options.Left);

	return mesh;
}

Ref<MeshGen::GeneratedMesh> MeshGen::MakeQuad(MeshGenOptions options)
{
	Ref<MeshGen::GeneratedMesh> mesh = MakeRef<MeshGen::GeneratedMesh>();

	const float32 s = options.Scale;

	/*
		0-------1
		|     / |
		|   /   |
		| /     |
		3-------2
	*/

	mesh->Positions = {
		// Top Left
		Vec3f(-s, s, 0.0f), // 0
		// Top Right
		Vec3f(s, s, 0.0f), // 1

		// Bottom Right
		Vec3f(s, -s, 0.0f), // 2

		// Bottom Left
		Vec3f(-s, -s, 0.0f), // 3
	};

	Vec3f surface_normal = Vec3f::GetSurfaceNormal(mesh->Positions[0], mesh->Positions[1], mesh->Positions[2]);

	mesh->Normals = { surface_normal, surface_normal, surface_normal, surface_normal };

	mesh->Uvs = {
		// Top Left
		Vec2f(options.UvMin.X, options.UvMin.Y), // 0

		// Top Right
		Vec2f(options.UvMax.X, options.UvMin.Y), // 1

		// Bottom Right
		Vec2f(options.UvMax.X, options.UvMax.Y), // 2

		// Bottom Left
		Vec2f(options.UvMin.X, options.UvMax.Y), // 3
	};


	mesh->Indices = { // Top left triangle
					  1, 0, 3,
					  // Bottom right triangle
					  1, 3, 2
	};

	return mesh;
}

} // namespace fx
