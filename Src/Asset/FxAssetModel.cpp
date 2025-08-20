#include "FxAssetModel.hpp"

bool FxAssetModel::CheckIfReady()
{
    if (mModelReady) {
        return mModelReady;
    }

    for (FxPrimitiveMesh<>* mesh : Meshes) {
        if (mesh->IsReady.load() == false) {
            return (mModelReady = false);
        }
    }

    if (Meshes.IsEmpty()) {
        return (mModelReady = false);
    }

    return (mModelReady = true);
}

void FxAssetModel::Render(RxGraphicsPipeline &pipeline)
{
    if (!CheckIfReady()) {
        return;
    }

    for (FxPrimitiveMesh<>* mesh : Meshes) {
        // if we know the model is not fully loaded, check each
        // individual mesh to see if they're ready.
        if (!mModelReady && !mesh->IsReady) {
            continue;
        }

        mesh->Render(pipeline);
    }
}
