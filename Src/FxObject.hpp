#pragma once

#include <Renderer/FxPrimitiveMesh.hpp>
#include <FxMaterial.hpp>
#include <FxEntity.hpp>

#include <Core/MemPool/FxMPPagedArray.hpp>

class FxObject : public FxAssetBase, public FxEntity
{
    friend class FxLoaderGltf;
    friend class FxAssetManager;
public:
    FxObject()
    {
        
            static int creates = 0;
            creates++;
        Log::Info("Create FxObject %d", creates);
    };

    void Render(const FxCamera& camera);
    bool CheckIfReady();

    ~FxObject() override
    {
        Destroy();
    }

    void Destroy() override
    {
        static int destroys = 0;
        destroys++;
        Log::Info("Destroy FxObject %d", destroys);

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
    FxRef<FxPrimitiveMesh<>> Mesh{nullptr};
    FxRef<FxMaterial> Material{nullptr};

    FxMPPagedArray<FxRef<FxObject>> AttachedNodes;

private:
    bool mReadyToRender = false;

    RxUniformBufferObject mUbo;
};
