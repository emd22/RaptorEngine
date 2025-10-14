#pragma once

#include "FxPhysicsObject.hpp"

// #include <ThirdParty/Jolt/Jolt.h>
// #include <ThirdParty/Jolt/Physics/Body/Body.h>
// #include <ThirdParty/Jolt/Physics/Body/BodyID.h>

#include <Core/MemPool/FxMPPagedArray.hpp>
#include <FxEntity.hpp>
#include <FxMaterial.hpp>
#include <FxPhysicsObject.hpp>


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
    FxRef<FxPrimitiveMesh<>> Mesh { nullptr };
    FxRef<FxMaterial> Material { nullptr };

    FxPagedArray<FxRef<FxObject>> AttachedNodes;

    FxVec3f Dimensions = FxVec3f::sZero;

    FxPhysicsObject Physics;

private:
    RxUniformBufferObject mUbo;

    bool mbReadyToRender : 1 = false;
    bool mbPhysicsEnabled : 1 = false;
};
