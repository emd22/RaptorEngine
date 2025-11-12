#include "FxAssetPaths.hpp"

#include <Core/FxFilesystemIO.hpp>

// TODO: Find asset path based on current dir
const char* FxAssetPath(FxAssetPathQuery query)
{
    switch (query) {
    case FxAssetPathQuery::eShaders:
        return "../Shaders/";
    case FxAssetPathQuery::eDataPacks:
        return "../build/";
    }
}
