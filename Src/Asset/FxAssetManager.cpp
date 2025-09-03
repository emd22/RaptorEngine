#include "FxAssetManager.hpp"

#include "Core/FxPanic.hpp"
#include "Core/FxSizedArray.hpp"
#include "FxAssetImage.hpp"
#include "FxAssetModel.hpp"
#include "Loader/FxLoaderGltf.hpp"
#include "Loader/FxLoaderJpeg.hpp"
#include "Loader/FxLoaderStb.hpp"

#include <Core/FxDefines.hpp>
#include <Core/Types.hpp>
#include <FxObject.hpp>
#include <atomic>
#include <chrono>
#include <thread>


////////////////////////////////////
// Asset Worker
////////////////////////////////////

void FxAssetWorker::Create()
{
    while (Running.test_and_set(std::memory_order_acquire)) {
        Running.wait(true, std::memory_order_relaxed);
    }

    Thread = std::thread([this]() { this->Update(); });
}

void FxAssetWorker::Update()
{
    // FxAssetManager& manager = FxAssetManager::GetInstance();

    while (Running.test()) {
        ItemReady.WaitForData();
        ItemReady.Reset();

        if (!Running.test()) {
            break;
        }

        // If there is data passed in then we load from memory
        if (Item.RawData != nullptr && Item.DataSize > 0) {
            LoadStatus = Item.Loader->LoadFromMemory(Item.Asset, Item.RawData, Item.DataSize);
        }
        // There is no data passed in, load from file
        else {
            // Call our specialized loader to load the asset file
            LoadStatus = Item.Loader->LoadFromFile(Item.Asset, Item.Path);
        }


        // Mark that we are waiting for the data to be uploaded to the GPU
        DataPendingUpload.test_and_set();

        // Signal the main asset thread that we are done
        //        manager.DataLoaded.SignalDataWritten();
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
        FxAssetWorker* worker = mWorkerThreads.Insert();

        // Create the worker from the newly inserted pointer
        worker->Create();
    }

    std::thread* thread = new std::thread([this]() { FxAssetManager::AssetManagerUpdate(); });
    mAssetManagerThread = thread;
}

void FxAssetManager::Shutdown()
{
    if (!mActive.test()) {
        return;
    }

    mActive.clear();
    ItemsEnqueuedNotifier.Kill();

    for (auto& worker : mWorkerThreads) {
        worker.Running.clear();
        worker.Running.notify_one();

        // The worker waits on the ItemReady notifier, so we kill the notifier
        worker.ItemReady.Kill();

        worker.Thread.join();
    }


    mAssetManagerThread->join();
    delete mAssetManagerThread;

    // Free the workers thread array here---
    // this calls the destructors for each worker thread,
    // which frees the `FxRef`'s to the data
    mWorkerThreads.Free();

    // mLoadQueue.Destroy();
}

// template<>
// void FxAssetManager::LoadAsset<FxAssetModel>(FxRef<FxAssetModel> asset, const std::string& path)
// {
//     DoLoadAsset<FxAssetModel, FxLoaderGltf, FxAssetType::Model>(asset, path);
// }


inline bool IsMemoryJpeg(const uint8* data, uint32 data_size)
{
    if (data_size < 120) {
        return false;
    }

    const bool header_correct = (data[0] == 0xFF) && (data[1] == 0xD8);
    const bool footer_correct = (data[data_size - 2] == 0xFF) && (data[data_size - 1] == 0xD9);

    return header_correct && footer_correct;
}


inline bool IsFileJpeg(const std::string& path)
{
    const char* path_cstr = path.c_str();

    FILE* fp = fopen(path_cstr, "rb");

    if (fp == nullptr) {
        return false;
    }

    constexpr size_t magic_size = sizeof(uint16);
    uint8 magic_buffer[2];

    if (fread(magic_buffer, 1, magic_size, fp) != magic_size) {
        fclose(fp);
        return false;
    }

    fclose(fp);

    if (magic_buffer[0] == 0xFF && magic_buffer[1] == 0xD8) {
        return true;
    }

    return false;
}

void FxAssetManager::LoadObject(FxRef<FxObject>& asset, const std::string& path)
{
    FxRef<FxLoaderGltf> loader = FxRef<FxLoaderGltf>::New();

    SubmitAssetToLoad<FxObject, FxLoaderGltf, FxAssetType::Object>(asset, loader, path);
}


void FxAssetManager::LoadObjectFromMemory(FxRef<FxObject>& asset, const uint8* data, uint32 data_size)
{
    FxRef<FxLoaderGltf> loader = FxRef<FxLoaderGltf>::New();

    SubmitAssetToLoad<FxObject, FxLoaderGltf, FxAssetType::Object>(asset, loader, "", data, data_size);
}


void FxAssetManager::LoadImage(RxImageType image_type, FxRef<FxAssetImage>& asset, const std::string& path)
{
    bool is_jpeg = IsFileJpeg(path);

    if (is_jpeg) {
        FxRef<FxLoaderJpeg> loader = FxRef<FxLoaderJpeg>::New();
        loader->ImageType = image_type;

        SubmitAssetToLoad<FxAssetImage, FxLoaderJpeg, FxAssetType::Image>(asset, loader, path);
    }
    else {
        FxRef<FxLoaderStb> loader = FxRef<FxLoaderStb>::New();
        loader->ImageType = image_type;

        SubmitAssetToLoad<FxAssetImage, FxLoaderStb, FxAssetType::Image>(asset, loader, path);
    }
}


void FxAssetManager::LoadImageFromMemory(RxImageType image_type, FxRef<FxAssetImage>& asset, const uint8* data,
                                         uint32 data_size)
{
    if (IsMemoryJpeg(data, data_size)) {
        // Load the image using turbojpeg
        FxRef<FxLoaderJpeg> loader = FxRef<FxLoaderJpeg>::New();
        loader->ImageType = image_type;

        SubmitAssetToLoad<FxAssetImage, FxLoaderJpeg, FxAssetType::Image>(asset, loader, "", data, data_size);
    }
    else {
        // Load the image using stb_image
        FxRef<FxLoaderStb> loader = FxRef<FxLoaderStb>::New();
        loader->ImageType = image_type;

        SubmitAssetToLoad<FxAssetImage, FxLoaderStb, FxAssetType::Image>(asset, loader, "", data, data_size);
    }
}


void FxAssetManager::CheckForUploadableData()
{
    for (auto& worker : mWorkerThreads) {
        // If there are no uploads pending, skip the worker
        if (!worker.DataPendingUpload.test()) {
            continue;
        }

        auto& loaded_item = worker.Item;

        // The asset was successfully loaded, upload to GPU
        if (worker.LoadStatus == FxLoaderBase::Status::Success) {
            // Load the resouce into GPU memory
            loaded_item.Loader->CreateGpuResource(loaded_item.Asset);

            while (!loaded_item.Asset->IsUploadedToGpu) {
                loaded_item.Asset->IsUploadedToGpu.wait(true);
            }


            // Call the OnLoaded callback if it was registered
            if (!loaded_item.Asset->mOnLoadedCallbacks.empty()) {
                for (auto& callback : loaded_item.Asset->mOnLoadedCallbacks) {
                    callback(loaded_item.Asset);
                }
                // loaded_item.Asset->mOnLoadedCallback(loaded_item.Asset);
            }

            loaded_item.Asset->IsFinishedNotifier.SignalDataWritten();
            loaded_item.Asset->mIsLoaded.store(true);

            //            FxRef<FxLoaderGltf> gltf_loader(loaded_item.Loader);
            //            gltf_loader->LoadAttachedMaterials();

            // Destroy the loader(clearing the loading buffers)
            loaded_item.Loader->Destroy(loaded_item.Asset);
        }
        else if (worker.LoadStatus == FxLoaderBase::Status::Error) {
            loaded_item.Asset->IsFinishedNotifier.SignalDataWritten();

            // There was an error, call the OnError callback if it was registered
            if (loaded_item.Asset->mOnErrorCallback) {
                loaded_item.Asset->mOnErrorCallback(loaded_item.Asset);
            }
        }
        else if (worker.LoadStatus == FxLoaderBase::Status::None) {
            loaded_item.Asset->IsFinishedNotifier.SignalDataWritten();

            FxPanic("FxAssetManager", "Worker status is none!", 0);
        }

        ItemsEnqueued.clear();
        worker.IsBusy.clear();
        worker.LoadStatus = FxLoaderBase::Status::None;

        worker.DataPendingUpload.clear();
    }
}

bool FxAssetManager::CheckWorkersBusy()
{
    for (auto& worker : mWorkerThreads) {
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

    FxAssetWorker* worker = FindWorkerThread();

    // No workers available, poll until one becomes available
    while (worker == nullptr) {
        OldLog::Debug("No workers available; Polling for worker thread...");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

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

                // Check if there is data to be uploaded to the GPU
                CheckForUploadableData();

                // Check if there are any items that can be loaded by a worker
                CheckForItemsToLoad();
            }
        }

        // There are no busy workers remaining, wait for the next item to be enqueued.
        ItemsEnqueuedNotifier.WaitForData();
        ItemsEnqueuedNotifier.Reset();
        if (!mActive.test()) {
            break;
        }

        CheckForItemsToLoad();
    }
}

FxAssetWorker* FxAssetManager::FindWorkerThread()
{
    for (FxAssetWorker& worker : mWorkerThreads) {
        if (!worker.IsBusy.test()) {
            return &worker;
        }
    }
    return nullptr;
}

FxAssetManager& FxAssetManager::GetInstance()
{
    static FxAssetManager AssetManager;

    return AssetManager;
}
