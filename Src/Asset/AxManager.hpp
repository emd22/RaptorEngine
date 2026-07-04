#pragma once

#include "Asset/AxQueue.hpp"
#include "Asset/AxQueueItem.hpp"
#include "AssetTicket.hpp"
#include "AxImage.hpp"
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
class AxWorker
{
public:
    AxWorker() = default;

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
    void LoadImage(const LockContext<AssetItemData>& asset_data);


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

class AxManager
{
public:
    AxManager() = default;

    void Start(int32 min_threads);
    void Shutdown();

    void WorkerUpdate();

    static AxManager* GetInstance();

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
    // Methods to create and load objects
    ////////////////////////////////////////////////


    /**
     * @brief Creates a new `Object` and loads the provided asset into it from
     * the path provided.
     */
    AssetTicket<Object> LoadObject(const std::string& name, const std::string& path, LoadObjectOptions options = {})
    {
        Object* object = gObjectManager->NewObject(name);

        AssetTicket<Object> ticket { object };
        LoadObject(ticket, path, options);

        return ticket;
    }

    /**
     * @brief Creates a new `Object` and loads the asset into it from
     * the data provided.
     */
    AssetTicket<Object> LoadObjectFromMemory(const std::string& name, const uint8* data, uint32 data_size)
    {
        Object* object = gObjectManager->NewObject(name);

        AssetTicket<Object> ticket { object };
        LoadObjectFromMemory(ticket, data, data_size);

        return ticket;
    }

    TSRef<AxImage> LoadImage(eImageType image_type, eImageFormat format, const std::string& path,
                             eImageCreateFlags flags)
    {
        TSRef<AxImage> asset = TSRef<AxImage>::New();
        LoadImage(image_type, format, asset, path, flags);

        return asset;
    }

    inline TSRef<AxImage> LoadImage(const std::string& path, eImageFormat format, eImageCreateFlags flags)
    {
        return LoadImage(eImageType::Flat, format, path, flags);
    }

    /**
     * @brief Creates a new `Object` and loads the asset into it from
     * the data provided.
     */
    TSRef<AxImage> LoadImageFromMemory(eImageType image_type, eImageFormat format, const uint8* data, uint32 data_size)
    {
        TSRef<AxImage> asset = TSRef<AxImage>::New();
        LoadImageFromMemory(image_type, format, asset, data, data_size);

        return asset;
    }

    TSRef<AxImage> LoadImageFromPixels(const ImageInfo& img_info)
    {
        TSRef<AxImage> asset = TSRef<AxImage>::New();
        LoadImageFromPixels(asset, img_info);
        return asset;
    }

    inline TSRef<AxImage> LoadImageFromMemory(eImageFormat format, const uint8* data, uint32 data_size)
    {
        return LoadImageFromMemory(eImageType::Flat, format, data, data_size);
    }

    ////////////////////////////////////////////////
    // Methods to load into existing containers
    ////////////////////////////////////////////////

    void LoadImageFromMemory(eImageType image_type, eImageFormat format, TSRef<AxImage>& asset, const uint8* data,
                             uint32 data_size);

    void LoadImageFromPixels(TSRef<AxImage>& asset, const ImageInfo& img_info);

    /**
     * @brief Loads an asset into the provided asset from the provided data.
     */
    void LoadObjectFromMemory(const AssetTicket<Object>& ticket, const uint8* data, uint32 data_size);


    /**
     * @brief Loads an object into the provided asset from a path.
     */
    void LoadObject(const AssetTicket<Object>& ticket, const std::string& path, LoadObjectOptions options = {});


    void LoadImage(eImageType image_type, eImageFormat format, TSRef<AxImage>& asset, const std::string& path,
                   eImageCreateFlags flags);

    /**
     * @brief Loads an Image2D from the path provided into `asset`.
     */
    inline void LoadImage(TSRef<AxImage>& asset, eImageFormat format, const std::string& path, eImageCreateFlags flags)
    {
        return LoadImage(eImageType::Flat, format, asset, path, flags);
    }
    /**
     * @brief Loads an Image2D from the data provided into `asset`.
     */
    void LoadImageFromMemory(TSRef<AxImage>& asset, eImageFormat format, const uint8* data, uint32 data_size)
    {
        LoadImageFromMemory(eImageType::Flat, format, asset, data, data_size);
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


    ~AxManager() { Shutdown(); }


private:
    AxWorker* FindWorkerThread();

    bool CheckForUploadableData();
    bool CheckForItemsToLoad();
    bool CheckForItemsToDelete();

    bool CheckWorkersBusy();

    void AddWorkerThread();
    void AssetManagerUpdate();

    template <typename TAssetType, typename TLoaderType, eAssetLoadType TLoadType>
        requires C_IsAsset<TAssetType>
    static void SubmitLoadAssetFromPath(const TSRef<TAssetType>& asset, TSRef<TLoaderType>& loader,
                                        const std::string& path)
    {
        AssertMsg(asset->bIsUploadedToGpu == false, "Asset is already uploaded!");

        AxManager* mgr = GetInstance();

        mgr->mLoadQueue.Push(AxQueueItem::UploadFileToProcess(path, loader, asset, TLoadType));
        mgr->ManagerUpdateNotifier.Signal();
    }


    template <typename TLoaderType>
    static void SubmitLoadObject(const AssetTicket<Object>& ticket, TSRef<TLoaderType>& loader, const std::string& path)
    {
        AxManager* mgr = GetInstance();

        mgr->mLoadQueue.Push(AxQueueItem::UploadFileToProcess<loader::LoaderGltf>(path, loader, ticket));
        mgr->ManagerUpdateNotifier.Signal();
    }

    template <typename TAssetType, typename TLoaderType, eAssetLoadType TLoadType>
    static void SubmitLoadAssetFromData(const TSRef<TAssetType>& asset, TSRef<TLoaderType>& loader,
                                        const Slice<const uint8>& asset_data)
    {
        AssertMsg(asset->bIsUploadedToGpu == false, "Asset is already uploaded!");

        AxManager* mgr = GetInstance();

        mgr->mLoadQueue.Push(AxQueueItem::UploadAndProcess(loader, asset, TLoadType, asset_data));
        mgr->ManagerUpdateNotifier.Signal();
    }

    template <typename TLoaderType>
    static void SubmitLoadObject(const AssetTicket<Object>& ticket, TSRef<TLoaderType>& loader,
                                 const Slice<const uint8>& asset_data)
    {
        AxManager* mgr = GetInstance();

        mgr->mLoadQueue.Push(AxQueueItem::UploadAndProcess<loader::LoaderGltf>(loader, ticket, asset_data));
        mgr->ManagerUpdateNotifier.Signal();
    }

    template <typename TAssetType, eAssetLoadType TLoadType>
        requires C_IsAsset<TAssetType>
    static void SubmitImageToUpload(const TSRef<TAssetType>& asset, const ImageInfo& img_info)
    {
        AssertMsg(img_info.ImageData.pData != nullptr, "Image data cannot be null");

        AxManager* mgr = GetInstance();

        mgr->mLoadQueue.Push(AxQueueItem::DirectUpload(asset, TLoadType, img_info));
        mgr->ManagerUpdateNotifier.Signal();
    }

public:
    String ScenePath = "";


    //    DataNotifier DataLoaded;
private:
    AxQueue mLoadQueue;
    TSQueue<AssetDeletionTicket> mDeletionTickets;

    SizedArray<AxWorker*> WorkersWaitingToUpload;

    std::atomic_flag mbActive;
    CountedNotifier ManagerUpdateNotifier;

    bool mbShouldSleep = false;

    uint32 mMinThreads = 2;
    SizedArray<AxWorker> mWorkerThreads;
    std::thread* mpAssetManagerThread;

    std::atomic_uint mTickCounter = 0;
    uint32 mLastActiveTick = 0;

    bool mbIsTimeSet = false;
    std::chrono::system_clock::time_point mLastActiveTime;
};

} // namespace fx
