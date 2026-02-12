#pragma once

#include "AxBase.hpp"
#include "Loader/AxLoaderBase.hpp"

#include <Core/FxLockContext.hpp>
#include <Core/FxTSRef.hpp>
#include <mutex>
#include <string>

enum class AxType
{
    eNone,
    eBinary,
    eObject,
    eImage,
};

struct AxItemData
{
    AxItemData() = default;

    template <typename TLoaderType, typename TAssetType>
    AxItemData(const FxRef<TLoaderType>& loader, const FxRef<TAssetType>& asset) : pLoader(loader), pAsset(asset)
    {
    }

    AxItemData(AxItemData&& other) { (*this) = std::move(other); }

    AxItemData& operator=(AxItemData&& other)
    {
        pLoader = std::move(other.pLoader);
        pAsset = std::move(other.pAsset);
        return *this;
    }

    FxRef<AxLoaderBase> pLoader { nullptr };
    FxRef<AxBase> pAsset { nullptr };
};

struct AxQueueItem
{
    AxQueueItem() = default;

    template <typename TLoaderType, typename TAssetType>
    AxQueueItem(const FxRef<TLoaderType>& loader, const FxRef<TAssetType>& asset, AxType type, const std::string& path)
        : Path(path), RawData(nullptr), DataSize(0), AssetType(type), Data(loader, asset)
    {
    }

    template <typename TLoaderType, typename TAssetType>
    AxQueueItem(const FxRef<TLoaderType>& loader, const FxRef<TAssetType>& asset, AxType type, const uint8* data,
                uint32 data_size)
        : Path(""), RawData(data), DataSize(data_size), AssetType(type), Data(loader, asset)
    {
    }

    AxQueueItem(AxQueueItem&& other) { (*this) = std::move(other); }

    AxQueueItem& operator=(AxQueueItem&& other)
    {
        mMutex.lock();

        Path = other.Path;
        Data = std::move(other.Data);
        RawData = other.RawData;
        DataSize = other.DataSize;
        AssetType = other.AssetType;

        mMutex.unlock();

        return *this;
    }

    FxLockContext<AxItemData> GetDataContext() { return FxLockContext<AxItemData>(mMutex, Data); }

public:
    std::string Path;

    std::mutex mMutex;


    // Data for loading from memory
    const uint8* RawData = nullptr;
    uint32 DataSize = 0;

    AxType AssetType;

private:
    AxItemData Data;
};
