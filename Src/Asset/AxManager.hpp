#pragma once

#include "Asset/AxQueue.hpp"
#include "Asset/AxQueueItem.hpp"
#include "AxImage.hpp"
#include "Object.hpp"

#include <Core/DataNotifier.hpp>
#include <Core/Ref.hpp>
#include <Core/TSRef.hpp>
#include <Core/Types.hpp>
#include <atomic>
#include <thread>

namespace fx {

template <typename T>
concept C_IsAsset = std::is_base_of_v<AxBase, T>;

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
        ItemReady.SignalDataWritten();
    }

    void DebugPrint() const
    {
        LogInfo(LC_ASSET, "Worker: Loading {}, {}", Item.Path, AssetTypeToString(Item.AssetType));
    }

    void Update();

    void Kill()
    {
        bRunning.store(false);
        // The worker waits on the ItemReady notifier, so we kill the notifier
        ItemReady.Kill();
    }


public:
    AxQueueItem Item;
    AxLoaderBase::eStatus LoadStatus = AxLoaderBase::eStatus::None;

    DataNotifier ItemReady;

    std::atomic_bool bRunning = true;

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


    ////////////////////////////////////////////////
    // Methods to create and load objects
    ////////////////////////////////////////////////


    /**
     * @brief Creates a new `Object` and loads the provided asset into it from
     * the path provided.
     */
    TSRef<Object> LoadObject(const std::string& name, const std::string& path, LoadObjectOptions options = {})
    {
        TSRef<Object> asset = TSRef<Object>::New();
        LoadObject(name, asset, path, options);

        return asset;
    }

    /**
     * @brief Creates a new `Object` and loads the asset into it from
     * the data provided.
     */
    TSRef<Object> LoadObjectFromMemory(const std::string& name, const uint8* data, uint32 data_size)
    {
        TSRef<Object> asset = TSRef<Object>::New();
        LoadObjectFromMemory(name, asset, data, data_size);

        return asset;
    }

    TSRef<AxImage> LoadImage(renderer::eImageType image_type, renderer::eImageFormat format, const std::string& path)
    {
        TSRef<AxImage> asset = TSRef<AxImage>::New();
        LoadImage(image_type, format, asset, path);

        return asset;
    }

    inline TSRef<AxImage> LoadImage(const std::string& path, renderer::eImageFormat format)
    {
        return LoadImage(renderer::eImageType::Flat, format, path);
    }

    /**
     * @brief Creates a new `Object` and loads the asset into it from
     * the data provided.
     */
    TSRef<AxImage> LoadImageFromMemory(renderer::eImageType image_type, renderer::eImageFormat format,
                                       const uint8* data, uint32 data_size)
    {
        TSRef<AxImage> asset = TSRef<AxImage>::New();
        LoadImageFromMemory(image_type, format, asset, data, data_size);

        return asset;
    }

    inline TSRef<AxImage> LoadImageFromMemory(renderer::eImageFormat format, const uint8* data, uint32 data_size)
    {
        return LoadImageFromMemory(renderer::eImageType::Flat, format, data, data_size);
    }


    ////////////////////////////////////////////////
    // Methods to load into existing containers
    ////////////////////////////////////////////////

    void LoadImageFromMemory(renderer::eImageType image_type, renderer::eImageFormat format, TSRef<AxImage>& asset,
                             const uint8* data, uint32 data_size);

    /**
     * @brief Loads an asset into the provided asset from the provided data.
     */
    void LoadObjectFromMemory(const std::string& name, TSRef<Object>& asset, const uint8* data, uint32 data_size);


    /**
     * @brief Loads an object into the provided asset from a path.
     */
    void LoadObject(const std::string& name, TSRef<Object>& asset, const std::string& path,
                    LoadObjectOptions options = {});


    void LoadImage(renderer::eImageType image_type, renderer::eImageFormat format, TSRef<AxImage>& asset,
                   const std::string& path);

    /**
     * @brief Loads an Image2D from the path provided into `asset`.
     */
    inline void LoadImage(TSRef<AxImage>& asset, renderer::eImageFormat format, const std::string& path)
    {
        return LoadImage(renderer::eImageType::Flat, format, asset, path);
    }
    /**
     * @brief Loads an Image2D from the data provided into `asset`.
     */
    void LoadImageFromMemory(TSRef<AxImage>& asset, renderer::eImageFormat format, const uint8* data, uint32 data_size)
    {
        LoadImageFromMemory(renderer::eImageType::Flat, format, asset, data, data_size);
    }

    ~AxManager() { Shutdown(); }


private:
    AxWorker* FindWorkerThread();

    void CheckForUploadableData();
    void CheckForItemsToLoad();

    bool CheckWorkersBusy();

    void AddWorkerThread();
    void AssetManagerUpdate();

    template <typename TAssetType, typename TLoaderType, eAssetType TEnumValue>
        requires C_IsAsset<TAssetType>
    static void SubmitAssetToLoad(const TSRef<TAssetType>& asset, TSRef<TLoaderType>& loader, const std::string& path,
                                  const uint8* data = nullptr, uint32 data_size = 0)
    {
        AssertMsg(asset->bIsUploadedToGpu == false, "Asset is already uploaded!");

        // Ref<LoaderType> loader = Ref<LoaderType>::New();

        AxManager* mgr = GetInstance();

        if (data != nullptr) {
            AxQueueItem queue_item((loader), asset, TEnumValue, data, data_size);
            mgr->mLoadQueue.Push(std::move(queue_item));
        }
        else {
            AxQueueItem queue_item((loader), asset, TEnumValue, path);
            mgr->mLoadQueue.Push(std::move(queue_item));
        }

        mgr->ItemsEnqueued.test_and_set();
        mgr->ItemsEnqueuedNotifier.SignalDataWritten();
    }

public:
    //    DataNotifier DataLoaded;
private:
    AxQueue mLoadQueue;

    std::atomic_flag mbActive;

    DataNotifier ItemsEnqueuedNotifier;
    std::atomic_flag ItemsEnqueued;

    uint32 mMinThreads = 2;
    // SizedArray<std::thread *> mWorkerThreads;
    SizedArray<AxWorker> mWorkerThreads;
    std::thread* mpAssetManagerThread;
};

} // namespace fx
