#pragma once

#include "AxBase.hpp"
#include "Loader/AxLoaderBase.hpp"

#include <Core/FxTSRef.hpp>
#include <mutex>
#include <string>

enum class AxType
{
    None,
    Binary,
    Object,
    Image,
};

struct AxQueueItem
{
    AxQueueItem() = default;

    template <typename TLoaderType, typename TAssetType>
    AxQueueItem(const FxRef<TLoaderType>& loader, const FxRef<TAssetType>& asset, AxType type, const std::string& path)
        : Path(path), Loader(loader), Asset(asset), RawData(nullptr), DataSize(0), AssetType(type)
    {
    }

    template <typename TLoaderType, typename TAssetType>
    AxQueueItem(const FxRef<TLoaderType>& loader, const FxRef<TAssetType>& asset, AxType type, const uint8* data,
                uint32 data_size)
        : Path(""), Loader(loader), Asset(asset), RawData(data), DataSize(data_size), AssetType(type)
    {
    }

    AxQueueItem(AxQueueItem&& other)
    {
        other.mMutex.lock();
        Path = other.Path;
        Loader = std::move(other.Loader);
        Asset = std::move(other.Asset);
        RawData = other.RawData;
        DataSize = other.DataSize;
        AssetType = other.AssetType;
        other.mMutex.unlock();
    }


    AxQueueItem& operator=(AxQueueItem&& other)
    {
        other.mMutex.lock();
        Path = other.Path;
        Loader = std::move(other.Loader);
        Asset = std::move(other.Asset);
        RawData = other.RawData;
        DataSize = other.DataSize;
        AssetType = other.AssetType;
        other.mMutex.unlock();

        return *this;
    }

public:
    std::string Path;

    std::mutex mMutex;

    FxRef<AxLoaderBase> Loader { nullptr };
    FxRef<AxBase> Asset { nullptr };

    // Data for loading from memory
    const uint8* RawData = nullptr;
    uint32 DataSize = 0;

    AxType AssetType;
};
