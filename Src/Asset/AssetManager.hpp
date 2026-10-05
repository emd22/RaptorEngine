#pragma once

#include "Asset/AxQueueItem.hpp"
#include "AssetBase.hpp"
#include "AssetDef.hpp"
#include "AssetTicket.hpp"
#include "Object/Object.hpp"
#include "Object/ObjectManager.hpp"

#include <Asset/Loader/Object/LoaderGltf.hpp>
#include <Core/Ref.hpp>
#include <Core/TSRef.hpp>
#include <Core/Types.hpp>
#include <Renderer/Constants.hpp>
#include <Core/HashMap.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <atomic>
#include <vector>
#include <chrono>
#include <thread>

namespace fx {

template <typename T>
concept C_IsAsset = std::is_base_of_v<AssetBase, T>;


// Buffer deletion is deferred by enough rendered frames that any frame which could have referenced the buffer
// has completed before the buffer is destroyed.
static constexpr uint32 scBufferDeletionFrameSpacing = renderer::FramesInFlight + 1;


struct LoadObjectOptions
{
	bool bKeepInMemory : 1 = true;
	bool bGeneratePhysicsMesh : 1 = false;
};

class AssetManager
{
public:
public:
	AssetManager() = default;

	void Start(int32 min_threads);
	void StopWorkers();
	void Shutdown();

	static AssetManager* GetInstance();

	template <typename T>
		requires C_IsAsset<T>
	TSRef<T> NewAsset()
	{
		return TSRef<T>::New();
	}

	FX_FORCE_INLINE void SetScenePath(const String& path) { ScenePath = path; }
	FX_FORCE_INLINE const String& GetScenePath() const { return ScenePath; }


	////////////////////////////////////////////////
	// Object loading
	////////////////////////////////////////////////

	/**
	 * @brief Creates a new `Object` and loads the provided asset into it from
	 * the path provided.
	 */
	AssetTicket LoadObject(const std::string& name, const std::string& path);

	/**
	 * @brief Creates a new `Object` and loads the asset into it from
	 * the data provided.
	 */
	AssetTicket LoadObjectFromMemory(const std::string& name, const uint8* data, uint32 data_size);

	/////////////////////////////////////
	// Image loading
	/////////////////////////////////////

	AssetTicket LoadImage(eImageType image_type, eImageFormat format, const std::string& path, eImageCreateFlags flags);
	AssetTicket LoadImageFromMemory(eImageType image_type, eImageFormat format, const Slice<const uint8>& data,
									eImageCreateFlags flags);


	/**
	 * @brief Uploads pixel data to the GPU creating an `fx::Image` object
	 */
	AssetTicket UploadImage(ImageInfo& img_info);

	fx::Image* GetNullImage(eImageFormat format);
	AssetTicket GetNullImageTicket(eImageFormat format);

	/**
	 * @brief A 1x1 RGBA8 normal map that points straight out of the surface, for materials without a normal map that
	 * are drawn by a pipeline that samples one (e.g. every skinned pipeline).
	 */
	fx::Image* GetFlatNormalImage();

	/////////////////////////////////////
	// General data loading
	/////////////////////////////////////

	/**
	 * @brief Load an asset from memory using a custom loader.
	 * @note This is only for special asset types. Use `LoadImageFromMemory` for images and `LoadObjectFromMemory` for
	 * objects.
	 */
	template <typename TLoaderType>
		requires loader::C_IsLoader<TLoaderType>
	void LoadFromMemory(AssetTicket& ticket, eAssetType asset_type, Slice<const uint8> data)
	{
		TSRef<TLoaderType> loader = TSRef<TLoaderType>::New();
		SubmitLoadAssetFromData<TLoaderType>(ticket, loader, asset_type, data);
	}


	/**
	 * @brief Load an asset from a path using a custom loader.
	 * @note This is only for special asset types. Use `LoadImage` for images and `LoadObject` for objects.
	 */
	template <typename TLoaderType>
		requires loader::C_IsLoader<TLoaderType>
	void LoadFromPath(AssetTicket& ticket, eAssetType asset_type, const std::string& path)
	{
		TSRef<TLoaderType> loader = TSRef<TLoaderType>::New();
		SubmitLoadAssetFromPath<TLoaderType>(ticket, loader, asset_type, path);
	}


	AssetTicket SubmitCustom(const AssetCustomFunctionType& fn);

	/////////////////////////////////////
	// Deletion Functions
	/////////////////////////////////////

	/// Takes ownership of the buffer, which is destroyed once the GPU is done with it.
	void DeleteBuffer(RxBufferResource* buffer_resource);

	void ShutdownDeletionQueue();

	void SignalUpdate();

	~AssetManager()
	{
		Shutdown();

		rx_asset_scheduler_free(mpScheduler);
		mpScheduler = nullptr;
	}


private:
	/// Queues an item to be loaded by one of the workers
	void Submit(AssetQueueItem&& item);

	AssetTicket NewTextureTicket();

	/**
	 * @brief When there is excess free time in asset management, we can tyr and load higher detailed materials/models
	 * for objects that are already loaded.
	 */
	void RequestHigherDetail();

	template <typename TLoaderType>
	static void SubmitLoadAssetFromPath(AssetTicket& ticket, TSRef<TLoaderType>& loader, eAssetType asset_type,
										const std::string& path)
	{
		GetInstance()->Submit(AssetQueueItem::UploadFileToProcess(ticket, loader, path, asset_type));
	}


	// template <typename TLoaderType>
	// static void SubmitLoadObject(const AssetTicket& ticket, TSRef<TLoaderType>& loader, eAssetType asset_type,
	// 							 const std::string& path)
	// {
	// 	AssetManager* mgr = GetInstance();

	// 	mgr->mLoadQueue.Push(AxQueueItem::UploadFileToProcess<loader::LoaderGltf>(ticket, loader, asset_type, path));
	// 	mgr->SignalUpdate();
	// }

	template <typename TLoaderType>
	static void SubmitLoadAssetFromData(AssetTicket& ticket, TSRef<TLoaderType>& loader, eAssetType asset_type,
										const Slice<const uint8>& asset_data)
	{
		GetInstance()->Submit(AssetQueueItem::UploadAndProcess(ticket, loader, asset_type, asset_data));
	}


public:
	String ScenePath = "";


private:
	RxAssetScheduler* mpScheduler = nullptr;

	/// What came in before the scheduler was started, which is handed to it then
	std::mutex mPendingMutex;
	std::vector<AssetQueueItem*> mPendingJobs;
	std::vector<RxBufferResource*> mPendingDeletions;

	std::atomic_flag mbActive;
	bool mbWorkersStopped = false;
	bool mbShutdownDone = false;

	uint32 mMinThreads = 2;

	HashMap<eImageFormat, fx::Image*> mNullImageList;
	fx::Image* mpFlatNormalImage = nullptr;
	std::mutex mNullImageMutex;

};

} // namespace fx
