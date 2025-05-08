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
    FxAssetQueueItem() = default;
    FxAssetQueueItem(FxAssetQueueItem &&other)
        : Path(std::move(other.Path)),
          Loader(std::move(other.Loader)),
          Asset(other.Asset),
          AssetType(other.AssetType)
    {
    }

    FxAssetQueueItem &operator = (FxAssetQueueItem &&other)
    {
        Path = std::move(other.Path);
        Loader = std::move(other.Loader);
        Asset = other.Asset;
        AssetType = other.AssetType;
        return *this;
    }

    std::string Path;
    std::unique_ptr<FxBaseLoader> Loader;

    FxBaseAsset *Asset;
    FxAssetType AssetType;
};
