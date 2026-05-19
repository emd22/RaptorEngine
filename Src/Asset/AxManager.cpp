#include "AxManager.hpp"

#include "AxImage.hpp"
#include "Core/Panic.hpp"
#include "Core/SizedArray.hpp"
#include "Loader/AxLoaderGltf.hpp"
#include "Loader/AxLoaderJpeg.hpp"
#include "Loader/AxLoaderStb.hpp"

#include <Core/Defines.hpp>
#include <Core/Types.hpp>
#include <Object.hpp>
#include <atomic>
#include <chrono>
#include <thread>

namespace fx {

static constexpr uint32 scMaxWorkerThreads = 10;


////////////////////////////////////
// Asset Worker
////////////////////////////////////

void AxWorker::Create()
{
    Thread = std::thread([this]() { this->Update(); });
}

void AxWorker::Update()
{
    while (bRunning.load()) {
        ItemReady.WaitForData();
        ItemReady.Reset();

        // In order to stop the worker, we need to kill the ItemReady notifier. Here we need to check if the worker
        // actually recieved data. This is also cheaper than calling ItemReady.IsKilled, but that shouldn't be a
        // bottleneck anyway.
        if (!bRunning.load()) {
            break;
        }

        // Retrieve the loader and asset from the item
        LockContext<AxItemData> asset_data = Item.GetDataContext();

        // If there is data passed in then we load from memory
        if (Item.pcRawData != nullptr && Item.DataSize > 0) {
            LoadStatus = asset_data->pLoader->LoadFromMemory(asset_data->pAsset, Item.pcRawData, Item.DataSize);

            // Data is loaded, free it
            // gEnginePool->FreeRaw(static_cast<void*>(const_cast<uint8*>(Item.pcRawData)));
            std::free(static_cast<void*>(const_cast<uint8*>(Item.pcRawData)));
        }
        // There is no data passed in, load from file
        else {
            // Call our specialized loader to load the asset file
            LoadStatus = asset_data->pLoader->LoadFromFile(asset_data->pAsset, Item.Path);
        }


        // Mark that we are waiting for the data to be uploaded to the GPU
        bDataPendingUpload.test_and_set();
    }
}


////////////////////////////////////
// Asset Manager
////////////////////////////////////


void AxManager::Start(int32 min_threads)
{
    mMinThreads = min_threads;
    mbActive.test_and_set();

    // Allocate the workers
    mWorkerThreads.InitCapacity(scMaxWorkerThreads);

    for (int32 i = 0; i < mMinThreads; i++) {
        // 'Insert' a new worker and get its pointer
        AxWorker* worker = mWorkerThreads.Insert();

        // Create the worker from the newly inserted pointer
        worker->Create();
    }

    mpAssetManagerThread = new std::thread([this]() { AxManager::AssetManagerUpdate(); });
}

void AxManager::DebugPrintWorkers() const
{
    for (const AxWorker& worker : mWorkerThreads) {
        worker.DebugPrint();
    }
}

void AxManager::Shutdown()
{
    LogInfo(LC_ASSET, "Shutting down asset manager...");

    if (!mbActive.test()) {
        return;
    }

    mbActive.clear();
    ItemsEnqueuedNotifier.Kill();

    for (auto& worker : mWorkerThreads) {
        worker.Kill();
        worker.Thread.join();
    }

    mpAssetManagerThread->join();
    delete mpAssetManagerThread;

    mWorkerThreads.Free();

    // Cleanup all permutations of empty images that were created.
    PagedArray<TSRef<AxImage>>& empty_images_list = AxImage::GetEmptyImagesArray();

    if (empty_images_list.IsInited()) {
        for (TSRef<AxImage>& image_ref : empty_images_list) {
            image_ref.DestroyRef();
        }
    }
}


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

void AxManager::LoadObject(const std::string& name, TSRef<Object>& asset, const std::string& path,
                           LoadObjectOptions options)
{
    TSRef<AxLoaderGltf> loader = TSRef<AxLoaderGltf>::New();
    loader->bKeepInMemory = options.bKeepInMemory || options.bGeneratePhysicsMesh;

    SubmitAssetToLoad<Object, AxLoaderGltf, eAssetType::Object>(asset, loader, path);
    asset->Name = name;
}


void AxManager::LoadObjectFromMemory(const std::string& name, TSRef<Object>& asset, const uint8* data, uint32 data_size)
{
    TSRef<AxLoaderGltf> loader = TSRef<AxLoaderGltf>::New();

    SubmitAssetToLoad<Object, AxLoaderGltf, eAssetType::Object>(asset, loader, "", data, data_size);
    asset->Name = name;
}


void AxManager::LoadImage(renderer::eImageType image_type, renderer::eImageFormat format, TSRef<AxImage>& asset,
                          const std::string& path, eImageCreateFlags flags)
{
    bool is_jpeg = IsFileJpeg(path);

    if (is_jpeg) {
        TSRef<AxLoaderJpeg> loader = TSRef<AxLoaderJpeg>::New();
        loader->ImageType = image_type;
        loader->ImageFormat = format;
        loader->CreationFlags = flags;

        SubmitAssetToLoad<AxImage, AxLoaderJpeg, eAssetType::Image>(asset, loader, path);
    }
    else {
        TSRef<AxLoaderStb> loader = TSRef<AxLoaderStb>::New();
        loader->ImageType = image_type;
        loader->ImageFormat = format;
        loader->CreationFlags = flags;

        SubmitAssetToLoad<AxImage, AxLoaderStb, eAssetType::Image>(asset, loader, path);
    }
}


void AxManager::LoadImageFromMemory(renderer::eImageType image_type, renderer::eImageFormat format,
                                    TSRef<AxImage>& asset, const uint8* data, uint32 data_size)
{
    if (IsMemoryJpeg(data, data_size)) {
        // Load the image using turbojpeg
        TSRef<AxLoaderJpeg> loader = TSRef<AxLoaderJpeg>::New();
        loader->ImageType = image_type;
        loader->ImageFormat = format;

        SubmitAssetToLoad<AxImage, AxLoaderJpeg, eAssetType::Image>(asset, loader, "", data, data_size);
    }
    else {
        // Load the image using stb_image
        TSRef<AxLoaderStb> loader = TSRef<AxLoaderStb>::New();
        loader->ImageType = image_type;
        loader->ImageFormat = format;

        SubmitAssetToLoad<AxImage, AxLoaderStb, eAssetType::Image>(asset, loader, "", data, data_size);
    }
}


void AxManager::CheckForUploadableData()
{
    constexpr int32 scMaxUploadsPerTick = 2;

    int32 upload_counter = scMaxUploadsPerTick;

    for (auto& worker : mWorkerThreads) {
        // If there are no uploads pending, skip the worker
        if (!worker.bDataPendingUpload.test()) {
            continue;
        }

        LockContext<AxItemData> asset_data = worker.Item.GetDataContext();

        // The asset was successfully loaded, upload to GPU
        if (worker.LoadStatus == AxLoaderBase::eStatus::Success) {
            // Load the resouce into GPU memory
            asset_data->pLoader->CreateGpuResource(asset_data->pAsset);

            while (!asset_data->pAsset->bIsUploadedToGpu) {
                asset_data->pAsset->bIsUploadedToGpu.wait(true);
            }


            {
                std::lock_guard guard(asset_data->pAsset->mCallbackMutex);

                // Call OnLoaded callbacks if they are attached
                if (!asset_data->pAsset->mOnLoadedCallbacks.empty()) {
                    for (auto& callback : asset_data->pAsset->mOnLoadedCallbacks) {
                        callback(asset_data->pAsset);
                    }
                }

                asset_data->pAsset->mOnLoadedCallbacks.clear();
            }

            // Notify the asset thread that loading is finished

            asset_data->pAsset->IsFinishedNotifier.SignalDataWritten();
            asset_data->pAsset->mIsLoaded.store(true);

            // Destroy the loader(clearing the loading buffers)
            asset_data->pLoader->Destroy(asset_data->pAsset);
        }
        else if (worker.LoadStatus == AxLoaderBase::eStatus::Error) {
            asset_data->pAsset->IsFinishedNotifier.SignalDataWritten();

            // There was an error, call the OnError callback if it was registered
            if (asset_data->pAsset->mOnErrorCallback) {
                asset_data->pAsset->mOnErrorCallback(asset_data->pAsset);
            }
        }
        else if (worker.LoadStatus == AxLoaderBase::eStatus::None) {
            asset_data->pAsset->IsFinishedNotifier.SignalDataWritten();

            Panic("AssetManager", "Worker status is none!");
        }

        ItemsEnqueued.clear();
        worker.bIsBusy.clear();
        worker.LoadStatus = AxLoaderBase::eStatus::None;

        worker.bDataPendingUpload.clear();

        if ((--upload_counter) <= 0) {
            break;
        }
    }
}

bool AxManager::CheckWorkersBusy()
{
    for (auto& worker : mWorkerThreads) {
        if (worker.bIsBusy.test()) {
            return true;
        }
    }
    return false;
}

void AxManager::AddWorkerThread()
{
    // At the maximum number of workers, break
    if (mWorkerThreads.Size >= mWorkerThreads.Capacity) {
        LogError(LC_ASSET, "Reached maximum number of worker threads");
        return;
    }

    AxWorker* worker = mWorkerThreads.Insert();
    worker->Create();
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
            LogError(LC_ASSET, "Could not find worker thread, skipping load of object...");
            DebugPrintWorkers();
            break;
        }

        // AddWorkerThread();

        LogWarning(LC_ASSET, "Currently {} items are enqueued...", mLoadQueue.Size());
        LogError(LC_ASSET, "No workers available; Polling for worker thread...");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // Check to see if any threads opened up
        worker = FindWorkerThread();

        --tries_remaining;
    }

    if (worker) {
        if (worker->bIsBusy.test()) {
            // Return to sender
            mLoadQueue.Push(std::move(item));
            return;
        }

        // Set busy
        worker->bIsBusy.test_and_set();

        // Submit the item we want to load
        worker->SubmitItemToLoad(std::move(item));
    }
}

void AxManager::AssetManagerUpdate()
{
    while (mbActive.test()) {
        bool is_busy = CheckWorkersBusy();

        // If any of the workers are still marked as busy, there is data
        // either to be uploaded or is currently being loaded.
        if (is_busy) {
            // Loop while we are busy to check for when we are pending upload, as well
            // as check for new arrivals.
            while (CheckWorkersBusy() && mbActive.test()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));

                // Check if there is data to be uploaded to the GPU
                CheckForUploadableData();

                // Check if there are any items that can be loaded by a worker
                CheckForItemsToLoad();
            }
        }
        // There is no data being loaded or uploaded, we can unload the extra worker threads.
        // else {
        //     const uint32 amount_threads = mWorkerThreads.Size;
        //     for (uint32 index = amount_threads; index > mMinThreads; index--) {
        //         LogInfo(LC_ASSET, "Killing asset manager worker thread...");

        //         // mWorkerThreads[index].Kill();
        //         // mWorkerThreads[index].Thread.join();
        //         // mWorkerThreads.RemoveLast();
        //     }
        // }

        // There are no busy workers remaining, wait for the next item to be enqueued.
        ItemsEnqueuedNotifier.WaitForData();
        ItemsEnqueuedNotifier.Reset();
        if (!mbActive.test()) {
            break;
        }

        CheckForItemsToLoad();
    }
}

AxWorker* AxManager::FindWorkerThread()
{
    uint32 worker_id = 0;
    for (AxWorker& worker : mWorkerThreads) {
        ++worker_id;
        if (!worker.bIsBusy.test()) {
            LogInfo(LC_ASSET, "Found worker (id={})", worker_id);
            return &worker;
        }
    }
    LogInfo(LC_ASSET, "Did not find any open worker!");

    return nullptr;
}

AxManager* AxManager::GetInstance() { return gAssetManager; }

} // namespace fx
