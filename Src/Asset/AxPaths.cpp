#include "AxPaths.hpp"

#include <Core/FxFilesystemIO.hpp>

// TODO: Find asset path based on current dir
const char* FxAssetPath(AxPathQuery query)
{
    switch (query) {
    case FxAssetPathQuery::eShaders:
        return FX_BASE_DIR "/Shaders/";
    case FxAssetPathQuery::eDataPacks:
        return FX_BASE_DIR "/build/";
    }
}
