#pragma once

#include <Core/Ref.hpp>
#include <Renderer/PrimitiveMesh.hpp>

namespace fx {


struct MeshGenOptions
{
    float32 Scale = 1.0f;
    Vec2f UvMin = Vec2f(0.0f, 0.0f);
    Vec2f UvMax = Vec2f(1.0f, 1.0f);
};

class MeshGen
{
public:
    using PositionVertex = renderer::Vertex<renderer::eVertexType::Slim>;

    struct GeneratedMesh
    {
        SizedArray<Vec3f> Positions;
        SizedArray<Vec3f> Normals;
        SizedArray<Vec2f> Uvs;

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
    static Ref<GeneratedMesh> MakeCube(MeshGenOptions options = {});
    static Ref<GeneratedMesh> MakeQuad(MeshGenOptions options = {});

private:
};

} // namespace fx
