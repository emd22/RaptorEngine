#include "FxAssetManager.hpp"
#include "Asset/Loader/FxGltfLoader.hpp"
#include "Core/StaticArray.hpp"

#include "FxModel.hpp"

#include <chrono>
#include <memory>
#include <thread>

#include <Core/Types.hpp>

static FxAssetManager AssetManager;

void FxAssetManager::Start(int32 thread_count)
{
    mThreadCount = thread_count;
    mActive = true;

    mThreads.InitCapacity(thread_count);

    for (int32 i = 0; i < thread_count; i++) {
        std::thread *thread = new std::thread([this]() { FxAssetManager::ThreadUpdate(); });
        mThreads.Insert(thread);
    }
}

void FxAssetManager::Shutdown()
{
    if (mActive == false) {
        return;
    }

    mActive = false;

    for (std::thread *thread : mThreads) {
        thread->join();

        delete thread;
    }
}

PtrContainer<FxModel> FxAssetManager::NewModel()
{
    return PtrContainer<FxModel>::Create();
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

    AssetManager.mLoadQueue.Push(std::move(queue_item));
}

void FxAssetManager::ThreadUpdate()
{
    while (mActive) {
        FxAssetQueueItem item;

        if (!mLoadQueue.PopIfAvailable(&item)) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
            continue;
        }

        FxBaseLoader::Status status = item.Loader->LoadFromFile(item.Asset, item.Path);

        if (!item.Asset->IsLoaded) {
            item.Asset->IsLoaded.wait(true);
        }

        if (status == FxBaseLoader::Status::Success) {
            if (item.Asset->OnLoaded) {
                item.Asset->OnLoaded(item.Asset);
            }
        }
        else {
            if (item.Asset->OnError) {
                item.Asset->OnError(item.Asset);
            }
        }
    }
}

FxAssetManager &FxAssetManager::GetInstance()
{
    return AssetManager;
}
