#pragma once

#include "Backend/GpuBuffer.hpp"
#include "VertexList.hpp"

#include <Math/Quat.hpp>

namespace fx {

class PrimitiveMesh
{
public:
	PrimitiveMesh() = default;

	/**
	 * @brief Recalculates normals if needed and uploads the vertex list to the GPU. Unless `bKeepInMemory` is set, the
	 * cpu-side vertices and indices are released once they have been copied into the upload.
	 */
	inline void UploadVertices(renderer::CommandBuffer& cmd)
	{
		if (VertexList.SupportsNormals() && !VertexList.HasNormals()) {
			RecalculateNormals();
		}

		VertexList.UploadToGpu(cmd);

		if (!bKeepInMemory) {
			VertexList.DestroyLocalBuffer();
			LocalIndexBuffer.Free();
		}
	}

	/**
	 * @brief Uploads mesh indices to a primitive mesh.
	 */
	void UploadIndices(renderer::CommandBuffer& cmd, const SizedArray<uint32>& indices)
	{
		if (indices.IsEmpty()) {
			LogError(LC_ASSET, "Cannot upload a mesh without indices");
			return;
		}

		LocalIndexBuffer.InitAsCopyOf(indices);
		GpuIndexBuffer.Create(cmd, renderer::eGpuBufferType::IndexBuffer, Slice(indices));
	}

	void UploadIndices(renderer::CommandBuffer& cmd)
	{
		if (LocalIndexBuffer.IsEmpty()) {
			LogError(LC_ASSET, "Cannot upload a mesh without indices");
			return;
		}

		GpuIndexBuffer.Create<uint32>(cmd, renderer::eGpuBufferType::IndexBuffer, LocalIndexBuffer);
	}

	void SetIndices(SizedArray<uint32>&& indices) { LocalIndexBuffer = std::move(indices); }

	renderer::VertexList& GetVertices()
	{
		if (!bKeepInMemory) {
			LogWarning(LC_ASSET, "Requesting vertices from a primitive mesh while `KeepInMemory` != true!");
		}

		return VertexList;
	}

	SizedArray<uint32>& GetIndices()
	{
		if (!bKeepInMemory) {
			LogWarning(LC_ASSET, "Requesting indices from a primitive mesh while `KeepInMemory` != true!");
		}

		return LocalIndexBuffer;
	}

	bool IsWritable() { return (VertexList.GpuBuffer.Initialized && GpuIndexBuffer.Initialized); }

	renderer::GpuBuffer& GetVertexBuffer() { return VertexList.GpuBuffer; }
	renderer::GpuBuffer& GetIndexBuffer() { return GpuIndexBuffer; }

	void Render(const renderer::CommandBuffer& cmd, uint32 num_instances)
	{
		if (!IsWritable()) {
			return;
		}

		const VkDeviceSize offset = 0;

		vkCmdBindVertexBuffers(cmd.Cmd, 0, 1, &VertexList.GpuBuffer.Buffer, &offset);
		vkCmdBindIndexBuffer(cmd.Cmd, GpuIndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(cmd.Cmd, static_cast<uint32>(GpuIndexBuffer.Size / sizeof(uint32)), num_instances, 0, 0, 0);
	}

	void RecalculateNormals()
	{
		using VertexType = renderer::Vertex<renderer::eVertexType::Default>;

		if (LocalIndexBuffer.IsEmpty()) {
			LogWarning(LC_ASSET, "Cannot recalculate normals as local indices are missing!");
			return;
		}

		AnonArray& vertices = VertexList.GetLocalBuffer();

		if (vertices.IsEmpty()) {
			LogWarning(LC_ASSET, "Cannot recalculate normals as local vertices are missing!");
			return;
		}

		const uint32 num_vertices = vertices.Size;
		const uint32 num_faces = static_cast<uint32>(LocalIndexBuffer.Size / 3);

		uint8* const base = static_cast<uint8*>(vertices.pData);
		const size_t stride = vertices.ObjectSize;

		auto vertex_at = [&](uint32 index) -> VertexType&
		{ return *reinterpret_cast<VertexType*>(base + (static_cast<size_t>(index) * stride)); };

		for (uint32 index = 0; index < num_vertices; index++) {
			memset(vertex_at(index).Normal, 0, sizeof(VertexType::Normal));
		}

		for (uint32 face = 0; face < num_faces; face++) {
			const uint32 index_a = LocalIndexBuffer.pData[face * 3];
			const uint32 index_b = LocalIndexBuffer.pData[face * 3 + 1];
			const uint32 index_c = LocalIndexBuffer.pData[face * 3 + 2];

			if (index_a >= num_vertices || index_b >= num_vertices || index_c >= num_vertices) {
				continue;
			}

			VertexType& vertex_a = vertex_at(index_a);
			VertexType& vertex_b = vertex_at(index_b);
			VertexType& vertex_c = vertex_at(index_c);

			const Vec3f position_a(vertex_a.Position);
			const Vec3f position_b(vertex_b.Position);
			const Vec3f position_c(vertex_c.Position);

			const Vec3f normal = (position_c - position_a).Cross(position_b - position_a);

			for (VertexType* vertex : { &vertex_a, &vertex_b, &vertex_c }) {
				vertex->Normal[0] += normal.X;
				vertex->Normal[1] += normal.Y;
				vertex->Normal[2] += normal.Z;
			}
		}

		for (uint32 index = 0; index < num_vertices; index++) {
			VertexType& vertex = vertex_at(index);

			Vec3f normal(vertex.Normal);

			if (normal.Length() < 1e-12f) {
				normal = Vec3f(0.0f, 1.0f, 0.0f);
			}
			else {
				normal.NormalizeIP();
			}

			normal.CopyTo(vertex.Normal);
		}

		VertexList.bContainsNormals = true;
	}

	void Destroy()
	{
		if (bIsReference || !bIsReady.load()) {
			return;
		}

		bIsReady.store(false);

		VertexList.Destroy();

		GpuIndexBuffer.Destroy();
		LocalIndexBuffer.Free();
	}

	~PrimitiveMesh() { Destroy(); }


public:
	renderer::VertexList VertexList;

	std::atomic_bool bIsReady = { false };

	bool bIsReference = false;
	bool bKeepInMemory = false;

	renderer::GpuBuffer GpuIndexBuffer;
	SizedArray<uint32> LocalIndexBuffer;
};

} // namespace fx
