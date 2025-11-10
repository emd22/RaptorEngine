#pragma once

#include "Physics/FxPhysicsObject.hpp"

// #include <ThirdParty/Jolt/Jolt.h>
// #include <ThirdParty/Jolt/Physics/Body/Body.h>
// #include <ThirdParty/Jolt/Physics/Body/BodyID.h>

#include <Core/MemPool/FxMPPagedArray.hpp>
#include <FxEntity.hpp>
#include <FxMaterial.hpp>


class FxObject : public FxAssetBase, public FxEntity
{
    friend class FxLoaderGltf;
    friend class FxAssetManager;

public:
    FxObject() { this->Type = FxEntityType::Object; }

    void Create(const FxRef<FxPrimitiveMesh<>>& mesh, const FxRef<FxMaterial>& material);

    void Render(const FxCamera& camera);
    bool CheckIfReady();

    void AttachObject(const FxRef<FxObject>& object);
    void Update();

    void SetGraphicsPipeline(RxGraphicsPipeline* pipeline, bool update_children = true);

    void SetPhysicsEnabled(bool enabled);
    FX_FORCE_INLINE bool GetPhysicsEnabled() { return mbPhysicsEnabled; }

    void PhysicsObjectCreate(FxPhysicsObject::PhysicsFlags flags, FxPhysicsObject::PhysicsType type,
                             const FxPhysicsProperties& properties);

    void Destroy() override;
    ~FxObject() override { Destroy(); }

private:
    void RenderMesh();

    void SyncObjectWithPhysics();

public:
    FxRef<FxPrimitiveMesh<>> pMesh { nullptr };
    FxRef<FxMaterial> pMaterial { nullptr };

    FxPagedArray<FxRef<FxObject>> AttachedNodes;

    FxVec3f Dimensions = FxVec3f::sZero;

    FxPhysicsObject Physics;

private:
    RxUniformBufferObject mUbo;

    bool mbReadyToRender : 1 = false;
    bool mbPhysicsEnabled : 1 = false;
};
