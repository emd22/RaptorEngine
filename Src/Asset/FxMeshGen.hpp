#pragma once

#include <Core/FxRef.hpp>
#include <Renderer/FxPrimitiveMesh.hpp>

class FxMeshGen
{
public:
    using LightVolumeVertex = RxVertex<FxVertexPosition>;

    struct GeneratedMesh
    {
        FxSizedArray<FxVec3f> Positions;
        FxSizedArray<uint32> Indices;

        FxRef<FxPrimitiveMesh<LightVolumeVertex>> AsLightVolume();
        FxRef<FxPrimitiveMesh<>> AsMesh();

        void Destroy()
        {
        }
    };

public:
    static FxRef<GeneratedMesh> MakeIcoSphere(int resolution);
    static FxRef<GeneratedMesh> MakeQuad();

private:
};
