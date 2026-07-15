#pragma once

#include "AssetDef.hpp"
#include "Loader/ImageLoaderBase.hpp"
#include "Loader/ObjectLoaderBase.hpp"

#include <Asset/AssetTicket.hpp>
#include <Core/LockContext.hpp>
#include <Core/Slice.hpp>
#include <Core/String.hpp>
#include <Core/TSRef.hpp>
// #include <Object/ObjectID.hpp>
#include <Renderer/Backend/Image.hpp>
#include <mutex>
#include <string>

namespace fx {


class Object;


struct AssetItemData
{
	AssetItemData() = default;

	template <typename TLoaderType>
	static AssetItemData Make(const TSRef<TLoaderType>& loader, const AssetTicket& ticket, eAssetType asset_type)
	{
		AssetItemData data;

		data.pLoader = loader;
		data.Ticket = ticket;
		data.LoadType = asset_type;

		return data;
	}

	AssetItemData(AssetItemData&& other) { (*this) = std::move(other); }

	AssetItemData& operator=(AssetItemData&& other)
	{
		pLoader = std::move(other.pLoader);
		Ticket = std::move(other.Ticket);
		LoadType = other.LoadType;

		return *this;
	}

	void CreateGpuResource()
	{
		switch (pLoader->GetLoaderType()) {
		case loader::eLoaderType::ImageLoader: {
			TSRef<loader::ImageLoaderBase> image_loader(pLoader);
			image_loader->CreateGpuResource(Ticket);
			break;
		}
		case loader::eLoaderType::ObjectLoader: {
			TSRef<loader::ObjectLoaderBase> object_loader(pLoader);
			object_loader->CreateGpuResource(Ticket);
			break;
		}

		default:
			LogError("Cannot create GPU resource for unknown type");
			break;
		}
	}

	void DestroyLoader()
	{
		switch (pLoader->GetLoaderType()) {
		case loader::eLoaderType::ImageLoader: {
			TSRef<loader::ImageLoaderBase> image_loader(pLoader);
			image_loader->Destroy();
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

	eAssetType LoadType = eAssetType::None;

	TSRef<loader::LoaderBase> pLoader { nullptr };
	// TSRef<AssetBase> pAsset { nullptr };

	AssetTicket Ticket { nullptr };
};


struct AxQueueItem
{
	AxQueueItem() = default;


	FX_FORCE_INLINE bool IsObject() const { return Data.LoadType == eAssetType::Object; }
	FX_FORCE_INLINE bool IsImage() const { return Data.LoadType == eAssetType::Image; }
	FX_FORCE_INLINE bool IsBinary() const { return Data.LoadType == eAssetType::Binary; }


	template <typename TLoaderType>
	static AxQueueItem UploadFileToProcess(const AssetTicket& ticket, const TSRef<TLoaderType>& loader,
										   const std::string& path, eAssetType asset_type)
	{
		AxQueueItem item;

		item.Data = AssetItemData::Make<TLoaderType>(loader, ticket, asset_type);
		item.pcRawData = nullptr;
		item.DataSize = 0;
		item.AssetLoadOp = eAssetLoadOp::ReadAndUpload;
		item.Path = path;

		return item;
	}

	template <typename TLoaderType>
	static AxQueueItem UploadAndProcess(const AssetTicket& ticket, const TSRef<TLoaderType>& loader,
										eAssetType asset_type, const Slice<const uint8>& data)
	{
		AxQueueItem item;

		item.Data = AssetItemData::Make<TLoaderType>(loader, ticket, asset_type);

		item.pcRawData = data.pData;
		item.DataSize = data.Size;
		item.AssetLoadOp = eAssetLoadOp::ProcessAndUpload;

		return item;
	}


	static AxQueueItem DirectUploadImage(AssetTicket& asset, const ImageInfo& img_info)
	{
		AxQueueItem item;

		item.Data = AssetItemData::Make<loader::LoaderBase>(nullptr, asset, eAssetType::Image);

		item.pcRawData = nullptr;
		item.ImgInfo = img_info;
		item.AssetLoadOp = eAssetLoadOp::DirectUpload;

		return item;
	}

	AxQueueItem(AxQueueItem&& other) { (*this) = std::move(other); }

	AxQueueItem& operator=(AxQueueItem&& other)
	{
		mMutex.lock();

		Path = other.Path;
		Data = std::move(other.Data);
		pcRawData = other.pcRawData;
		DataSize = other.DataSize;
		ImgInfo = std::move(other.ImgInfo);
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
