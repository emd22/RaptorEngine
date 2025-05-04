#pragma once

#include <atomic>

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

    virtual void Destroy() = 0;

    virtual ~FxBaseAsset()
    {
    }

public:

    using OnLoadFunc = void (*)(FxBaseAsset *asset);
    using OnErrorFunc = void (*)(FxBaseAsset *asset);

    /**
     * Attaches a new callback for when the Asset is finished being loaded by the AssetManager.
     */
    // void OnLoaded(CallbackFunc &&on_loaded) { AttachCallback(FxAssetCallbackType::OnLoaded, std::move(on_loaded)); }

    OnLoadFunc OnLoaded = nullptr;
    OnErrorFunc OnError = nullptr;

    std::atomic_bool IsLoaded = false;
// private:
};
