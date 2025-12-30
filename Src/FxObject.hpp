#pragma once

#include "Physics/PhObject.hpp"

// #include <ThirdParty/Jolt/Jolt.h>
// #include <ThirdParty/Jolt/Physics/Body/Body.h>
// #include <ThirdParty/Jolt/Physics/Body/BodyID.h>

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

    void Create(const FxRef<FxPrimitiveMesh<>>& mesh, const FxRef<FxMaterial>& material);

    void Render(const FxCamera& camera);
    bool CheckIfReady();

    void AttachObject(const FxRef<FxObject>& object);
    void Update();

    void OnAttached(FxScene* scene) override;

    void SetGraphicsPipeline(RxGraphicsPipeline* pipeline, bool update_children = true);

    void PhysicsCreatePrimitive(PhPrimitiveType primitive_type, const FxVec3f& dimensions, PhMotionType motion_type,
                                const PhProperties& physics_properties);

    void PhysicsCreateMesh(const FxPrimitiveMesh<>& physics_mesh, PhMotionType motion_type,
                           const PhProperties& physics_properties);

    void SetPhysicsEnabled(bool enabled);
    FX_FORCE_INLINE bool GetPhysicsEnabled() { return mbPhysicsEnabled; }

    FX_FORCE_INLINE void SetObjectLayer(FxObjectLayer layer) { mObjectLayer = layer; }
    FX_FORCE_INLINE FxObjectLayer GetObjectLayer() const { return mObjectLayer; }

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

private:
    RxUniformBufferObject mUbo;

    bool mbReadyToRender : 1 = false;
    bool mbPhysicsEnabled : 1 = false;

    FxObjectLayer mObjectLayer = FxObjectLayer::eWorldLayer;
};


FX_VALIDATE_ENTITY_TYPE(FxObject)
