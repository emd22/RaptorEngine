#include "FxModel.hpp"

bool FxModel::IsReady()
{
    if (mModelReady) {
        return mModelReady;
    }

    for (FxMesh *mesh : Meshes) {
        if (!mesh->IsReady()) {
            return false;
        }
    }

    return (mModelReady = true);
}

void FxModel::Render()
{
    mModelReady = IsReady();

    for (FxMesh *mesh : Meshes) {
        // if we know the model is not fully loaded, check for each
        // individual mesh.
        if (!mModelReady && !mesh->IsReady()) {
            continue;
        }

        mesh->Render();
    }
}
