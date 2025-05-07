#include "FxAssetManager.hpp"
#include "Asset/Loader/FxGltfLoader.hpp"
#include "Core/StaticArray.hpp"

#include "FxModel.hpp"

#include <chrono>
#include <memory>
#include <thread>

#include <Core/Types.hpp>

static FxAssetManager AssetManager;

////////////////////////////////////
// Asset Worker
////////////////////////////////////

void FxAssetWorker::Create()
{
    Thread = std::thread([this]() { FxAssetWorker::Update(); });
    Running.store(true);
}

void FxAssetWorker::Update()
{
    while (Running.load()) {
        ItemReady.WaitForData();

        if (Running.load() == false) {
            break;
        }

        LoadStatus = Item.Loader->LoadFromFile(Item.Asset, Item.Path);

        ItemReady.Reset();

        AssetManager.DataLoaded.SignalDataWritten();
    }
}


////////////////////////////////////
// Asset Manager
////////////////////////////////////


void FxAssetManager::Start(int32 thread_count)
{
    mThreadCount = thread_count;
    mActive = true;

    mWorkerThreads.InitCapacity(thread_count);

    for (int32 i = 0; i < thread_count; i++) {
        FxAssetWorker *worker = mWorkerThreads.Insert();
        worker->Create();

    }

    std::thread *thread = new std::thread([this]() { FxAssetManager::AssetThreadUpdate(); });
    mAssetThread = thread;
}

void FxAssetManager::Shutdown()
{
    if (mActive == false) {
        return;
    }

    mActive = false;
    DataAvailable.SignalDataWritten();

    // for (std::thread *thread : mWorkerThreads) {
    //     thread->join();
    //     delete thread;
    // }
    //
    for (auto &worker : mWorkerThreads) {
        worker.Running.store(false);
        worker.ItemReady.SignalDataWritten();
        worker.Thread.join();
    }

    mAssetThread->join();
    delete mAssetThread;
}

PtrContainer<FxModel> FxAssetManager::NewModel()
{
    return PtrContainer<FxModel>::New();
}

PtrContainer<FxModel> FxAssetManager::LoadModel(std::string path)
{
    PtrContainer<FxModel> model = NewModel();
    LoadModel(model, path);

    return model;
}

void FxAssetManager::LoadModel(PtrContainer<FxModel> &model, std::string path)
{
    FxAssetQueueItem queue_item;

    queue_item.Asset = model.Get();
    queue_item.Loader = std::make_unique<FxGltfLoader>();
    queue_item.AssetType = FxAssetType::Model;
    queue_item.Path = path;

    AssetManager.mLoadQueue.Push(queue_item);

    AssetManager.DataAvailable.SignalDataWritten();
}

void FxAssetManager::AssetThreadUpdate()
{
    while (mActive) {
        DataAvailable.WaitForData();
        if (!mActive) {
            break;
        }

        FxAssetQueueItem item;

        if (!mLoadQueue.PopIfAvailable(&item)) {
            continue;
        }

        FxAssetWorker &worker = FindWorkerThread();
        worker.Item = std::move(item);
        worker.ItemReady.SignalDataWritten();

        DataLoaded.WaitForData();

        auto &loaded_item = worker.Item;

        if (worker.LoadStatus == FxBaseLoader::Status::Success) {
            // Load the resouce into GPU memory
            loaded_item.Loader->CreateGpuResource(loaded_item.Asset);

            if (!loaded_item.Asset->IsLoaded) {
                loaded_item.Asset->IsLoaded.wait(true);
            }

            if (loaded_item.Asset->OnLoaded) {
                loaded_item.Asset->OnLoaded(loaded_item.Asset);
            }
        }
        else {
            if (loaded_item.Asset->OnError) {
                loaded_item.Asset->OnError(loaded_item.Asset);
            }
        }

        DataAvailable.Reset();
        DataLoaded.Reset();

        // FxBaseLoader::Status status = item.Loader->LoadFromFile(item.Asset, item.Path);

        // if (!item.Asset->IsLoaded) {
        //     item.Asset->IsLoaded.wait(true);
        //     // continue;
        // }

        // if (status == FxBaseLoader::Status::Success) {
        //     if (item.Asset->OnLoaded) {
        //         item.Asset->OnLoaded(item.Asset);
        //     }
        // }
        // else {
        //     if (item.Asset->OnError) {
        //         item.Asset->OnError(item.Asset);
        //     }
        // }
    }
}

FxAssetWorker &FxAssetManager::FindWorkerThread()
{
    // TODO: find the best worker based on items available
    for (FxAssetWorker &worker : mWorkerThreads) {
        if (!worker.IsBusy) {
            return worker;
        }
    }
    return mWorkerThreads[0];
}


void FxAssetManager::WorkerUpdate()
{
    while (mActive) {

    }
}

FxAssetManager &FxAssetManager::GetInstance()
{
    return AssetManager;
}
