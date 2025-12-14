#pragma once

#include "AxBase.hpp"
#include "Loader/AxLoaderBase.hpp"

#include <Core/FxRef.hpp>
#include <string>

enum class AxType
{
    None,
    Binary,
    // Model,
    Object,
    Image,
};

struct AxQueueItem
{
    AxQueueItem() = default;

    template <typename LoaderType, typename AssetType>
    AxQueueItem(const FxRef<LoaderType>& loader, const FxRef<AssetType>& asset, AxType type, const std::string& path)
        : Path(path), Loader(loader), Asset(asset), RawData(nullptr), DataSize(0), AssetType(type)
    {
    }

    template <typename LoaderType, typename AssetType>
    AxQueueItem(const FxRef<LoaderType>& loader, const FxRef<AssetType>& asset, AxType type, const uint8* data,
                uint32 data_size)
        : Path(""), Loader(loader), Asset(asset), RawData(data), DataSize(data_size), AssetType(type)
    {
    }
    std::string Path;

    FxRef<AxLoaderBase> Loader { nullptr };
    FxRef<AxBase> Asset { nullptr };

    // Data for loading from memory
    const uint8* RawData = nullptr;
    uint32 DataSize = 0;

    AxType AssetType;
};
