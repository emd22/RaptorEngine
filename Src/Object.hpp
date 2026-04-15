#pragma once

#include "Physics/PhObject.hpp"

// #include <ThirdParty/Jolt/Jolt.h>
// #include <ThirdParty/Jolt/Physics/Body/Body.h>
// #include <ThirdParty/Jolt/Physics/Body/BodyID.h>

#include <Asset/Animation.hpp>
#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Ref.hpp>
#include <Core/TSRef.hpp>
#include <Entity.hpp>
#include <Material.hpp>
#include <ObjectManager.hpp>

namespace fx {

class PrimitiveMesh;

class Object : public AxBase, public Entity
{
    friend class AxLoaderGltf;
    friend class AxManager;

public:
    static const ::fx::ObjectId sNone = UINT32_MAX;

    static constexpr EntityType scEntityType = EntityType::eObject;

public:
    Object();

    void MakeInstanceOf(const TSRef<Object>& source_ref);

    void Create(const Ref<PrimitiveMesh>& mesh, const TSRef<Material>& material);

    /**
     * @brief Render only the primitive(s) for the objects. Does not bind material or other object data.
     */
    void RenderPrimitive(const renderer::RxCommandBuffer& cmd);
    void Render(const Camera& camera);
    void RenderUnlit(const Camera& camera);

    bool CheckIfReady();

    void AttachObject(const TSRef<Object>& object);
    void Update();

    void OnAttached(Scene* scene) override;

    void SetGraphicsPipeline(renderer::RxPipeline* pipeline, bool update_children = true);

    void PhysicsCreatePrimitive(PhPrimitiveType primitive_type, const Vec3f& dimensions, PhMotionType motion_type,
                                const PhProperties& physics_properties);

    void PhysicsCreateMesh(Ref<PrimitiveMesh> physics_mesh, PhMotionType motion_type,
                           const PhProperties& physics_properties);

    void PrintDebug() const;

    // XXX: TEMP
    void UpdateAnimation();

    /**
     * @brief Reserve `num_instances` amount of future instances in the object manager.
     * @note This may update the object id if there are not enough free slots following this object.
     */
    void ReserveInstances(uint32 num_instances);

    void SetPhysicsEnabled(bool enabled);
    FX_FORCE_INLINE bool GetPhysicsEnabled() { return mbPhysicsEnabled; }

    FX_FORCE_INLINE void SetObjectLayer(ObjectLayer layer) { mObjectLayer = layer; }
    FX_FORCE_INLINE ObjectLayer GetObjectLayer() const { return mObjectLayer; }

    FX_FORCE_INLINE void SetShadowCaster(const bool value) { mbIsShadowCaster = value; }
    FX_FORCE_INLINE bool IsShadowCaster() const { return mbIsShadowCaster; }

    void SetRenderUnlit(const bool value);
    FX_FORCE_INLINE bool GetRenderUnlit() const { return mbRenderUnlit; }

    FX_FORCE_INLINE bool IsSkinned() const { return (pMesh != nullptr) && pMesh->VertexList.IsSkinned(); }

    void Destroy() override;
    ~Object();

private:
    void RenderMesh();

    void SyncObjectWithPhysics();

public:
    Ref<PrimitiveMesh> pMesh { nullptr };
    TSRef<Material> pMaterial { nullptr };

    Ref<Skeleton> pSkeleton { nullptr };
    SizedArray<Animation> Animations;
    Animation* pCurrentAnimation = nullptr;
    float32 AnimationTime = 0.0f;

    PagedArray<TSRef<Object>> AttachedNodes;

    Vec3f Dimensions = Vec3f::sZero;
    PhObject Physics;

private:
    /// Object slots allocated following this object. Used by other instances of this object.
    uint16 mInstanceSlots = 0;
    uint16 mInstanceSlotsInUse = 0;

    TSRef<Object> mpInstanceSource { nullptr };

    bool mbReadyToRender : 1 = false;
    bool mbPhysicsEnabled : 1 = false;
    bool mbIsInstance : 1 = false;
    bool mbIsShadowCaster : 1 = false;
    bool mbRenderUnlit : 1 = false;

    ObjectLayer mObjectLayer = ObjectLayer::eWorldLayer;
};


FX_VALIDATE_ENTITY_TYPE(Object)

} // namespace fx
