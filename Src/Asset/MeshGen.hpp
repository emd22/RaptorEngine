#pragma once

#include <Core/Ref.hpp>
#include <Renderer/PrimitiveMesh.hpp>

namespace fx {


struct MeshGenOptions
{
    float32 Scale = 1;
};

class MeshGen
{
public:
    using PositionVertex = renderer::RxVertex<renderer::RxVertexType::eSlim>;

    struct GeneratedMesh
    {
        SizedArray<Vec3f> Positions;
        SizedArray<uint32> Indices;

        Ref<PrimitiveMesh> AsMesh(renderer::RxVertexType vertex_type);

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
    // static Ref<GeneratedMesh> MakeQuad();
    static Ref<GeneratedMesh> MakeCube(MeshGenOptions options = {});
    static Ref<GeneratedMesh> MakeQuad(MeshGenOptions options = {});

private:
};

} // namespace fx
