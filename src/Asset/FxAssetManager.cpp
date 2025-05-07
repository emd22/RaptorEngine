#include "FxAssetManager.hpp"
#include "Asset/Loader/FxGltfLoader.hpp"
#include "Core/StaticArray.hpp"

#include "FxModel.hpp"

#include <atomic>
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
    while (Running.test_and_set(std::memory_order_acquire)) {
        Running.wait(true, std::memory_order_relaxed);
    }
}

void FxAssetWorker::Update()
{
    while (Running.test()) {
        ItemReady.WaitForData();

        if (!Running.test()) {
            break;
        }

        // Call our specialized loader
        LoadStatus = Item.Loader->LoadFromFile(Item.Asset, Item.Path);

        // Signal the main asset thread that we are done
        AssetManager.DataLoaded.SignalDataWritten();

        // IsBusy.clear();
        DataPendingUpload.test_and_set();
    }
}


////////////////////////////////////
// Asset Manager
////////////////////////////////////


void FxAssetManager::Start(int32 thread_count)
{
    mThreadCount = thread_count;
    mActive.test_and_set();

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
    if (!mActive.test()) {
        return;
    }

    mActive.clear();
    ItemsEnqueuedNotifier.Kill();

    for (auto &worker : mWorkerThreads) {
        worker.Running.clear();
        worker.Running.notify_one();

        // The worker waits on ItemReady, so we kill the notifier
        worker.ItemReady.Kill();

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
    while (AssetManager.ItemsEnqueued.test_and_set()) {
        AssetManager.ItemsEnqueued.wait(true, std::memory_order_relaxed);
    }

    AssetManager.ItemsEnqueuedNotifier.SignalDataWritten();
}

void FxAssetManager::CheckForUploadableData()
{
    for (auto &worker : mWorkerThreads) {
        // If there are no uploads pending, skip the worker
        if (!worker.DataPendingUpload.test()) {
            continue;
        }

        auto &loaded_item = worker.Item;

        if (worker.LoadStatus == FxBaseLoader::Status::Success) {
            // Load the resouce into GPU memory
            loaded_item.Loader->CreateGpuResource(loaded_item.Asset);

            if (!loaded_item.Asset->IsUploadedToGpu) {
                loaded_item.Asset->IsUploadedToGpu.wait(true);
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

        ItemsEnqueued.clear();
        worker.IsBusy.clear();
    }
}

bool FxAssetManager::CheckWorkersBusy()
{
    for (auto &worker : mWorkerThreads) {
        if (worker.IsBusy.test()) {
            return true;
        }
    }
    return false;
}

void FxAssetManager::CheckForItemsToLoad()
{
    FxAssetQueueItem item;
    if (!mLoadQueue.PopIfAvailable(&item)) {
        return;
    }

    // Find a worker thread
    FxAssetWorker *worker = FindWorkerThread();

    // No workers available, poll until one becomes available
    while (worker == nullptr) {
        Log::Debug("No workers available; Polling for worker thread...");

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        worker = FindWorkerThread();
    }

    // Submit the item we want to load
    worker->SubmitItemToLoad(item);
}

void FxAssetManager::AssetThreadUpdate()
{
    while (mActive.test()) {
        bool is_busy = CheckWorkersBusy();

        // If any of the workers are still marked as busy, there is data
        // either to be uploaded or is currently being loaded.
        if (is_busy) {
            // Loop while we are busy to check for when we are pending upload, as well
            // as check for new arrivals.
            while (CheckWorkersBusy()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                CheckForUploadableData();
                CheckForItemsToLoad();
            }
        }

        // Wait for items to be submitted
        ItemsEnqueuedNotifier.WaitForData();

        if (!mActive.test()) {
            break;
        }

        CheckForItemsToLoad();
    }
}

FxAssetWorker *FxAssetManager::FindWorkerThread()
{
    for (FxAssetWorker &worker : mWorkerThreads) {
        if (!worker.IsBusy.test()) {
            return &worker;
        }
    }
    return nullptr;
}

FxAssetManager &FxAssetManager::GetInstance()
{
    return AssetManager;
}
