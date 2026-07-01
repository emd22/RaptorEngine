#pragma once

#include "AssetBase.hpp"

#include <Object/ObjectID.hpp>
#include <functional>

namespace fx {

/**
 * An asset base class for other assets to be derived from.
 *
 * @see Model
 */
class AssetObject : public AssetBase
{
public:
    using OnLoadFunc = std::function<void()>;
    using OnErrorFunc = std::function<void()>;

public:
    void OnLoaded(const OnLoadFunc& on_loaded_callback)
    {
        std::lock_guard guard(mCallbackMutex);

        // If the asset has already been loaded, call the callback immediately.
        if (IsFinishedNotifier.IsSignalled()) {
            on_loaded_callback();
            return;
        }

        mOnLoadedCallbacks.push_back(on_loaded_callback);
    }

    void OnError(const OnErrorFunc& on_error_callback)
    {
        if (IsFinishedNotifier.IsSignalled()) {
            // on_error_callback();
            return;
        }

        {
            std::lock_guard guard(mCallbackMutex);
            mOnErrorCallback = on_error_callback;
        }
    }


protected:
    std::mutex mCallbackMutex;

    std::vector<OnLoadFunc> mOnLoadedCallbacks;
    OnErrorFunc mOnErrorCallback = nullptr;

    friend class AxManager;
};

} // namespace fx
