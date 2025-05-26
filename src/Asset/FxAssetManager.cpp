#include "FxAssetManager.hpp"
#include "Asset/Loader/FxGltfLoader.hpp"
#include "Core/FxStaticArray.hpp"

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

        // Call our specialized loader to load the asset file
        LoadStatus = Item.Loader->LoadFromFile(Item.Asset, Item.Path);

        // Mark that we are waiting for the data to be uploaded to the GPU
        DataPendingUpload.test_and_set();

        // Signal the main asset thread that we are done
        AssetManager.DataLoaded.SignalDataWritten();
    }
}


////////////////////////////////////
// Asset Manager
////////////////////////////////////


void FxAssetManager::Start(int32 thread_count)
{
    mThreadCount = thread_count;
    mActive.test_and_set();

    // Allocate the workers
    mWorkerThreads.InitCapacity(thread_count);

    for (int32 i = 0; i < thread_count; i++) {
        // 'Insert' a new worker and get its pointer
        FxAssetWorker *worker = mWorkerThreads.Insert();

        // Create the worker from the newly inserted pointer
        worker->Create();
    }

    std::thread *thread = new std::thread([this]() { FxAssetManager::AssetManagerUpdate(); });
    mAssetManagerThread = thread;
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

        // The worker waits on the ItemReady notifier, so we kill the notifier
        worker.ItemReady.Kill();

        worker.Thread.join();
    }

    mAssetManagerThread->join();
    delete mAssetManagerThread;
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

    AssetManager.ItemsEnqueued.test_and_set();
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

        // The asset was successfully loaded, upload to GPU
        if (worker.LoadStatus == FxBaseLoader::Status::Success) {
            // Load the resouce into GPU memory
            loaded_item.Loader->CreateGpuResource(loaded_item.Asset);
            while (!loaded_item.Asset->IsUploadedToGpu) {
                loaded_item.Asset->IsUploadedToGpu.wait(true);
            }

            // Call the OnLoaded callback if it was registered.
            if (loaded_item.Asset->OnLoaded) {
                loaded_item.Asset->OnLoaded(loaded_item.Asset);
            }
        }
        else {
            // There was an error, call the OnError callback if it was registered.
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
        // The load queue is currently in use(uploaded to), skip for now.
        return;
    }

    FxAssetWorker *worker = FindWorkerThread();

    // No workers available, poll until one becomes available
    while (worker == nullptr) {
        Log::Debug("No workers available; Polling for worker thread...");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Check to see if any threads opened up
        worker = FindWorkerThread();
    }

    // Submit the item we want to load
    worker->SubmitItemToLoad(item);
}

void FxAssetManager::AssetManagerUpdate()
{
    while (mActive.test()) {
        bool is_busy = CheckWorkersBusy();

        // If any of the workers are still marked as busy, there is data
        // either to be uploaded or is currently being loaded.
        if (is_busy) {
            // Loop while we are busy to check for when we are pending upload, as well
            // as check for new arrivals.
            while (CheckWorkersBusy()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));

                CheckForUploadableData();
                CheckForItemsToLoad();
            }
        }

        // There are no busy workers remaining, wait for the next item to be enqueued.
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
