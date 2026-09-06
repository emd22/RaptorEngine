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

		bIsUploadedToGpu.store(true);
		bIsUploadedToGpu.notify_all();

		bIsLoaded.store(true);
		IsFinishedNotifier.Signal();
	}

	void SignalFinished() { IsFinishedNotifier.Signal(); }

	void SignalUploadedToGpu()
	{
		bIsUploadedToGpu.store(true);
		bIsUploadedToGpu.notify_all();
	}

	void OnLoaded(void* item, const OnLoadFunc& on_loaded_callback)
	{
		{
			std::lock_guard guard(mCallbackMutex);
			if (IsFinishedNotifier.IsSignalled()) {
				// Unlock before invoking callback to avoid deadlock if callback re-enters OnLoaded
			} else {
				mOnLoadedCallbacks.push_back(on_loaded_callback);
				return;
			}
		}
		on_loaded_callback(item);
	}


	void OnError(const OnErrorFunc& on_error_callback)
	{
		OnErrorFunc to_call = nullptr;
		{
			std::lock_guard guard(mCallbackMutex);
			if (IsFinishedNotifier.IsSignalled()) {
				to_call = on_error_callback;
			} else {
				mOnErrorCallback = on_error_callback;
				return;
			}
		}
		if (to_call) {
			to_call();
		}
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
class AssetTicket
{
public:
	AssetTicket() = delete;
	explicit AssetTicket(void* data)
	{
		mpData = data;
		pTicketData = new AssetTicketData;
	}

	AssetTicket(const AssetTicket& other) { (*this) = other; }
	AssetTicket(AssetTicket&& other) { (*this) = std::move(other); }

	AssetTicket& operator=(const AssetTicket& other)
	{
		if (this == &other) {
			return *this;
		}
		// Increment new first to handle self-alias of pTicketData
		if (other.pTicketData) {
			other.pTicketData->UsageCount.fetch_add(1);
		}
		if (pTicketData) {
			// Release old
			if (pTicketData->UsageCount.fetch_sub(1) <= 1) {
				delete pTicketData;
			}
		}
		pTicketData = other.pTicketData;
		mpData = other.mpData;

		return *this;
	}

	AssetTicket& operator=(AssetTicket&& other)
	{
		if (this == &other) {
			return *this;
		}
		if (pTicketData) {
			if (pTicketData->UsageCount.fetch_sub(1) <= 1) {
				delete pTicketData;
			}
		}
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

	FX_FORCE_INLINE void* Get() { return mpData; }
	FX_FORCE_INLINE const void* Get() const { return mpData; }

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

	void SignalFinished()
	{
		Assert(pTicketData != nullptr);

		pTicketData->SignalFinished();
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
			delete pTicketData;
			pTicketData = nullptr;
		} else {
			// Still shared, clear our pointer without freeing
			pTicketData = nullptr;
			mpData = nullptr;
		}
	}

	FX_FORCE_INLINE bool IsValid() const { return mpData != nullptr && pTicketData != nullptr; }
	FX_FORCE_INLINE bool IsInvalid() const { return mpData == nullptr || pTicketData == nullptr; }

	~AssetTicket() { DecRef(); }

public:
	AssetTicketData* pTicketData = nullptr;

protected:
	void* mpData = nullptr;
};


} // namespace fx
