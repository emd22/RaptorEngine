#pragma once

#include "FxBaseAsset.hpp"

#include <Renderer/FxMesh.hpp>
#include <Core/FxRef.hpp>
#include <FxMaterial.hpp>

#include <Core/FxStaticPtrArray.hpp>


class FxAssetModel : public FxBaseAsset
{
protected:
public:
    FxAssetModel() = default;

    friend class FxAssetManager;

public:
    void Render(RvkGraphicsPipeline &pipeline);
    bool CheckIfReady();

    ~FxAssetModel() override
    {
        Destroy();
    }

// private:
    void Destroy() override
    {
        Log::Info("Destroy FxAssetModel (%lu meshes)", Meshes.Size);

        for (FxMesh<>* mesh : Meshes) {
            mesh->Destroy();
        }

        Meshes.Free();

        mModelReady = false;
        IsUploadedToGpu = false;
    }

public:
    FxStaticPtrArray<FxMesh<>> Meshes;
    std::vector<FxRef<FxMaterial>> Materials;

private:
    bool mModelReady = false;
};
