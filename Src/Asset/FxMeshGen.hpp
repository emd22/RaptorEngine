#pragma once

#include <Core/FxRef.hpp>
#include <Renderer/FxPrimitiveMesh.hpp>

struct FxMeshGenOptions
{
    float Scale = 1;
};

class FxMeshGen
{
public:
    using PositionVertex = RxVertex<FxVertexPosition>;

    struct GeneratedMesh
    {
        FxSizedArray<FxVec3f> Positions;
        FxSizedArray<uint32> Indices;

        FxRef<FxPrimitiveMesh<PositionVertex>> AsPositionsMesh();
        FxRef<FxPrimitiveMesh<>> AsMesh();

        void Destroy()
        {
            Positions.Free();
            Indices.Free();
        }

        ~GeneratedMesh() { Destroy(); }
    };

public:
    static FxRef<GeneratedMesh> MakeIcoSphere(int resolution);
    // static FxRef<GeneratedMesh> MakeQuad();
    static FxRef<GeneratedMesh> MakeCube(FxMeshGenOptions options = {});
    static FxRef<GeneratedMesh> MakeQuad(FxMeshGenOptions options = {});

private:
};
