#pragma once

#include <Core/Ref.hpp>
#include <Renderer/PrimitiveMesh.hpp>

namespace fx {


struct MeshGenOptions
{
	float32 Scale = 1.0f;
	FVec2 UvMin = FVec2(0.0f, 0.0f);
	FVec2 UvMax = FVec2(0.5f, 0.5f);
};

struct CubeGenOptions
{
	static CubeGenOptions Uniform(float32 scale)
	{
		return CubeGenOptions({
			.Left = { .Scale = scale },
			.Right = { .Scale = scale },
			.Top = { .Scale = scale },
			.Bottom = { .Scale = scale },
			.Front = { .Scale = scale },
			.Back = { .Scale = scale },

			.bAlignUVs = true,
		});
	}

	MeshGenOptions Left {};
	MeshGenOptions Right {};
	MeshGenOptions Top {};
	MeshGenOptions Bottom {};
	MeshGenOptions Front {};
	MeshGenOptions Back {};


	bool bAlignUVs = false;
};

class MeshGen
{
public:
	using PositionVertex = renderer::Vertex<renderer::eVertexType::Slim>;

	struct GeneratedMesh
	{
		SizedArray<FVec3> Positions;
		SizedArray<FVec3> Normals;
		/// Optional, only filled by generators that lay out UVs (cube, quad)
		SizedArray<FVec3> Tangents;
		/// One per vertex, -1 where the face's UVs are mirrored. Empty means +1 everywhere
		SizedArray<float32> TangentHandedness;
		SizedArray<FVec2> Texcoords;

		SizedArray<uint32> Indices;

		Ref<PrimitiveMesh> AsMesh(renderer::eVertexType vertex_type);

		Ref<PrimitiveMesh> AsSlimMesh();
		Ref<PrimitiveMesh> AsDefaultMesh();

		void Destroy()
		{
			Positions.Free();
			Indices.Free();
		}

		~GeneratedMesh() { Destroy(); }

	private:
	};

public:
	static Ref<GeneratedMesh> MakeIcoSphere(int resolution);
	static Ref<GeneratedMesh> MakeCube(CubeGenOptions options = {});

	static Ref<GeneratedMesh> MakeWireframeBox();
	/// A single segment from the origin to +1 on X, as a line list
	static Ref<GeneratedMesh> MakeLine();
	static Ref<GeneratedMesh> MakeQuad(FVec2 scale = FVec2(1.0f, 1.0f));

private:
};

} // namespace fx
