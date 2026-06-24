#pragma once

#include "AxBase.hpp"
#include "Loader/AxLoaderBase.hpp"

#include <Core/LockContext.hpp>
#include <Core/Slice.hpp>
#include <Core/String.hpp>
#include <Core/TSRef.hpp>
#include <Object/ObjectID.hpp>
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
    AxItemData(const TSRef<TLoaderType>& loader, const TSRef<TAssetType>& asset, eAssetLoadType load_type)
        : pLoader(loader), pAsset(asset), LoadType(load_type)
    {
    }

    template <typename TLoaderType>
    AxItemData(const TSRef<TLoaderType>& loader, const ObjectID& id)
        : pLoader(loader), ObjID(id), LoadType(eAssetLoadType::Object)
    {
    }

    AxItemData(AxItemData&& other) { (*this) = std::move(other); }

    AxItemData& operator=(AxItemData&& other)
    {
        pLoader = std::move(other.pLoader);
        pAsset = std::move(other.pAsset);
        ObjID = std::move(other.ObjID);
        LoadType = other.LoadType;

        return *this;
    }

    eAssetLoadType LoadType = eAssetLoadType::None;

    TSRef<AxLoaderBase> pLoader { nullptr };
    TSRef<AxBase> pAsset { nullptr };

    ObjectID ObjID { ObjectID::Null };
};


struct AxQueueItem
{
    AxQueueItem() = default;

    FX_FORCE_INLINE bool IsObject() const { return Data.LoadType == eAssetLoadType::Object; }
    FX_FORCE_INLINE bool IsImage() const { return Data.LoadType == eAssetLoadType::Image; }
    FX_FORCE_INLINE bool IsBinary() const { return Data.LoadType == eAssetLoadType::Binary; }

    template <typename TLoaderType, typename TAssetType>
    static AxQueueItem UploadFileToProcess(const std::string& path, const TSRef<TLoaderType>& loader,
                                           const TSRef<TAssetType>& asset, eAssetLoadType type)
    {
        AxQueueItem item { loader, asset, type };

        item.pcRawData = nullptr;
        item.DataSize = 0;
        item.AssetLoadOp = eAssetLoadOp::ReadAndUpload;
        item.Path = path;

        return std::move(item);
    }

    template <typename TLoaderType, typename TAssetType>
    static AxQueueItem UploadFileToProcess(const std::string& path, const TSRef<TLoaderType>& loader,
                                           const ObjectID& object_id, eAssetLoadType type)
    {
        AxQueueItem item { loader, object_id };

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
        AxQueueItem item { loader, asset, type };

        item.pcRawData = data.pData;
        item.DataSize = data.Size;
        item.AssetLoadOp = eAssetLoadOp::ProcessAndUpload;

        return std::move(item);
    }

    template <typename TLoaderType, typename TAssetType>
    static AxQueueItem UploadAndProcess(const TSRef<TLoaderType>& loader, const ObjectID& object_id,
                                        eAssetLoadType type, const Slice<const uint8>& data)
    {
        AxQueueItem item { loader, object_id };

        item.pcRawData = data.pData;
        item.DataSize = data.Size;
        item.AssetLoadOp = eAssetLoadOp::ProcessAndUpload;

        return std::move(item);
    }


    template <typename TAssetType>
    static AxQueueItem DirectUpload(const TSRef<TAssetType>& asset, eAssetLoadType type, const ImageInfo& img_info)
    {
        AxQueueItem item { nullptr, asset, type };

        item.pcRawData = nullptr;
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
        ImgInfo = other.ImgInfo;
        AssetLoadOp = other.AssetLoadOp;

        other.pcRawData = nullptr;
        other.DataSize = 0;
        other.ImgInfo.ImageData.SetNull();

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

    eAssetLoadOp AssetLoadOp = eAssetLoadOp::None;

    AxItemData Data;
};

} // namespace fx
