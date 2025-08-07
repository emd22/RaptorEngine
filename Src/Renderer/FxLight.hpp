#pragma once

#include <Math/Mat4.hpp>

#include <Renderer/FxPrimitiveMesh.hpp>
#include <Asset/FxMeshGen.hpp>

#include <FxEntity.hpp>

class FxCamera;

class FxLight : public FxEntity
{
public:
    using VertexType = RxVertex<FxVertexPosition>;

public:
    FxLight() = default;

    void SetLightVolume(const FxRef<FxPrimitiveMesh<VertexType>>& volume);
    void SetLightVolume(const FxRef<FxMeshGen::GeneratedMesh>& volume_gen, bool create_debug_mesh = false);

    void Render(const FxCamera& camera) const;
    void RenderDebugMesh(const FxCamera& camera);

public:
    FxRef<FxPrimitiveMesh<VertexType>> LightVolume{nullptr};
    FxRef<FxMeshGen::GeneratedMesh> LightVolumeGen{nullptr};

    FxVec3f Color = FxVec3f::One;

private:
    FxRef<FxPrimitiveMesh<>> mDebugMesh{nullptr};
};
