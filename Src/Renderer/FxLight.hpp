#pragma once

#include <Math/Mat4.hpp>

#include <Renderer/FxMesh.hpp>

class FxCamera;

class FxLight
{
public:
    using VertexType = RvkVertex<FxVertexPosition>;

public:
    FxLight() = default;

    void SetLightVolume(const FxRef<FxMesh<VertexType>>& volume);
    void Render(const FxCamera& camera);

public:
    Mat4f ModelMatrix;
    FxRef<FxMesh<VertexType>> LightVolume{nullptr};
};
