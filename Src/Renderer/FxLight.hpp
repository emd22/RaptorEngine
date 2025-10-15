#pragma once

#include <Asset/FxMeshGen.hpp>
#include <FxColor.hpp>
#include <FxEntity.hpp>
#include <Math/Mat4.hpp>
#include <Renderer/FxPrimitiveMesh.hpp>

class FxCamera;

enum FxLightFlags : uint16
{
    FxLF_None = 0x0000,
    /**
     * @brief Sets whether the mesh position is separate from the light position sent to the shader.
     */
    FxLF_IndependentMeshPosition = 0x0001,
};

FX_DEFINE_ENUM_AS_FLAGS(FxLightFlags);

class FxLight : public FxEntity
{
public:
    using VertexType = RxVertex<FxVertexPosition>;

public:
    FxLight(FxLightFlags flags = FxLF_None) : Flags(flags) { this->Type = FxEntityType::Light; }

    void SetLightVolume(const FxRef<FxPrimitiveMesh<VertexType>>& volume);
    void SetLightVolume(const FxRef<FxMeshGen::GeneratedMesh>& volume_gen, bool create_debug_mesh = false);

    void Render(const FxCamera& camera);
    void RenderDebugMesh(const FxCamera& camera);

    void MoveTo(const FxVec3f& position) override;
    void MoveTo(const FxVec3f& position, bool force_move_mesh);

    void MoveBy(const FxVec3f& offset) override;
    void MoveBy(const FxVec3f& position, bool force_move_mesh);

    void Scale(const FxVec3f& scale) override;

    // Force move the mesh
    void MoveLightVolumeTo(const FxVec3f& position);

public:
    FxRef<FxPrimitiveMesh<VertexType>> LightVolume { nullptr };
    FxRef<FxMeshGen::GeneratedMesh> LightVolumeGen { nullptr };

    FxColor Color = FxColor::sWhite;

    float Radius = 2.0f;

    FxLightFlags Flags = FxLF_None;

private:
    FxRef<FxPrimitiveMesh<>> mDebugMesh { nullptr };

protected:
    FxVec3f mLightPosition = FxVec3f::sZero;

    RxGraphicsPipeline* mpLightPipeline = nullptr;
};


class FxPointLight : public FxLight
{
};

class FxDirectionalLight : public FxLight
{
public:
    FxDirectionalLight()
    {
        this->Radius = 0.0f;
        this->Flags |= FxLF_IndependentMeshPosition;
    }
};
