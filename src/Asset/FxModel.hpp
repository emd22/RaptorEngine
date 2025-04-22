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
    bool IsReady();

public:
    // StaticArray<float> Positions;
    // StaticArray<float> Normals;

    StaticPtrArray<FxMesh> Meshes;

private:
    bool mModelReady = false;
    // FxMesh *mMesh = nullptr;
};
