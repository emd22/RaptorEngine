#pragma once

#include "FxAssetBase.hpp"

#include <Core/FxRef.hpp>
#include <Core/FxStaticPtrArray.hpp>
#include <FxMaterial.hpp>
#include <Renderer/FxPrimitiveMesh.hpp>


class FxAssetModel : public FxAssetBase
{
protected:
public:
    FxAssetModel() = default;

    friend class FxAssetManager;

public:
    void Render(RxGraphicsPipeline& pipeline);
    bool CheckIfReady();

    ~FxAssetModel() override { Destroy(); }

    // private:
    void Destroy() override
    {
        OldLog::Info("Destroy FxAssetModel (%lu meshes)", Meshes.Size);

        for (FxPrimitiveMesh<>* mesh : Meshes) {
            mesh->Destroy();
        }

        Meshes.Free();

        // Delete the materials
        for (FxRef<FxMaterial>& mat : Materials) {
            mat->Destroy();
        }

        Materials.clear();

        mModelReady = false;
        IsUploadedToGpu = false;
    }

public:
    FxStaticPtrArray<FxPrimitiveMesh<>> Meshes;
    std::vector<FxRef<FxMaterial>> Materials;

private:
    bool mModelReady = false;
};
