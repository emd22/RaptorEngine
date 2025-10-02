#pragma once

#include "FxAssetBase.hpp"
#include "Loader/FxLoaderBase.hpp"

#include <Core/FxRef.hpp>
#include <string>

enum class FxAssetType
{
    None,
    Binary,
    // Model,
    Object,
    Image,
};

struct FxAssetQueueItem
{
    FxAssetQueueItem() = default;

    template <typename LoaderType, typename AssetType>
    FxAssetQueueItem(const FxRef<LoaderType>& loader, const FxRef<AssetType>& asset, FxAssetType type,
                     const std::string& path)
        : Path(path), Loader(loader), Asset(asset), RawData(nullptr), DataSize(0), AssetType(type)
    {
    }

    template <typename LoaderType, typename AssetType>
    FxAssetQueueItem(const FxRef<LoaderType>& loader, const FxRef<AssetType>& asset, FxAssetType type,
                     const uint8* data, uint32 data_size)
        : Path(""), Loader(loader), Asset(asset), RawData(data), DataSize(data_size), AssetType(type)
    {
    }
    std::string Path;

    FxRef<FxLoaderBase> Loader { nullptr };
    FxRef<FxAssetBase> Asset { nullptr };

    // Data for loading from memory
    const uint8* RawData = nullptr;
    uint32 DataSize = 0;

    FxAssetType AssetType;
};
