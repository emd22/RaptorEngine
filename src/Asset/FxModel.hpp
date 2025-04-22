#pragma once

#include "FxBaseAsset.hpp"

#include <Renderer/FxMesh.hpp>

#include <Core/StaticPtrArray.hpp>


class FxModel : public FxBaseAsset
{
protected:
public:
    FxModel()
    {
    }

    friend class FxAssetManager;

public:
    void Render();
    bool CheckIfReady();

    ~FxModel() override
    {
        Destroy();
    }

// private:
    void Destroy() override
    {
        Log::Info("Destroy FxModel (%lu meshes)", Meshes.Size);
        for (FxMesh *mesh : Meshes)
        {
            Log::Info("Destroy mesh");
            mesh->Destroy();
        }

        // Meshes.Free();
    }

public:
    // StaticArray<float> Positions;
    // StaticArray<float> Normals;

    StaticPtrArray<FxMesh> Meshes;

private:
    bool mModelReady = false;
    // FxMesh *mMesh = nullptr;
};
