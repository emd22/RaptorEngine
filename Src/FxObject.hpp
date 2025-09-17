#pragma once

#include <ThirdParty/Jolt/Jolt.h>
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
    FxObject() = default;

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


    void AttachObject(FxRef<FxObject>& object)
    {
        if (AttachedNodes.IsEmpty()) {
            AttachedNodes.Create(32);
        }
        AttachedNodes.Insert(object);
    }

private:
    void RenderMesh();

public:
    FxRef<FxPrimitiveMesh<>> Mesh { nullptr };
    FxRef<FxMaterial> Material { nullptr };

    FxMPPagedArray<FxRef<FxObject>> AttachedNodes;

    JPH::BodyID JoltBodyID {};

private:
    bool mReadyToRender = false;

    RxUniformBufferObject mUbo;
};
