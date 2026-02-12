#include "AxManager.hpp"

#include "AxImage.hpp"
#include "Core/FxPanic.hpp"
#include "Core/FxSizedArray.hpp"
#include "Loader/AxLoaderGltf.hpp"
#include "Loader/AxLoaderJpeg.hpp"
#include "Loader/AxLoaderStb.hpp"

#include <Core/FxDefines.hpp>
#include <Core/FxTypes.hpp>
#include <FxObject.hpp>
#include <atomic>
#include <chrono>
#include <thread>


////////////////////////////////////
// Asset Worker
////////////////////////////////////

void AxWorker::Create()
{
    while (Running.test_and_set(std::memory_order_acquire)) {
        Running.wait(true, std::memory_order_relaxed);
    }

    Thread = std::thread([this]() { this->Update(); });
}

void AxWorker::Update()
{
    // FxAssetManager& manager = FxAssetManager::GetInstance();

    while (Running.test()) {
        ItemReady.WaitForData();
        ItemReady.Reset();

        if (!Running.test()) {
            break;
        }

        // Retrieve the loader and asset from the item
        FxLockContext<AxItemData> asset_data = Item.GetDataContext();

        // If there is data passed in then we load from memory
        if (Item.RawData != nullptr && Item.DataSize > 0) {
            LoadStatus = asset_data->pLoader->LoadFromMemory(asset_data->pAsset, Item.RawData, Item.DataSize);

            // LoadStatus = Item.pLoader->LoadFromMemory(Item.pAsset, Item.RawData, Item.DataSize);
        }
        // There is no data passed in, load from file
        else {
            LoadStatus = asset_data->pLoader->LoadFromFile(asset_data->pAsset, Item.Path);

            // Call our specialized loader to load the asset file
            // LoadStatus = Item.pLoader->LoadFromFile(Item.pAsset, Item.Path);
        }


        // Mark that we are waiting for the data to be uploaded to the GPU
        DataPendingUpload.test_and_set();
    }
}


////////////////////////////////////
// Asset Manager
////////////////////////////////////


void AxManager::Start(int32 thread_count)
{
    mThreadCount = thread_count;
    mActive.test_and_set();

    // Allocate the workers
    mWorkerThreads.InitCapacity(thread_count);

    for (int32 i = 0; i < thread_count; i++) {
        // 'Insert' a new worker and get its pointer
        AxWorker* worker = mWorkerThreads.Insert();

        // Create the worker from the newly inserted pointer
        worker->Create();
    }

    std::thread* thread = new std::thread([this]() { AxManager::AssetManagerUpdate(); });
    mAssetManagerThread = thread;
}

void AxManager::Shutdown()
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

    // Cleanup all permutations of "empty images" that were created.
    FxPagedArray<FxRef<AxImage>>& empty_images_list = AxImage::GetEmptyImagesArray();

    FxLogInfo("Number of empty images created: {}", empty_images_list.Size());

    if (empty_images_list.IsInited()) {
        for (FxRef<AxImage>& image_ref : empty_images_list) {
            image_ref.DestroyRef();
        }
    }

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

void AxManager::LoadObject(FxRef<FxObject>& asset, const std::string& path, FxLoadObjectOptions options)
{
    FxRef<AxLoaderGltf> loader = FxRef<AxLoaderGltf>::New();
    loader->bKeepInMemory = options.KeepInMemory;

    SubmitAssetToLoad<FxObject, AxLoaderGltf, AxType::eObject>(asset, loader, path);
}


void AxManager::LoadObjectFromMemory(FxRef<FxObject>& asset, const uint8* data, uint32 data_size)
{
    FxRef<AxLoaderGltf> loader = FxRef<AxLoaderGltf>::New();

    SubmitAssetToLoad<FxObject, AxLoaderGltf, AxType::eObject>(asset, loader, "", data, data_size);
}


void AxManager::LoadImage(RxImageType image_type, RxImageFormat format, FxRef<AxImage>& asset, const std::string& path)
{
    bool is_jpeg = IsFileJpeg(path);

    if (is_jpeg) {
        FxRef<AxLoaderJpeg> loader = FxRef<AxLoaderJpeg>::New();
        loader->ImageType = image_type;
        loader->ImageFormat = format;

        SubmitAssetToLoad<AxImage, AxLoaderJpeg, AxType::eImage>(asset, loader, path);
    }
    else {
        FxRef<AxLoaderStb> loader = FxRef<AxLoaderStb>::New();
        loader->ImageType = image_type;
        loader->ImageFormat = format;

        SubmitAssetToLoad<AxImage, AxLoaderStb, AxType::eImage>(asset, loader, path);
    }
}


void AxManager::LoadImageFromMemory(RxImageType image_type, RxImageFormat format, FxRef<AxImage>& asset,
                                    const uint8* data, uint32 data_size)
{
    if (IsMemoryJpeg(data, data_size)) {
        // Load the image using turbojpeg
        FxRef<AxLoaderJpeg> loader = FxRef<AxLoaderJpeg>::New();
        loader->ImageType = image_type;
        loader->ImageFormat = format;

        SubmitAssetToLoad<AxImage, AxLoaderJpeg, AxType::eImage>(asset, loader, "", data, data_size);
    }
    else {
        // Load the image using stb_image
        FxRef<AxLoaderStb> loader = FxRef<AxLoaderStb>::New();
        loader->ImageType = image_type;
        loader->ImageFormat = format;

        SubmitAssetToLoad<AxImage, AxLoaderStb, AxType::eImage>(asset, loader, "", data, data_size);
    }
}


void AxManager::CheckForUploadableData()
{
    for (auto& worker : mWorkerThreads) {
        // If there are no uploads pending, skip the worker
        if (!worker.DataPendingUpload.test()) {
            continue;
        }

        // std::lock_guard<std::mutex> guard(worker.Item.mMutex);

        // AxQueueItem& loaded_item = worker.Item;

        FxLockContext<AxItemData> asset_data = worker.Item.GetDataContext();


        // The asset was successfully loaded, upload to GPU
        if (worker.LoadStatus == AxLoaderBase::Status::eSuccess) {
            // Load the resouce into GPU memory
            asset_data->pLoader->CreateGpuResource(asset_data->pAsset);

            while (!asset_data->pAsset->bIsUploadedToGpu) {
                asset_data->pAsset->bIsUploadedToGpu.wait(true);
            }

            if (!asset_data->pAsset->mOnLoadedCallbacks.empty()) {
                for (auto& callback : asset_data->pAsset->mOnLoadedCallbacks) {
                    callback(asset_data->pAsset);
                }
            }

            asset_data->pAsset->IsFinishedNotifier.SignalDataWritten();
            asset_data->pAsset->mIsLoaded.store(true);

            // Destroy the loader(clearing the loading buffers)
            asset_data->pLoader->Destroy(asset_data->pAsset);
        }
        else if (worker.LoadStatus == AxLoaderBase::Status::eError) {
            asset_data->pAsset->IsFinishedNotifier.SignalDataWritten();

            // There was an error, call the OnError callback if it was registered
            if (asset_data->pAsset->mOnErrorCallback) {
                asset_data->pAsset->mOnErrorCallback(asset_data->pAsset);
            }
        }
        else if (worker.LoadStatus == AxLoaderBase::Status::eNone) {
            asset_data->pAsset->IsFinishedNotifier.SignalDataWritten();

            FxPanic("FxAssetManager", "Worker status is none!");
        }

        ItemsEnqueued.clear();
        worker.IsBusy.clear();
        worker.LoadStatus = AxLoaderBase::Status::eNone;

        worker.DataPendingUpload.clear();
    }
}

bool AxManager::CheckWorkersBusy()
{
    for (auto& worker : mWorkerThreads) {
        if (worker.IsBusy.test()) {
            return true;
        }
    }
    return false;
}

void AxManager::CheckForItemsToLoad()
{
    AxQueueItem item;
    if (!mLoadQueue.PopIfAvailable(&item)) {
        // The load queue is currently in use(uploaded to), skip for now.
        return;
    }

    AxWorker* worker = FindWorkerThread();

    // Set this to something small, but high enough that it won't cancel loading in a ton of big models at once.
    int tries_remaining = 5;

    // No workers available, poll until one becomes available
    while (worker == nullptr) {
        if (tries_remaining <= 0) {
            FxLogError("Could not find worker thread, breaking...");
            break;
        }

        FxLogError("No workers available; Polling for worker thread...");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // Check to see if any threads opened up
        worker = FindWorkerThread();

        --tries_remaining;
    }

    // Submit the item we want to load
    worker->SubmitItemToLoad(std::move(item));
}

void AxManager::AssetManagerUpdate()
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

AxWorker* AxManager::FindWorkerThread()
{
    for (AxWorker& worker : mWorkerThreads) {
        if (!worker.IsBusy.test()) {
            return &worker;
        }
    }
    return nullptr;
}

AxManager& AxManager::GetInstance()
{
    static AxManager AssetManager;

    return AssetManager;
}
