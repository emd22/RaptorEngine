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
        const FxRef<LoaderType>& loader,
        const FxRef<AssetType>& asset,
        FxAssetType type,
        const std::string& path
    )
        : Path(path), Loader(loader), Asset(asset), RawData(nullptr), DataSize(0), AssetType(type)
    {
    }

    template <typename LoaderType, typename AssetType>
    FxAssetQueueItem(
        const FxRef<LoaderType>& loader,
        const FxRef<AssetType>& asset,
        FxAssetType type,
        const uint8* data,
        uint32 data_size
    )
        : Path(""), Loader(loader), Asset(asset), RawData(data), DataSize(data_size), AssetType(type)
    {
    }

    // FxAssetQueueItem() = default;
    // FxAssetQueueItem(FxAssetQueueItem&& other)
    //     : Path(other.Path),
    //       Loader(std::move(other.Loader)),
    //       Asset(std::move(other.Asset)),
    //       AssetType(other.AssetType)
    // {
    // }

    // FxAssetQueueItem(const FxAssetQueueItem& other)
    // {
    //     Path = other.Path;
    //     Loader = other.Loader;
    //     Asset = other.Asset;
    //     AssetType = other.AssetType;

    //     RawData = other.RawData;
    // }

    // FxAssetQueueItem& operator = (FxAssetQueueItem&& other)
    // {
    //     Path = other.Path;
    //     Loader = other.Loader;
    //     Asset = other.Asset;
    //     AssetType = other.AssetType;
    //     return *this;
    // }

    // FxAssetQueueItem& operator = (FxAssetQueueItem& other)
    // {
    //     Path = other.Path;
    //     Loader = other.Loader;
    //     Asset = other.Asset;
    //     AssetType = other.AssetType;
    //     return *this;
    // }

    std::string Path;

    FxRef<FxBaseLoader> Loader{nullptr};
    FxRef<FxBaseAsset> Asset{nullptr};

    // Data for loading from memory
    const uint8* RawData = nullptr;
    uint32 DataSize = 0;

    FxAssetType AssetType;
};
