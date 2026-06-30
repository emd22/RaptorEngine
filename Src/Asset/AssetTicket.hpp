#pragma once

#include <Core/DataNotifier.hpp>
#include <Core/Defines.hpp>
#include <atomic>

namespace fx {

class AssetTicketData
{
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

    ~AssetTicketData() = default;

public:
    DataNotifier IsFinishedNotifier;
    std::atomic_bool bIsUploadedToGpu = { false };

    std::atomic_bool bIsLoaded = { false };

    std::atomic_int UsageCount = 1;

protected:
    friend class LoaderGltf;
    friend class AxManager;
};

template <typename T>
class AssetTicket
{
public:
    AssetTicket() = default;
    AssetTicket(const AssetTicket& other) { (*this) = other; }

    AssetTicket(AssetTicket&& other) { (*this) = std::move(this); }

    AssetTicket& operator=(const AssetTicket& other)
    {
        mpTicketData = other.mpTicketData;
        mpData = other.mpData;

        if (mpTicketData) {
            mpTicketData->UsageCount.fetch_add(1);
        }

        return *this;
    }

    AssetTicket& operator=(AssetTicket&& other)
    {
        mpTicketData = other.mpTicketData;
        mpData = other.mpData;

        other.DecRef();

        other.mpTicketData = nullptr;
        other.mpData = nullptr;

        return *this;
    }

    /**
     * @brief Returns true if the asset has been loaded and is in GPU memory.
     */
    FX_FORCE_INLINE bool IsLoaded() const
    {
        if (mpTicketData == nullptr) {
            return false;
        }

        return mpTicketData->bIsLoaded.load();
    }


    void WaitUntilLoaded()
    {
        if (mpTicketData == nullptr || mpTicketData->IsFinishedNotifier.IsSignalled()) {
            return;
        }

        mpTicketData->IsFinishedNotifier.Wait(true);
    }

    void MarkAndSignalLoaded() const
    {
        if (mpTicketData == nullptr) {
            return;
        }

        mpTicketData->MarkAndSignalLoaded();
    }

    void DecRef()
    {
        if (!mpTicketData) {
            return;
        }

        if (mpTicketData->UsageCount.fetch_sub(1) <= 1) {
            delete mpTicketData;
            mpTicketData = nullptr;
        }
    }

    ~AssetTicket() { DecRef(); }

protected:
    AssetTicketData* mpTicketData = nullptr;
    T* mpData = nullptr;
};


} // namespace fx
