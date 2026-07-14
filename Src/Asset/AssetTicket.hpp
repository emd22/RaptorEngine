#pragma once

#include <Core/Assert.hpp>
#include <Core/DataNotifier.hpp>
#include <Core/Defines.hpp>
#include <atomic>
#include <functional>

namespace fx {


/**
 * @brief The internal representation of an asset ticket. This is shared between all instances of an `AssetTicket`, much
 * like a shared ptr.
 */
class AssetTicketData
{
public:
    using OnLoadFunc = std::function<void(void*)>;
    using OnErrorFunc = std::function<void()>;

public:
    AssetTicketData() = default;
    AssetTicketData(const AssetTicketData&) = delete;

    AssetTicketData& operator=(const AssetTicketData&) = delete;

    void MarkAndSignalLoaded()
    {
        if (bIsLoaded.load()) {
            return;
        }

        IsFinishedNotifier.Signal();

        bIsUploadedToGpu = true;
        bIsUploadedToGpu.notify_all();

        bIsLoaded.store(true);
    }

    void SignalUploadedToGpu()
    {
        bIsUploadedToGpu = true;
        bIsUploadedToGpu.notify_all();
    }

    void OnLoaded(void* item, const OnLoadFunc& on_loaded_callback)
    {
        std::lock_guard guard(mCallbackMutex);

        // If the asset has already been loaded, call the callback immediately.
        if (IsFinishedNotifier.IsSignalled()) {
            on_loaded_callback(item);
            return;
        }

        mOnLoadedCallbacks.push_back(on_loaded_callback);
    }


    void OnError(const OnErrorFunc& on_error_callback)
    {
        std::lock_guard guard(mCallbackMutex);

        // If the asset has already been loaded, call the callback immediately.
        if (IsFinishedNotifier.IsSignalled()) {
            on_error_callback();
            return;
        }

        mOnErrorCallback = on_error_callback;
    }


    ~AssetTicketData() = default;

public:
    DataNotifier IsFinishedNotifier;
    std::atomic_bool bIsUploadedToGpu = { false };
    std::atomic_bool bIsLoaded = { false };
    std::atomic_int UsageCount = 1;

    // Callback members
    std::mutex mCallbackMutex;
    std::vector<OnLoadFunc> mOnLoadedCallbacks;
    OnErrorFunc mOnErrorCallback = nullptr;

protected:
    friend class LoaderGltf;
    friend class AssetManager;
};


/**
 * @brief Functions as a "carrier" for an asset that holds all of the signalling logic to communicate between the asset
 * manager and the code requesting the asset.
 */
template <typename T>
class AssetTicket
{
public:
    AssetTicket() = delete;
    explicit AssetTicket(T* data)
    {
        mpData = data;
        pTicketData = new AssetTicketData;
    }

    AssetTicket(const AssetTicket& other) { (*this) = other; }
    AssetTicket(AssetTicket&& other) { (*this) = std::move(other); }

    AssetTicket& operator=(const AssetTicket& other)
    {
        pTicketData = other.pTicketData;
        mpData = other.mpData;

        if (pTicketData) {
            pTicketData->UsageCount.fetch_add(1);
        }

        return *this;
    }

    AssetTicket& operator=(AssetTicket&& other)
    {
        pTicketData = other.pTicketData;
        mpData = other.mpData;

        other.pTicketData = nullptr;
        other.mpData = nullptr;

        return *this;
    }

    /**
     * @brief Returns true if the asset has been loaded and is in GPU memory.
     */
    FX_FORCE_INLINE bool IsLoaded() const
    {
        if (pTicketData == nullptr) {
            return false;
        }

        return pTicketData->bIsLoaded.load();
    }

    FX_FORCE_INLINE T& operator*() { return *mpData; }
    FX_FORCE_INLINE T* operator->() { return mpData; }

    FX_FORCE_INLINE T* Get() { return mpData; }

    void WaitUntilLoaded()
    {
        Assert(pTicketData != nullptr);

        pTicketData->IsFinishedNotifier.Wait(true);
    }

    void MarkAndSignalLoaded() const
    {
        Assert(pTicketData != nullptr);

        pTicketData->MarkAndSignalLoaded();
    }

    void SignalUploadedToGpu() const
    {
        Assert(pTicketData != nullptr);

        pTicketData->SignalUploadedToGpu();
    }


    void OnLoaded(const AssetTicketData::OnLoadFunc& on_loaded_callback)
    {
        Assert(pTicketData != nullptr);

        pTicketData->OnLoaded(reinterpret_cast<void*>(mpData), on_loaded_callback);
    }

    void DecRef()
    {
        if (!pTicketData) {
            return;
        }

        if (pTicketData->UsageCount.fetch_sub(1) <= 1) {
            free(pTicketData);
            pTicketData = nullptr;
        }
    }

    ~AssetTicket() { DecRef(); }

public:
    AssetTicketData* pTicketData = nullptr;

protected:
    T* mpData = nullptr;
};


} // namespace fx
