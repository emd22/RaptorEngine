#pragma once

#include <Core/FxRef.hpp>

#include "FxBaseAsset.hpp"
#include "Loader/FxBaseLoader.hpp"

#include <string>

enum class FxAssetType
{
    None,
    Binary,
    Model,
    Image,
};

struct FxAssetQueueItem
{
    FxAssetQueueItem() = default;

    template <typename LoaderType, typename AssetType>
    FxAssetQueueItem(
        std::shared_ptr<LoaderType> loader,
        std::shared_ptr<AssetType> asset,
        FxAssetType type,
        const std::string& path
    )
        : Path(path), Loader(loader), Asset(asset), AssetType(type)
    {
    }

    // FxAssetQueueItem() = default;
    FxAssetQueueItem(FxAssetQueueItem&& other)
        : Path(other.Path),
          Loader(std::move(other.Loader)),
          Asset(std::move(other.Asset)),
          AssetType(other.AssetType)
    {
    }

    FxAssetQueueItem(const FxAssetQueueItem& other)
    {
        Path = other.Path;
        Loader = other.Loader;
        Asset = other.Asset;
        AssetType = other.AssetType;
    }

    FxAssetQueueItem& operator = (FxAssetQueueItem&& other)
    {
        Path = other.Path;
        Loader = other.Loader;
        Asset = other.Asset;
        AssetType = other.AssetType;
        return *this;
    }

    FxAssetQueueItem& operator = (FxAssetQueueItem& other)
    {
        Path = other.Path;
        Loader = other.Loader;
        Asset = other.Asset;
        AssetType = other.AssetType;
        return *this;
    }

    std::string Path;

    std::shared_ptr<FxBaseLoader> Loader{nullptr};
    std::shared_ptr<FxBaseAsset> Asset{nullptr};

    FxAssetType AssetType;
};
