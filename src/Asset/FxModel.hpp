#pragma once

#include "FxBaseAsset.hpp"

#include <Renderer/FxMesh.hpp>

#include <Core/FxStaticPtrArray.hpp>


class FxModel : public FxBaseAsset
{
protected:
public:
    FxModel()
    {
    }

    friend class FxAssetManager;

public:
    void Render(RvkGraphicsPipeline &pipeline);
    bool CheckIfReady();

    ~FxModel() override
    {
        puts("WHERE ME?????");
        Destroy();
    }

// private:
    void Destroy() override
    {
        Log::Info("Destroy FxModel (%lu meshes)", Meshes.Size);

        for (FxMesh *mesh : Meshes) {
            mesh->Destroy();
        }

        Meshes.Free();

        mModelReady = false;
        IsUploadedToGpu = false;
    }

public:
    FxStaticPtrArray<FxMesh> Meshes;

private:
    bool mModelReady = false;
};
