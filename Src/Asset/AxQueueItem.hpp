#pragma once

#include "AxBase.hpp"
#include "Loader/AxLoaderBase.hpp"

#include <Core/LockContext.hpp>
#include <Core/String.hpp>
#include <Core/TSRef.hpp>
#include <mutex>

namespace fx {

enum class eAxType
{
    None,
    Binary,
    Object,
    Image,
};

struct AxItemData
{
    AxItemData() = default;

    template <typename TLoaderType, typename TAssetType>
    AxItemData(const TSRef<TLoaderType>& loader, const TSRef<TAssetType>& asset) : pLoader(loader), pAsset(asset)
    {
    }

    AxItemData(AxItemData&& other) { (*this) = std::move(other); }

    AxItemData& operator=(AxItemData&& other)
    {
        pLoader = std::move(other.pLoader);
        pAsset = std::move(other.pAsset);
        return *this;
    }

    TSRef<AxLoaderBase> pLoader { nullptr };
    TSRef<AxBase> pAsset { nullptr };
};

struct AxQueueItem
{
    AxQueueItem() = default;

    template <typename TLoaderType, typename TAssetType>
    AxQueueItem(const TSRef<TLoaderType>& loader, const TSRef<TAssetType>& asset, eAxType type, const String& path)
        : Path(path), pcRawData(nullptr), DataSize(0), AssetType(type), Data(loader, asset)
    {
    }

    template <typename TLoaderType, typename TAssetType>
    AxQueueItem(const TSRef<TLoaderType>& loader, const TSRef<TAssetType>& asset, eAxType type, const uint8* data,
                uint32 data_size)
        : Path(""), pcRawData(data), DataSize(data_size), AssetType(type), Data(loader, asset)
    {
    }

    AxQueueItem(AxQueueItem&& other) { (*this) = std::move(other); }

    AxQueueItem& operator=(AxQueueItem&& other)
    {
        mMutex.lock();

        Path = other.Path;
        Data = std::move(other.Data);
        pcRawData = other.pcRawData;
        DataSize = other.DataSize;
        AssetType = other.AssetType;

        mMutex.unlock();

        return *this;
    }

    LockContext<AxItemData> GetDataContext() { return LockContext<AxItemData>(mMutex, Data); }

public:
    String Path;

    std::mutex mMutex;


    // Data for loading from memory
    const uint8* pcRawData = nullptr;
    uint32 DataSize = 0;

    eAxType AssetType;

private:
    AxItemData Data;
};

} // namespace fx
