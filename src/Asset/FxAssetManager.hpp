#pragma once

#include "Asset/FxAssetQueueItem.hpp"
#include "Core/Types.hpp"
#include "FxModel.hpp"
#include "Asset/FxAssetQueue.hpp"

#include <condition_variable>
#include <thread>

#include <Core/FxDataNotifier.hpp>

/**
 * Worker thread that waits and processes individual asset loading.
 */
class FxAssetWorker
{
public:
    FxAssetWorker()
    {

    }

    void Create();

    void DispatchAsset(FxAssetQueueItem &item)
    {
        IsBusy = true;
        Item = std::move(item);
    }

    void Update();

public:
    FxAssetQueueItem Item;
    FxBaseLoader::Status LoadStatus;
    FxDataNotifier ItemReady;

    std::atomic_bool Running = false;

    std::atomic_bool IsBusy = false;

    // FxDataProcessor Processor;
    std::thread Thread;
};

class FxAssetManager
{
public:
    void Start(int32 thread_count);
    void Shutdown();

    void WorkerUpdate();
    void AssetThreadUpdate();

    static FxAssetManager &GetInstance();

    static PtrContainer<FxModel> NewModel();
    static PtrContainer<FxModel> LoadModel(std::string path);
    static void LoadModel(PtrContainer<FxModel> &model, std::string path);

    ~FxAssetManager()
    {
        Shutdown();
    }

private:
    // void NotifyAssetOnLoad(FxAssetQueueItem &item);
    FxAssetWorker &FindWorkerThread();

public:
    FxDataNotifier DataLoaded;
private:
    FxAssetQueue mLoadQueue;

    std::atomic_bool mActive = false;
    FxDataNotifier DataAvailable;


    int32 mThreadCount = 2;
    // StaticArray<std::thread *> mWorkerThreads;
    StaticArray<FxAssetWorker> mWorkerThreads;
    std::thread *mAssetThread;
};


// FxModel *model = FxAssetManager::LoadModel("Test.gltf");
//
// create new FxModel
// FxModel has a data buffer, extends onload, has init functions that will be called
// the FxModel is queued in LoadQueue
// When there is available time, the Loader will be called(Loader.Load)
// Notify other functions that the asset has been created.
//
//
// model.OnLoaded([]() {}); -> called when loaded
// model.Render(); -> waits for loaded
