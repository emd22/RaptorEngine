#pragma once

#include <Core/DataNotifier.hpp>
#include <Core/Defines.hpp>
#include <atomic>
#include <functional>

namespace fx {

class AssetTicketData
{
public:
    using OnLoadFunc = std::function<void()>;
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
    friend class AxManager;
};

template <typename T>
class AssetTicket
{
public:
    AssetTicket() = delete;
    AssetTicket(T* data)
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

        other.DecRef();

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
        if (pTicketData == nullptr || pTicketData->IsFinishedNotifier.IsSignalled()) {
            return;
        }

        pTicketData->IsFinishedNotifier.Wait(true);
    }

    void MarkAndSignalLoaded() const
    {
        if (pTicketData == nullptr) {
            return;
        }

        pTicketData->MarkAndSignalLoaded();
    }

    void SignalUploadedToGpu() const
    {
        if (pTicketData == nullptr) {
            return;
        }

        pTicketData->SignalUploadedToGpu();
    }


    void OnLoaded(const AssetTicketData::OnLoadFunc& on_loaded_callback)
    {
        if (pTicketData == nullptr) {
            return;
        }

        pTicketData->OnLoaded(on_loaded_callback);
    }

    void DecRef()
    {
        if (!pTicketData) {
            return;
        }

        if (pTicketData->UsageCount.fetch_sub(1) <= 1) {
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
