#pragma once

#include "AssetBase.hpp"
#include "Loader/ImageLoaderBase.hpp"
#include "Loader/ObjectLoaderBase.hpp"

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


struct AssetItemData
{
    AssetItemData() = default;

    template <typename TLoaderType, typename TAssetType>
    static AssetItemData Make(const TSRef<TLoaderType>& loader, const TSRef<TAssetType>& asset,
                              eAssetLoadType load_type)
    {
        AssetItemData data;

        data.pLoader = loader;
        data.pAsset = asset;
        data.LoadType = load_type;

        return std::move(data);
    }

    template <typename TLoaderType>
    static AssetItemData Make(const TSRef<TLoaderType>& loader, const ObjectID& id)
    {
        AssetItemData data;

        data.pLoader = loader;
        data.ObjID = id;
        data.LoadType = eAssetLoadType::Object;

        return std::move(data);
    }

    AssetItemData(AssetItemData&& other) { (*this) = std::move(other); }

    AssetItemData& operator=(AssetItemData&& other)
    {
        pLoader = std::move(other.pLoader);
        pAsset = std::move(other.pAsset);
        ObjID = std::move(other.ObjID);
        LoadType = other.LoadType;

        return *this;
    }

    void CreateGpuResource()
    {
        switch (pLoader->scLoaderType) {
        case loader::eLoaderType::ImageLoader: {
            TSRef<loader::ImageLoaderBase> image_loader(pLoader);
            image_loader->CreateGpuResource(pAsset);
            break;
        }
        case loader::eLoaderType::ObjectLoader: {
            TSRef<loader::ObjectLoaderBase> object_loader(pLoader);
            object_loader->CreateGpuResource(ObjID);
            break;
        }

        default:
            LogError("Cannot create GPU resource for unknown type");
            break;
        }
    }

    void DestroyLoader()
    {
        switch (pLoader->scLoaderType) {
        case loader::eLoaderType::ImageLoader: {
            TSRef<loader::ImageLoaderBase> image_loader(pLoader);
            image_loader->Destroy(pAsset);
            break;
        }
        case loader::eLoaderType::ObjectLoader: {
            TSRef<loader::ObjectLoaderBase> object_loader(pLoader);
            object_loader->Destroy();
            break;
        }

        default:
            LogError("Cannot create GPU resource for unknown type");
            break;
        }
    }

    eAssetLoadType LoadType = eAssetLoadType::None;

    TSRef<loader::LoaderBase> pLoader { nullptr };
    TSRef<AssetBase> pAsset { nullptr };

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
        AxQueueItem item;

        item.Data = AssetItemData::Make<TLoaderType, TAssetType>(loader, asset, type);

        item.pcRawData = nullptr;
        item.DataSize = 0;
        item.AssetLoadOp = eAssetLoadOp::ReadAndUpload;
        item.Path = path;

        return std::move(item);
    }

    template <typename TLoaderType>
    static AxQueueItem UploadFileToProcess(const std::string& path, const TSRef<TLoaderType>& loader,
                                           const ObjectID& object_id)
    {
        AxQueueItem item;

        item.Data = AssetItemData::Make<TLoaderType>(loader, object_id);
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

        item.Data = AssetItemData::Make<TLoaderType, TAssetType>(loader, asset, type);

        item.pcRawData = data.pData;
        item.DataSize = data.Size;
        item.AssetLoadOp = eAssetLoadOp::ProcessAndUpload;

        return std::move(item);
    }

    template <typename TLoaderType>
    static AxQueueItem UploadAndProcess(const TSRef<TLoaderType>& loader, const ObjectID& object_id,
                                        const Slice<const uint8>& data)
    {
        AxQueueItem item;

        item.Data = AssetItemData::Make<TLoaderType>(loader, object_id);

        item.pcRawData = data.pData;
        item.DataSize = data.Size;
        item.AssetLoadOp = eAssetLoadOp::ProcessAndUpload;

        return std::move(item);
    }


    template <typename TAssetType>
    static AxQueueItem DirectUpload(const TSRef<TAssetType>& asset, eAssetLoadType type, const ImageInfo& img_info)
    {
        AxQueueItem item;

        item.Data = AssetItemData::Make<loader::LoaderBase, TAssetType>(nullptr, asset, type);

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

    LockContext<AssetItemData> GetDataContext() { return LockContext<AssetItemData>(mMutex, Data); }

public:
    std::string Path;

    std::mutex mMutex;

    // Data for loading from memory
    const uint8* pcRawData = nullptr;
    uint32 DataSize = 0;

    ImageInfo ImgInfo;

    eAssetLoadOp AssetLoadOp = eAssetLoadOp::None;

    AssetItemData Data;
};

} // namespace fx
