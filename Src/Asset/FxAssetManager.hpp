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

/**
 * Worker thread that waits and processes individual asset loading.
 */
class FxAssetWorker
{
public:
    FxAssetWorker() = default;

    void Create();

    void SubmitItemToLoad(FxAssetQueueItem &item)
    {
        while (IsBusy.test_and_set())
            IsBusy.wait(true, std::memory_order_relaxed);

        Item = std::move(item);
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

    template <typename T>
    static FxRef<T> NewAsset()
    {
        // return PtrContainer<T>::New();
        return FxRef<T>::New();
    }

    template <typename T>
    static FxRef<T> LoadAsset(const std::string& path)
    {
        FxRef<T> asset = FxRef<T>::New();
        LoadAsset<T>(asset, path);

        return asset;
    }

    // Specializations in cpp file
    template <typename T>
    static void LoadAsset(FxRef<T> asset, const std::string& path)
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
