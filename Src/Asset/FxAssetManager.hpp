#pragma once

#include "Asset/FxAssetQueueItem.hpp"
#include "Asset/FxAssetQueue.hpp"

#include "FxModel.hpp"
#include "FxImage.hpp"

#include <Core/Types.hpp>
#include <Core/FxRef.hpp>

#include <atomic>
#include <thread>

#include <Core/FxDataNotifier.hpp>

template <typename T>
concept C_IsAsset = std::is_base_of_v<FxBaseAsset, T>;

/**
 * Worker thread that waits and processes individual asset loading.
 */
class FxAssetWorker
{
public:
    FxAssetWorker() = default;

    void Create();
d
    void SubmitItemToLoad(FxAssetQueueItem &item)
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
    FxBaseLoader::Status LoadStatus = FxBaseLoader::Status::None;

    FxDataNotifier ItemReady;

    std::atomic_flag Running = ATOMIC_FLAG_INIT;
    std::atomic_flag IsBusy = ATOMIC_FLAG_INIT;

    std::atomic_flag DataPendingUpload = ATOMIC_FLAG_INIT;

    std::thread Thread;
};

class FxAssetManager
{
public:
    FxAssetManager() = default;

    void Start(int32 thread_count);
    void Shutdown();

    void WorkerUpdate();

    static FxAssetManager &GetInstance();

    template <typename T> requires C_IsAsset<T>
    static FxRef<T> NewAsset()
    {
        // return PtrContainer<T>::New();
        return FxRef<T>::New();
    }

    template <typename T> requires C_IsAsset<T>
    static FxRef<T> LoadAsset(const std::string& path)
    {
        FxRef<T> asset = FxRef<T>::New();
        LoadAsset<T>(asset, path);

        return asset;
    }

    template <typename T> requires C_IsAsset<T>
    static FxRef<T> LoadFromMemory(const uint8* data, uint32 data_size)
    {
        FxRef<T> asset = FxRef<T>::New();
        LoadFromMemory<T>(asset, data, data_size);

        return asset;
    }


    // Specializations in cpp file
    template <typename T> requires C_IsAsset<T>
    static void LoadAsset(FxRef<T> asset, const std::string& path)
    {
        if constexpr (!std::is_same<T, FxImage>::value && !std::is_same<T, FxModel>::value) {
            static_assert(0, "Asset type is not implemented!");
        }
    }

    template <typename T> requires C_IsAsset<T>
    static void LoadFromMemory(FxRef<T> asset, const uint8* data, uint32 data_size)
    {
        if constexpr (!std::is_same<T, FxImage>::value && !std::is_same<T, FxModel>::value) {
            static_assert(0, "Asset type is not implemented!");
        }
    }

    ~FxAssetManager()
    {
        Shutdown();
    }

private:
    FxAssetWorker *FindWorkerThread();

    void CheckForUploadableData();
    void CheckForItemsToLoad();

    bool CheckWorkersBusy();

    void AssetManagerUpdate();

    template <typename AssetType, typename LoaderType, FxAssetType EnumValue> requires C_IsAsset<AssetType>
    static void DoLoadAsset(const FxRef<AssetType>& asset, const std::string& path)
    {
        FxRef<LoaderType> loader = FxRef<LoaderType>::New();

        FxAssetQueueItem queue_item(
            (loader),
            asset,
            EnumValue,
            path
        );

        FxAssetManager& mgr = GetInstance();

        mgr.mLoadQueue.Push(queue_item);

        mgr.ItemsEnqueued.test_and_set();
        mgr.ItemsEnqueuedNotifier.SignalDataWritten();
    }

    template <typename AssetType, typename LoaderType, FxAssetType EnumValue> requires C_IsAsset<AssetType>
    static void DoLoadFromMemory(const FxRef<AssetType>& asset, const uint8* data, uint32 data_size)
    {
        FxRef<LoaderType> loader = FxRef<LoaderType>::New();

        FxAssetQueueItem queue_item(
            (loader),
            asset,
            EnumValue,
            data,
            data_size
        );

        FxAssetManager& mgr = GetInstance();

        mgr.mLoadQueue.Push(queue_item);

        mgr.ItemsEnqueued.test_and_set();
        mgr.ItemsEnqueuedNotifier.SignalDataWritten();
    }

public:
    FxDataNotifier DataLoaded;
private:
    FxAssetQueue mLoadQueue;

    std::atomic_flag mActive;

    FxDataNotifier ItemsEnqueuedNotifier;
    std::atomic_flag ItemsEnqueued;

    int32 mThreadCount = 2;
    // FxSizedArray<std::thread *> mWorkerThreads;
    FxSizedArray<FxAssetWorker> mWorkerThreads;
    std::thread *mAssetManagerThread;
};
