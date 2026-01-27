#pragma once

#include "Asset/AxQueue.hpp"
#include "Asset/AxQueueItem.hpp"
#include "AxImage.hpp"
#include "FxObject.hpp"

#include <Core/FxDataNotifier.hpp>
#include <Core/FxRef.hpp>
#include <Core/FxTypes.hpp>
#include <atomic>
#include <thread>

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

    void SubmitItemToLoad(const AxQueueItem& item)
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
    AxQueueItem Item;
    AxLoaderBase::Status LoadStatus = AxLoaderBase::Status::eNone;

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

class AxManager
{
public:
    AxManager() = default;

    void Start(int32 thread_count);
    void Shutdown();

    void WorkerUpdate();

    static AxManager& GetInstance();

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

    static FxRef<AxImage> LoadImage(RxImageType image_type, RxImageFormat format, const std::string& path)
    {
        FxRef<AxImage> asset = FxRef<AxImage>::New();
        LoadImage(image_type, format, asset, path);

        return asset;
    }

    static inline FxRef<AxImage> LoadImage(const std::string& path, RxImageFormat format)
    {
        return LoadImage(RxImageType::e2d, format, path);
    }

    /**
     * @brief Creates a new `FxObject` and loads the asset into it from
     * the data provided.
     */
    static FxRef<AxImage> LoadImageFromMemory(RxImageType image_type, RxImageFormat format, const uint8* data,
                                              uint32 data_size)
    {
        FxRef<AxImage> asset = FxRef<AxImage>::New();
        LoadImageFromMemory(image_type, format, asset, data, data_size);

        return asset;
    }

    static inline FxRef<AxImage> LoadImageFromMemory(RxImageFormat format, const uint8* data, uint32 data_size)
    {
        return LoadImageFromMemory(RxImageType::e2d, format, data, data_size);
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

    static void LoadImage(RxImageType image_type, RxImageFormat format, FxRef<AxImage>& asset, const std::string& path);

    /**
     * @brief Loads an Image2D from the path provided into `asset`.
     */
    static inline void LoadImage(FxRef<AxImage>& asset, RxImageFormat format, const std::string& path)
    {
        return LoadImage(RxImageType::e2d, format, asset, path);
    }


    static void LoadImageFromMemory(RxImageType image_type, RxImageFormat format, FxRef<AxImage>& asset,
                                    const uint8* data, uint32 data_size);

    /**
     * @brief Loads an Image2D from the data provided into `asset`.
     */
    static void LoadImageFromMemory(FxRef<AxImage>& asset, RxImageFormat format, const uint8* data, uint32 data_size)
    {
        return LoadImageFromMemory(RxImageType::e2d, format, asset, data, data_size);
    }

    ~AxManager() { Shutdown(); }


private:
    AxWorker* FindWorkerThread();

    void CheckForUploadableData();
    void CheckForItemsToLoad();

    bool CheckWorkersBusy();

    void AssetManagerUpdate();

    template <typename TAssetType, typename TLoaderType, AxType TEnumValue>
        requires C_IsAsset<TAssetType>
    static void SubmitAssetToLoad(const FxRef<TAssetType>& asset, FxRef<TLoaderType>& loader, const std::string& path,
                                  const uint8* data = nullptr, uint32 data_size = 0)
    {
        if (asset->bIsUploadedToGpu) {
            printf("*** DELETING ***\n");
            asset->Destroy();
        }

        // FxRef<LoaderType> loader = FxRef<LoaderType>::New();

        AxManager& mgr = GetInstance();

        if (data != nullptr) {
            AxQueueItem queue_item((loader), asset, TEnumValue, data, data_size);
            mgr.mLoadQueue.Push(queue_item);
        }
        else {
            AxQueueItem queue_item((loader), asset, TEnumValue, path);
            mgr.mLoadQueue.Push(queue_item);
        }

        mgr.ItemsEnqueued.test_and_set();
        mgr.ItemsEnqueuedNotifier.SignalDataWritten();
    }

public:
    //    FxDataNotifier DataLoaded;
private:
    AxQueue mLoadQueue;

    std::atomic_flag mActive;

    FxDataNotifier ItemsEnqueuedNotifier;
    std::atomic_flag ItemsEnqueued;

    int32 mThreadCount = 2;
    // FxSizedArray<std::thread *> mWorkerThreads;
    FxSizedArray<AxWorker> mWorkerThreads;
    std::thread* mAssetManagerThread;
};
