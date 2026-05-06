#pragma once

#include <Core/DataNotifier.hpp>
#include <Core/PagedArray.hpp>
#include <Core/TSRef.hpp>
#include <atomic>
#include <functional>
#include <mutex>

namespace fx {


/**
 * An asset base class for other assets to be derived from.
 *
 * @see Model
 */
class AxBase
{
protected:
    AxBase() {}


    // template <typename T>
    // friend class Ref;

public:
    // using OnLoadFunc = void (*)(Ref<BaseAsset> asset);
    using OnLoadFunc = std::function<void(TSRef<AxBase>)>;
    using OnErrorFunc = void (*)(TSRef<AxBase> asset);

    virtual void WaitUntilLoaded()
    {
        if (IsFinishedNotifier.IsDone()) {
            return;
        }

        IsFinishedNotifier.WaitForData(true);
    }

    /** Returns true if the asset has been loaded and is in GPU memory. */
    inline bool IsLoaded() const { return mIsLoaded.load(); }

    virtual void Destroy() = 0;

    virtual ~AxBase() {}

    /**
     * Attaches a new callback for when the Asset is finished being loaded by the AssetManager.
     */
    // void OnLoaded(CallbackFunc &&on_loaded) { AttachCallback(AssetCallbackType::OnLoaded, std::move(on_loaded));
    // }


    void OnLoaded(const OnLoadFunc& on_loaded_callback)
    {
        std::lock_guard guard(mCallbackMutex);
        // If the asset has already been loaded, call the callback immediately.
        if (IsFinishedNotifier.IsDone()) {
            LogInfo("CALLING EARLY");
            on_loaded_callback(this);
            return;
        }

        mOnLoadedCallbacks.push_back(on_loaded_callback);
    }

    void OnError(const OnErrorFunc& on_error_callback)
    {
        if (IsFinishedNotifier.IsDone()) {
            // on_error_callback(this);
            return;
        }

        {
            std::lock_guard guard(mCallbackMutex);
            mOnErrorCallback = on_error_callback;
        }
    }

public:
    DataNotifier IsFinishedNotifier;
    std::atomic_bool bIsUploadedToGpu = false;

protected:
    friend class AxLoaderGltf;

    std::vector<OnLoadFunc> mOnLoadedCallbacks;

    std::mutex mCallbackMutex;

    // OnLoadFunc mOnLoadedCallback = nullptr;
    OnErrorFunc mOnErrorCallback = nullptr;
    std::atomic_bool mIsLoaded = false;

    friend class AxManager;
};

} // namespace fx
