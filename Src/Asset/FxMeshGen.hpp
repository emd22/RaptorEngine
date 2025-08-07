#pragma once

#include <Renderer/FxPrimitiveMesh.hpp>
#include <Core/FxRef.hpp>

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
            // Positions.Free();
            // Indices.Free();
        }
    };

public:
    static FxRef<GeneratedMesh> MakeIcoSphere(int resolution);

private:
};
