#pragma once

#include "FxAssetQueueItem.hpp"

#include "FxModel.hpp"
#include "Asset/FxAssetQueue.hpp"



#include <thread>


class FxAssetManager
{
public:
    void Start(int32 thread_count);
    void ShutDown();
    void ThreadUpdate();

    static FxAssetManager &GetInstance();
    static FxModel *LoadModel(std::string path);
    static void LoadToModel(FxModel *model, std::string path);

    ~FxAssetManager()
    {
        ShutDown();
    }

private:
    void NotifyAssetOnLoaded(FxAssetQueueItem &item);

private:
    FxAssetQueue mLoadQueue;

    std::atomic_bool mActive = false;

    int32 mThreadCount = 2;
    StaticArray<std::thread *> mThreads;
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
