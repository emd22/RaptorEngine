#pragma once

#include <Math/Mat4.hpp>

#include <Renderer/FxMesh.hpp>
#include <Asset/FxMeshGen.hpp>

class FxCamera;

class FxLight
{
public:
    using VertexType = RvkVertex<FxVertexPosition>;

public:
    FxLight() = default;

    void SetLightVolume(const FxRef<FxMesh<VertexType>>& volume);
    void SetLightVolume(const FxRef<FxMeshGen::GeneratedMesh>& volume_gen, bool create_debug_mesh = false);

    void Render(const FxCamera& camera) const;
    void RenderDebugMesh(const FxCamera& camera) const;

public:
    Mat4f ModelMatrix = Mat4f::Identity;
    FxRef<FxMesh<VertexType>> LightVolume{nullptr};

    FxRef<FxMeshGen::GeneratedMesh> LightVolumeGen{nullptr};

private:
    FxRef<FxMesh<>> mDebugMesh{nullptr};

};
