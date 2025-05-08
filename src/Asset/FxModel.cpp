#include "FxModel.hpp"

bool FxModel::CheckIfReady()
{
    if (mModelReady) {
        return mModelReady;
    }

    for (FxMesh *mesh : Meshes) {
        if (mesh->IsReady.load() == false) {
            return (mModelReady = false);
        }
    }

    if (Meshes.IsEmpty()) {
        return (mModelReady = false);
    }

    return (mModelReady = true);
}

void FxModel::Render(GraphicsPipeline &pipeline)
{
    if (!CheckIfReady()) {
        return;
    }

    for (FxMesh *mesh : Meshes) {
        // if we know the model is not fully loaded, check each
        // individual mesh to see if they're ready.
        if (!mModelReady && !mesh->IsReady) {
            continue;
        }

        mesh->Render(pipeline);
    }
}
