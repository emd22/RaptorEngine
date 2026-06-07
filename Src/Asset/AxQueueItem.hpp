#pragma once

#include "AxBase.hpp"
#include "Loader/AxLoaderBase.hpp"

#include <Core/LockContext.hpp>
#include <Core/Slice.hpp>
#include <Core/String.hpp>
#include <Core/TSRef.hpp>
#include <Renderer/Backend/Image.hpp>
#include <mutex>
#include <string>

namespace fx {


enum class eAssetLoadType
{
    None,
    Binary,
    Object,
    Image,
};

enum class eAssetLoadOp
{
    None,

    /// Reads a file source into an asset given a path.
    ReadAndUpload,

    /// Reads unprocessed file data using a loader and uploads to an asset.
    ProcessAndUpload,

    /// Directly uploads the data into an asset.
    DirectUpload,
};


constexpr const char* AssetTypeToString(eAssetLoadType type)
{
    switch (type) {
    case eAssetLoadType::None:
        return "None";
    case eAssetLoadType::Binary:
        return "Binary";
    case eAssetLoadType::Object:
        return "Object";
    case eAssetLoadType::Image:
        return "Image";
    default:;
    }
    return "None";
}


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

    // template <typename TLoaderType, typename TAssetType>
    // AxQueueItem(const TSRef<TLoaderType>& loader, const TSRef<TAssetType>& asset, eAssetType type,
    //             const std::string& path)
    //     : Path(path), pcRawData(nullptr), DataSize(0), AssetType(type), Data(loader, asset)
    // {
    // }

    // template <typename TLoaderType, typename TAssetType>
    // AxQueueItem(const TSRef<TLoaderType>& loader, const TSRef<TAssetType>& asset, eAssetType type, const uint8* data,
    //             uint32 data_size)
    //     : Path(""), pcRawData(data), DataSize(data_size), AssetType(type), Data(loader, asset)
    // {
    // }

    template <typename TLoaderType, typename TAssetType>
    static AxQueueItem UploadFileToProcess(const std::string& path, const TSRef<TLoaderType>& loader,
                                           const TSRef<TAssetType>& asset, eAssetLoadType type)
    {
        AxQueueItem item;
        item.Data.pLoader = loader;
        item.Data.pAsset = asset;

        item.AssetType = type;
        item.pcRawData = nullptr;
        item.DataSize = 0;
        item.AssetLoadOp = eAssetLoadOp::ReadAndUpload;
        item.Path = path;

        return std::move(item);
    }


    template <typename TLoaderType, typename TAssetType>
    static AxQueueItem UploadAndProcess(const TSRef<TLoaderType>& loader, const TSRef<TAssetType>& asset,
                                        eAssetLoadType type, const Slice<const uint8>& data)
    {
        AxQueueItem item;
        item.Data.pLoader = loader;
        item.Data.pAsset = asset;

        item.AssetType = type;
        item.pcRawData = data.pData;
        item.DataSize = data.Size;
        item.AssetLoadOp = eAssetLoadOp::ProcessAndUpload;

        return std::move(item);
    }


    template <typename TAssetType>
    static AxQueueItem DirectUpload(const TSRef<TAssetType>& asset, eAssetLoadType type, const ImageInfo& img_info)
    {
        AxQueueItem item;
        item.Data.pLoader = nullptr;
        item.Data.pAsset = asset;

        item.pcRawData = nullptr;
        item.AssetType = type;
        item.ImgInfo = img_info;
        item.AssetLoadOp = eAssetLoadOp::DirectUpload;

        return std::move(item);
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
        AssetLoadOp = other.AssetLoadOp;
        ImgInfo = other.ImgInfo;

        other.pcRawData = nullptr;
        other.DataSize = 0;

        mMutex.unlock();

        return *this;
    }

    LockContext<AxItemData> GetDataContext() { return LockContext<AxItemData>(mMutex, Data); }

public:
    std::string Path;

    std::mutex mMutex;

    // Data for loading from memory
    const uint8* pcRawData = nullptr;
    uint32 DataSize = 0;

    ImageInfo ImgInfo;
    eAssetLoadType AssetType;

    eAssetLoadOp AssetLoadOp = eAssetLoadOp::None;

private:
    AxItemData Data;
};

} // namespace fx
