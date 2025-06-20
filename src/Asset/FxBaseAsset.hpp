#pragma once

#include <atomic>

#include <Core/FxDataNotifier.hpp>

#include <Core/FxRef.hpp>

/**
 * An asset base class for other assets to be derived from.
 *
 * @see FxModel
 */
class FxBaseAsset
{
protected:
    FxBaseAsset()
    {
    }


    // template <typename T>
    // friend class FxRef;

public:

    using OnLoadFunc = void (*)(const FxRef<FxBaseAsset> asset);
    using OnErrorFunc = void (*)(const FxRef<FxBaseAsset> asset);

    virtual void WaitUntilLoaded()
    {
        if (IsFinishedNotifier.IsDone()) {
            return;
        }

        IsFinishedNotifier.WaitForData(true);
    }


    virtual void Destroy() = 0;

    virtual ~FxBaseAsset()
    {
    }

    /**
     * Attaches a new callback for when the Asset is finished being loaded by the AssetManager.
     */
    // void OnLoaded(CallbackFunc &&on_loaded) { AttachCallback(FxAssetCallbackType::OnLoaded, std::move(on_loaded)); }



    void OnLoaded(const OnLoadFunc& on_loaded_callback)
    {
        // If the asset has already been loaded, call the callback immediately.
        if (IsFinishedNotifier.IsDone()) {
            // on_loaded_callback(this);

            return;
        }
        printf("Registered onload callback\n");
        mOnLoadedCallback = on_loaded_callback;
    }

    void OnError(const OnErrorFunc& on_error_callback)
    {
        if (IsFinishedNotifier.IsDone()) {
            // on_error_callback(this);
            return;
        }

        mOnErrorCallback = on_error_callback;
    }

public:
    FxDataNotifier IsFinishedNotifier;
    std::atomic_bool IsUploadedToGpu = false;

protected:
    OnLoadFunc mOnLoadedCallback = nullptr;
    OnErrorFunc mOnErrorCallback = nullptr;

    friend class FxAssetManager;
};
