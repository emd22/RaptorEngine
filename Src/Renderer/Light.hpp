#pragma once

#include <Asset/MeshGen.hpp>
#include <Color.hpp>
#include <Entity.hpp>
#include <Math/Mat4.hpp>
#include <Renderer/PrimitiveMesh.hpp>

namespace fx {
class Camera;

namespace renderer {
class RxPipeline;
} // namespace renderer

enum LightFlags : uint16
{
    LF_None = 0x0000,
};

enum class LightType
{
    eUnknown,
    eDirectional,
    ePoint,
};

FX_DEFINE_ENUM_AS_FLAGS(LightFlags);

class LightBase : public Entity
{
public:
    using VertexType = renderer::RxVertex<renderer::RxVertexType::eSlim>;

    static constexpr EntityType scEntityType = EntityType::eLight;

public:
    LightBase(LightFlags flags = LF_None);

    void SetLightVolume(const Ref<PrimitiveMesh>& volume);
    void SetLightVolume(const Ref<MeshGen::GeneratedMesh>& volume_gen, bool create_debug_mesh = false);

    void SetRadius(const float radius);

    virtual void Render(const PerspectiveCamera& camera, Camera* shadow_camera);
    virtual void RenderDebugMesh(const PerspectiveCamera& camera);

    virtual ~LightBase() {}

public:
    Ref<PrimitiveMesh> pLightVolume { nullptr };
    Ref<MeshGen::GeneratedMesh> pLightVolumeGen { nullptr };

    renderer::RxPipeline* pPipelineInside = nullptr;
    renderer::RxPipeline* pPipelineOutside = nullptr;

    struct Color Color = Color::sWhite;
    struct Color AmbientColor { 0x101f1f1f };

    LightFlags Flags = LF_None;

    LightType Type = LightType::eUnknown;

    bool bEnabled = true;

protected:
    Ref<PrimitiveMesh> mpDebugMesh { nullptr };
    renderer::RxPipeline* pPipeline = nullptr;

    float32 mRadius = 1.0f;
};


/**
 * @brief
 */
class LightPoint : public LightBase
{
public:
    LightPoint();
};

/**
 * @brief
 */
class LightDirectional : public LightBase
{
public:
    LightDirectional();

    void Render(const PerspectiveCamera& camera, Camera* shadow_camera) override;
};

////////////////////////////////////
// Entity Validations
////////////////////////////////////

FX_VALIDATE_ENTITY_TYPE(LightBase)

} // namespace fx
