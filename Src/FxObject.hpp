#pragma once

#include "Physics/PhObject.hpp"

// #include <ThirdParty/Jolt/Jolt.h>
// #include <ThirdParty/Jolt/Physics/Body/Body.h>
// #include <ThirdParty/Jolt/Physics/Body/BodyID.h>

#include <Core/FxName.hpp>
#include <Core/MemPool/FxMPPagedArray.hpp>
#include <FxEntity.hpp>
#include <FxMaterial.hpp>
#include <FxObjectManager.hpp>

template <typename T>
class FxPrimitiveMesh;

class FxObject : public AxBase, public FxEntity
{
    friend class AxLoaderGltf;
    friend class AxManager;

public:
    static const FxObjectId sNone = UINT32_MAX;

    static constexpr FxEntityType scEntityType = FxEntityType::eObject;

public:
    FxObject();

    void MakeInstanceOf(const FxRef<FxObject>& source_ref);

    void Create(const FxRef<FxPrimitiveMesh<>>& mesh, const FxRef<FxMaterial>& material);

    /**
     * @brief Render only the primitive(s) for the objects. Does not bind material or other object data.
     */
    void RenderPrimitive(const RxCommandBuffer& cmd);
    void Render(const FxCamera& camera);
    void RenderUnlit(const FxCamera& camera);

    bool CheckIfReady();

    void AttachObject(const FxRef<FxObject>& object);
    void Update();

    void OnAttached(FxScene* scene) override;

    void SetGraphicsPipeline(RxPipeline* pipeline, bool update_children = true);

    void PhysicsCreatePrimitive(PhPrimitiveType primitive_type, const FxVec3f& dimensions, PhMotionType motion_type,
                                const PhProperties& physics_properties);

    void PhysicsCreateMesh(const FxRef<FxPrimitiveMesh<>>& physics_mesh, PhMotionType motion_type,
                           const PhProperties& physics_properties);

    /**
     * @brief Reserve `num_instances` amount of future instances in the object manager.
     * @note This may update the object id if there are not enough free slots following this object.
     */
    void ReserveInstances(uint32 num_instances);

    void SetPhysicsEnabled(bool enabled);
    FX_FORCE_INLINE bool GetPhysicsEnabled() { return mbPhysicsEnabled; }

    FX_FORCE_INLINE void SetObjectLayer(FxObjectLayer layer) { mObjectLayer = layer; }
    FX_FORCE_INLINE FxObjectLayer GetObjectLayer() const { return mObjectLayer; }

    FX_FORCE_INLINE void SetShadowCaster(const bool value) { mbIsShadowCaster = value; }
    FX_FORCE_INLINE bool IsShadowCaster() const { return mbIsShadowCaster; }

    void SetRenderUnlit(const bool value);
    FX_FORCE_INLINE bool GetRenderUnlit() const { return mbRenderUnlit; }

    void Destroy() override;
    ~FxObject();

private:
    void RenderMesh();

    void SyncObjectWithPhysics();

public:
    FxRef<FxPrimitiveMesh<>> pMesh { nullptr };
    FxRef<FxMaterial> pMaterial { nullptr };

    FxPagedArray<FxRef<FxObject>> AttachedNodes;

    FxVec3f Dimensions = FxVec3f::sZero;

    PhObject Physics;

    FxName Name;

private:
    /// Object slots allocated following this object. Used by other instances of this object.
    uint16 mInstanceSlots = 0;
    uint16 mInstanceSlotsInUse = 0;

    FxRef<FxObject> mpInstanceSource { nullptr };

    bool mbReadyToRender : 1 = false;
    bool mbPhysicsEnabled : 1 = false;
    bool mbIsInstance : 1 = false;
    bool mbIsShadowCaster : 1 = false;
    bool mbRenderUnlit : 1 = false;

    FxObjectLayer mObjectLayer = FxObjectLayer::eWorldLayer;
};


FX_VALIDATE_ENTITY_TYPE(FxObject)
