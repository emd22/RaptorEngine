#pragma once

#include "Physics/PhObject.hpp"

// #include <ThirdParty/Jolt/Jolt.h>
// #include <ThirdParty/Jolt/Physics/Body/Body.h>
// #include <ThirdParty/Jolt/Physics/Body/BodyID.h>

#include <Asset/Animation.hpp>
#include <Asset/AssetObject.hpp>
#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Ref.hpp>
#include <Core/TSRef.hpp>
#include <Entity.hpp>
#include <Material/MaterialID.hpp>
#include <Script/FoxScript.hpp>


namespace fx {

namespace renderer {
class Pipeline;
};

enum class eObjectFlags : uint16
{
    None = 0,
    ReadyToRender = (1 << 0),
    PhysicsEnabled = (1 << 1),
    IsInstance = (1 << 2),
    ShadowCaster = (1 << 3),
    Unlit = (1 << 4),
};

FxEnumFlags(eObjectFlags);


class PrimitiveMesh;

class Object : public AssetObject, public Entity
{
    friend class LoaderGltf;
    friend class AxManager;

public:
    static constexpr eEntityType scEntityType = eEntityType::Object;

public:
    Object() = default;
    Object(const ObjectID& id);

    void MakeInstanceOf(const ObjectID& source);

    void Create(const Ref<PrimitiveMesh>& mesh, const MaterialID& material);

    /**
     * @brief Render only the primitive(s) for the objects. Does not bind material or other object data.
     */
    void RenderPrimitive(const renderer::CommandBuffer& cmd);
    void Render(const Camera& camera);
    void RenderShallow(const Camera& camera);
    void RenderUnlit(const Camera& camera);

    bool CheckIfReady(bool require_material);


    void AttachObject(const ObjectID& object);

    void Update();

    void OnAttached(Scene* scene) override;

    void SetGraphicsPipeline(renderer::Pipeline* pipeline, bool update_children = true);

    void PhysicsCreatePrimitive(ePhPrimitiveType primitive_type, const Vec3f& dimensions, ePhMotionType motion_type,
                                const PhProperties& physics_properties);

    void PhysicsCreateMesh(Ref<PrimitiveMesh> physics_mesh, ePhMotionType motion_type,
                           const PhProperties& physics_properties);

    void PrintDebug() const;

    // XXX: TEMP
    void UpdateAnimation();

    /**
     * @brief Reserve `num_instances` amount of future instances in the object manager.
     * @note This may update the object id if there are not enough free slots following this object.
     */
    void ReserveInstances(uint32 num_instances);

    /////////////////////////////////////
    // Script
    /////////////////////////////////////

    void AttachScript(const Ref<script::FoxScript>& script);
    void LoadScript(const String& path);

    /////////////////////////////////////
    // Material
    /////////////////////////////////////

    /**
     * @brief Returns the ID of the material assigned to this object.
     */
    FX_FORCE_INLINE const MaterialID& GetMaterialID() const { return mMaterialID; };

    /**
     * @brief Sets the material ID for the object.
     */
    FX_FORCE_INLINE void SetMaterialID(const MaterialID& id) { mMaterialID = id; };

    /////////////////////////////////////
    // Physics
    /////////////////////////////////////

    FX_FORCE_INLINE void SetPhysicsId(PhObjectId phys_id) { PhysicsId = phys_id; }
    FX_FORCE_INLINE PhObjectId GetPhysicsId() const { return PhysicsId; }
    void SetPhysicsEnabled(bool enabled);
    FX_FORCE_INLINE bool GetPhysicsEnabled() { return (Flags & eObjectFlags::PhysicsEnabled) != 0; }

    FX_FORCE_INLINE void SetObjectLayer(eObjectLayer layer) { mObjectLayer = layer; }
    FX_FORCE_INLINE eObjectLayer GetObjectLayer() const { return mObjectLayer; }

    /////////////////////////////////////
    // Render options
    /////////////////////////////////////

    FX_FORCE_INLINE void SetShadowCaster(const bool value)
    {
        if (value) {
            Flags |= eObjectFlags::ShadowCaster;
        }
        else {
            Flags &= ~(eObjectFlags::ShadowCaster);
        }
    }

    FX_FORCE_INLINE bool IsShadowCaster() const { return (Flags & eObjectFlags::ShadowCaster) != 0; }

    void SetRenderUnlit(const bool value);
    FX_FORCE_INLINE bool GetRenderUnlit() const { return (Flags & eObjectFlags::Unlit) != 0; }

    FX_FORCE_INLINE bool IsSkinned() const { return (pMesh != nullptr) && pMesh->VertexList.IsSkinned(); }

    void MarkReadyToRender()
    {
        Flags |= eObjectFlags::ReadyToRender;

        bIsUploadedToGpu = true;
        IsFinishedNotifier.Signal();
    }

    void Destroy() override;
    ~Object();

private:
    void RenderMesh();
    void SetScriptVars();

    void SyncObjectWithPhysics(PhObject* phys);

public:
    Ref<PrimitiveMesh> pMesh { nullptr };
    PhObjectId PhysicsId = PhObjectIdNull;

    Ref<Skeleton> pSkeleton { nullptr };
    SizedArray<Animation> Animations;
    Animation* pCurrentAnimation = nullptr;
    float32 AnimationTime = 0.0f;

    Scene* pScene = nullptr;

    PagedArray<ObjectID> AttachedNodes;

    Vec3f Dimensions = Vec3f::sZero;

    Ref<script::FoxScript> pScript { nullptr };

    MaterialID mMaterialID = MaterialID::Null;

private:
    /// Object slots allocated following this object. Used by other instances of this object.
    uint16 mInstanceSlots = 0;
    uint16 mInstanceSlotsInUse = 0;
    eObjectFlags Flags = eObjectFlags::None;

    eObjectLayer mObjectLayer = eObjectLayer::WorldLayer;
};


FX_VALIDATE_ENTITY_TYPE(Object);

} // namespace fx
