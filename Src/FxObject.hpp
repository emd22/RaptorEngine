#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/Body.h>
#include <ThirdParty/Jolt/Physics/Body/BodyID.h>

#include <Core/MemPool/FxMPPagedArray.hpp>
#include <FxEntity.hpp>
#include <FxMaterial.hpp>
#include <Renderer/FxPrimitiveMesh.hpp>


class FxObject : public FxAssetBase, public FxEntity
{
    friend class FxLoaderGltf;
    friend class FxAssetManager;

public:
    enum PhysicsFlags
    {
        PF_CreateInactive = 0x01,
    };

    enum class PhysicsType
    {
        Static,
        Dynamic,
    };

public:
    FxObject() = default;

    void Create(const FxRef<FxPrimitiveMesh<>>& mesh, const FxRef<FxMaterial>& material);

    void Render(const FxCamera& camera);
    bool CheckIfReady();

    ~FxObject() override { Destroy(); }

    void Destroy() override
    {
        if (Mesh) {
            Mesh->Destroy();
        }
        if (Material) {
            Material->Destroy();
        }

        if (!AttachedNodes.IsEmpty()) {
            for (FxRef<FxObject>& obj : AttachedNodes) {
                obj->Destroy();
            }
        }

        mReadyToRender = false;
        IsUploadedToGpu = false;
    }


    void AttachObject(const FxRef<FxObject>& object)
    {
        if (AttachedNodes.IsEmpty()) {
            AttachedNodes.Create(32);
        }

        Dimensions += object->Dimensions;

        AttachedNodes.Insert(object);
    }


    void CreatePhysicsBody(PhysicsFlags flags, PhysicsType type);
    void DestroyPhysicsBody();

    inline JPH::Body* GetPhysicsBody() { return mpPhysicsBody; };
    inline const JPH::BodyID& GetPhysicsBodyId() { return mpPhysicsBody->GetID(); };

    void Update();

private:
    void RenderMesh();

public:
    FxRef<FxPrimitiveMesh<>> Mesh { nullptr };
    FxRef<FxMaterial> Material { nullptr };

    FxPagedArray<FxRef<FxObject>> AttachedNodes;

    FxVec3f Dimensions = FxVec3f::Zero;

private:
    bool mReadyToRender = false;

    RxUniformBufferObject mUbo;

    JPH::Body* mpPhysicsBody = nullptr;

    bool mbPhysicsEnabled = false;
    bool mbHasPhysicsBody = false;
};
