#pragma once

#include "Asset/AxQueue.hpp"
#include "Asset/AxQueueItem.hpp"
#include "AssetBase.hpp"
#include "AssetDef.hpp"
#include "AssetTicket.hpp"
#include "Object/Object.hpp"
#include "Object/ObjectManager.hpp"

#include <Asset/Loader/Object/LoaderGltf.hpp>
#include <Core/DataNotifier.hpp>
#include <Core/Ref.hpp>
#include <Core/TSQueue.hpp>
#include <Core/TSRef.hpp>
#include <Core/Types.hpp>
#include <atomic>
#include <chrono>
#include <thread>

namespace fx {

template <typename T>
concept C_IsAsset = std::is_base_of_v<AssetBase, T>;


static constexpr uint32 scDeletionTickOffset = 10;


struct AssetDeletionTicket
{
	enum class eType
	{
		None,
		Buffer,
	};

	struct BufferTicket
	{
		VkBuffer Buffer = VK_NULL_HANDLE;
		VmaAllocation Allocation = VK_NULL_HANDLE;
	};

public:
	AssetDeletionTicket(uint32 current_tick, const renderer::RawGpuBuffer& gpu_buffer)
		: Type(eType::Buffer), Value { .Ticket = { .Buffer = gpu_buffer.Buffer, .Allocation = gpu_buffer.Allocation } },
		  MinDeletionTick(current_tick + scDeletionTickOffset)
	{
	}

	void DeleteImmediate() const;
	bool TryDelete(uint32 current_tick) const;

public:
	eType Type = eType::None;

	union
	{
		BufferTicket Ticket;
	} Value;

	uint32 MinDeletionTick = 0;
};

/**
 * Worker thread that waits and processes individual asset loading.
 */
class AssetWorker
{
public:
	AssetWorker() = default;

	void Create();

	void SubmitItemToLoad(AxQueueItem&& item)
	{
		Item = std::move(item);
		ItemReady.Signal();
	}

	void DebugPrint() const
	{
		LogInfo(LC_ASSET, "Worker: Loading {}, {}", Item.Path, AssetTypeToString(Item.Data.LoadType));
	}

	void Update();

	void Kill()
	{
		bRunning.store(false);
		// The worker waits on the ItemReady notifier, so we kill the notifier
		ItemReady.Kill();
	}

private:
	void DirectUploadData(AssetItemData& item_data);

	void LoadObject(LockContext<AssetItemData>& asset_data);
	void LoadImage(LockContext<AssetItemData>& asset_data);


public:
	AxQueueItem Item;
	loader::eLoaderStatus LoadStatus = loader::eLoaderStatus::None;

	DataNotifier ItemReady;

	std::atomic_bool bRunning = { true };

	std::atomic_flag bIsBusy = ATOMIC_FLAG_INIT;
	std::atomic_flag bDataPendingUpload = ATOMIC_FLAG_INIT;

	std::thread Thread;
};

struct LoadObjectOptions
{
	bool bKeepInMemory : 1 = true;
	bool bGeneratePhysicsMesh : 1 = false;
};

class AssetManager
{
public:
	AssetManager() = default;

	void Start(int32 min_threads);
	void Shutdown();

	void WorkerUpdate();

	static AssetManager* GetInstance();

	void DebugPrintWorkers() const;

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
	 * @brief Uploads pixel data to the GPU creating an `fx::Image` object.
	 */
	AssetTicket UploadImage(const ImageInfo& img_info);

	fx::Image* GetNullImage(eImageFormat format);
	AssetTicket GetNullImageTicket(eImageFormat format);

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

	/////////////////////////////////////
	// Deletion Functions
	/////////////////////////////////////

	void DeleteBuffer(const renderer::RawGpuBuffer& buffer)
	{
		SpinLockContext<Queue<fx::AssetDeletionTicket>> queue = mDeletionTickets.GetQueue();

		if (!queue->IsInited()) {
			queue->InitCapacity(256);
		}

		queue->Emplace(mTickCounter, buffer);
		ManagerUpdateNotifier.Signal();
	}

	void ShutdownDeletionQueue();

	void SignalUpdate() { ManagerUpdateNotifier.Signal(); }

	~AssetManager() { Shutdown(); }


private:
	AssetWorker* FindWorkerThread();

	int32 CheckForUploadableData();
	int32 CheckForItemsToLoad();
	int32 CheckForItemsToDelete();

	bool CheckWorkersBusy();

	void AddWorkerThread();
	void AssetManagerUpdate();

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
		AssetManager* mgr = GetInstance();

		mgr->mLoadQueue.Push(AxQueueItem::UploadFileToProcess(ticket, loader, path, asset_type));
		mgr->SignalUpdate();
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
		AssetManager* mgr = GetInstance();

		mgr->mLoadQueue.Push(AxQueueItem::UploadAndProcess(ticket, loader, asset_type, asset_data));
		mgr->SignalUpdate();
	}


public:
	String ScenePath = "";


	//    DataNotifier DataLoaded;
private:
	AxQueue mLoadQueue;
	TSQueue<AssetDeletionTicket> mDeletionTickets;

	SizedArray<AssetWorker*> WorkersWaitingToUpload;

	std::atomic_flag mbActive;
	CountedNotifier ManagerUpdateNotifier;

	bool mbShouldSleep = false;

	uint32 mMinThreads = 2;
	SizedArray<AssetWorker> mWorkerThreads;
	std::thread* mpAssetManagerThread;

	std::atomic_uint mTickCounter = 0;
	uint32 mLastActiveTick = 0;

	std::unordered_map<eImageFormat, fx::Image*> mNullImageList;

	bool mbIsTimeSet = false;
	std::chrono::system_clock::time_point mLastActiveTime;
};

} // namespace fx
