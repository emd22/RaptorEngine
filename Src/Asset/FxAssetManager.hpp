#pragma once

#include "Asset/FxAssetQueue.hpp"
#include "Asset/FxAssetQueueItem.hpp"
#include "FxAssetImage.hpp"
#include "FxObject.hpp"

#include <Core/FxDataNotifier.hpp>
#include <Core/FxRef.hpp>
#include <Core/FxTypes.hpp>
#include <atomic>
#include <thread>

template <typename T>
concept C_IsAsset = std::is_base_of_v<FxAssetBase, T>;

/**
 * Worker thread that waits and processes individual asset loading.
 */
class FxAssetWorker
{
public:
    FxAssetWorker() = default;

    void Create();

    void SubmitItemToLoad(const FxAssetQueueItem& item)
    {
        // Wait for item to no longer be busy
        while (IsBusy.test()) {
            IsBusy.wait(true);
        }
        // Set busy
        IsBusy.test_and_set();

        Item = (item);
        ItemReady.SignalDataWritten();
    }

    void Update();

public:
    FxAssetQueueItem Item;
    FxLoaderBase::Status LoadStatus = FxLoaderBase::Status::None;

    FxDataNotifier ItemReady;

    std::atomic_flag Running = ATOMIC_FLAG_INIT;
    std::atomic_flag IsBusy = ATOMIC_FLAG_INIT;

    std::atomic_flag DataPendingUpload = ATOMIC_FLAG_INIT;

    std::thread Thread;
};

struct FxLoadObjectOptions
{
    bool KeepInMemory = false;
};

class FxAssetManager
{
public:
    FxAssetManager() = default;

    void Start(int32 thread_count);
    void Shutdown();

    void WorkerUpdate();

    static FxAssetManager& GetInstance();

    template <typename T>
        requires C_IsAsset<T>
    static FxRef<T> NewAsset()
    {
        return FxRef<T>::New();
    }


    ////////////////////////////////////////////////
    // Methods to create and load objects
    ////////////////////////////////////////////////


    /**
     * @brief Creates a new `FxObject` and loads the provided asset into it from
     * the path provided.
     */
    static FxRef<FxObject> LoadObject(const std::string& path, FxLoadObjectOptions options = {})
    {
        FxRef<FxObject> asset = FxRef<FxObject>::New();
        LoadObject(asset, path, options);

        return asset;
    }

    /**
     * @brief Creates a new `FxObject` and loads the asset into it from
     * the data provided.
     */
    static FxRef<FxObject> LoadObjectFromMemory(const uint8* data, uint32 data_size)
    {
        FxRef<FxObject> asset = FxRef<FxObject>::New();
        LoadObjectFromMemory(asset, data, data_size);

        return asset;
    }

    static FxRef<FxAssetImage> LoadImage(RxImageType image_type, const std::string& path)
    {
        FxRef<FxAssetImage> asset = FxRef<FxAssetImage>::New();
        LoadImage(image_type, asset, path);

        return asset;
    }

    static inline FxRef<FxAssetImage> LoadImage(const std::string& path) { return LoadImage(RxImageType::Image, path); }

    /**
     * @brief Creates a new `FxObject` and loads the asset into it from
     * the data provided.
     */
    static FxRef<FxAssetImage> LoadImageFromMemory(RxImageType image_type, const uint8* data, uint32 data_size)
    {
        FxRef<FxAssetImage> asset = FxRef<FxAssetImage>::New();
        LoadImageFromMemory(image_type, asset, data, data_size);

        return asset;
    }

    static inline FxRef<FxAssetImage> LoadImageFromMemory(const uint8* data, uint32 data_size)
    {
        return LoadImageFromMemory(RxImageType::Image, data, data_size);
    }


    ////////////////////////////////////////////////
    // Methods to load into existing containers
    ////////////////////////////////////////////////

    /**
     * @brief Loads an object into the provided asset from a path.
     */
    static void LoadObject(FxRef<FxObject>& asset, const std::string& path, FxLoadObjectOptions options = {});

    /**
     * @brief Loads an asset into the provided asset from the provided data.
     */
    static void LoadObjectFromMemory(FxRef<FxObject>& asset, const uint8* data, uint32 data_size);

    static void LoadImage(RxImageType image_type, FxRef<FxAssetImage>& asset, const std::string& path);

    /**
     * @brief Loads an Image2D from the path provided into `asset`.
     */
    static inline void LoadImage(FxRef<FxAssetImage>& asset, const std::string& path)
    {
        return LoadImage(RxImageType::Image, asset, path);
    }


    static void LoadImageFromMemory(RxImageType image_type, FxRef<FxAssetImage>& asset, const uint8* data,
                                    uint32 data_size);

    /**
     * @brief Loads an Image2D from the data provided into `asset`.
     */
    static void LoadImageFromMemory(FxRef<FxAssetImage>& asset, const uint8* data, uint32 data_size)
    {
        return LoadImageFromMemory(RxImageType::Image, asset, data, data_size);
    }


    ~FxAssetManager() { Shutdown(); }

private:
    FxAssetWorker* FindWorkerThread();

    void CheckForUploadableData();
    void CheckForItemsToLoad();

    bool CheckWorkersBusy();

    void AssetManagerUpdate();

    template <typename AssetType, typename LoaderType, FxAssetType EnumValue>
        requires C_IsAsset<AssetType>
    static void SubmitAssetToLoad(const FxRef<AssetType>& asset, FxRef<LoaderType>& loader, const std::string& path,
                                  const uint8* data = nullptr, uint32 data_size = 0)
    {
        if (asset->IsUploadedToGpu) {
            printf("*** DELETING ***\n");
            asset->Destroy();
        }

        // FxRef<LoaderType> loader = FxRef<LoaderType>::New();

        FxAssetManager& mgr = GetInstance();

        if (data != nullptr) {
            FxAssetQueueItem queue_item((loader), asset, EnumValue, data, data_size);
            mgr.mLoadQueue.Push(queue_item);
        }
        else {
            FxAssetQueueItem queue_item((loader), asset, EnumValue, path);
            mgr.mLoadQueue.Push(queue_item);
        }

        mgr.ItemsEnqueued.test_and_set();
        mgr.ItemsEnqueuedNotifier.SignalDataWritten();
    }

public:
    //    FxDataNotifier DataLoaded;
private:
    FxAssetQueue mLoadQueue;

    std::atomic_flag mActive;

    FxDataNotifier ItemsEnqueuedNotifier;
    std::atomic_flag ItemsEnqueued;

    int32 mThreadCount = 2;
    // FxSizedArray<std::thread *> mWorkerThreads;
    FxSizedArray<FxAssetWorker> mWorkerThreads;
    std::thread* mAssetManagerThread;
};
