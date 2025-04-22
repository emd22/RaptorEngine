#pragma once

#include "FxBaseAsset.hpp"
#include "Loader/FxBaseLoader.hpp"

#include <string>

enum class FxAssetType
{
    None,
    Binary,
    Model,
    Texture,
};

struct FxAssetQueueItem
{
    std::string Path;
    std::unique_ptr<FxBaseLoader> Loader;

    FxBaseAsset *Asset;
    FxAssetType AssetType;
};
