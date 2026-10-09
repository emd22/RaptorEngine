#pragma once

#include <Core/Assert.hpp>
#include <Core/DataNotifier.hpp>
#include <Core/Defines.hpp>
#include <Core/Types.hpp>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace fx {


class AssetCallbackQueue
{
public:
	using Task = std::function<void()>;

public:
	static AssetCallbackQueue& Get()
	{
		static AssetCallbackQueue sInstance;
		return sInstance;
	}

	void BindToCurrentThread()
	{
		std::lock_guard guard(mMutex);

		mOwner = std::this_thread::get_id();
		mbBound = true;
	}

	void Unbind()
	{
		std::vector<Task> discarded;

		{
			std::lock_guard guard(mMutex);

			mbBound = false;
			discarded.swap(mPending);
		}
	}

	void Run(Task task)
	{
		{
			std::lock_guard guard(mMutex);

			const bool on_owner_thread = (mOwner == std::this_thread::get_id());
			const bool run_inline = !mbBound || (on_owner_thread && !mbDispatching && mPending.empty());

			if (!run_inline) {
				mPending.push_back(std::move(task));
				return;
			}
		}

		task();
	}

	uint32 Dispatch()
	{
		std::vector<Task> batch;
		bool on_owner_thread = true;

		{
			std::lock_guard guard(mMutex);

			if (!mbBound) {
				return 0;
			}

			on_owner_thread = (mOwner == std::this_thread::get_id());

			if (on_owner_thread) {
				batch.swap(mPending);
				mbDispatching = true;
			}
		}

		AssertMsg(on_owner_thread, "Asset callbacks must be dispatched on the thread that started the asset manager");

		struct DispatchScope
		{
			~DispatchScope()
			{
				std::lock_guard guard(Queue.mMutex);
				Queue.mbDispatching = false;
			}

			AssetCallbackQueue& Queue;
		} scope { *this };

		for (Task& task : batch) {
			task();
		}

		return static_cast<uint32>(batch.size());
	}

private:
	AssetCallbackQueue() = default;

private:
	std::mutex mMutex;
	std::vector<Task> mPending;
	std::thread::id mOwner;
	bool mbBound = false;
	bool mbDispatching = false;
};


/**
 * @brief The internal representation of an asset ticket. This is shared between all instances of an `AssetTicket`, much
 * like a shared ptr.
 */
class AssetTicketData
{
public:
	using OnLoadFunc = std::function<void(void*)>;
	using OnErrorFunc = std::function<void()>;

	enum class eState : uint8
	{
		Pending,
		Loaded,
		Failed,
	};

public:
	AssetTicketData() = default;
	AssetTicketData(const AssetTicketData&) = delete;

	AssetTicketData& operator=(const AssetTicketData&) = delete;

	void MarkAndSignalLoaded(void* item)
	{
		SignalUploadedToGpu();
		CompleteLoaded(item);
	}

	void SignalUploadedToGpu()
	{
		bIsUploadedToGpu.store(true);
		bIsUploadedToGpu.notify_all();
	}

	void CompleteLoaded(void* item, OnLoadFunc before = nullptr, OnLoadFunc after = nullptr)
	{
		std::vector<OnLoadFunc> callbacks;

		{
			std::lock_guard guard(mCallbackMutex);

			if (mState.load() != eState::Pending) {
				return;
			}

			mState.store(eState::Loaded);
			bIsLoaded.store(true);

			callbacks = std::move(mOnLoadedCallbacks);
			mOnLoadedCallbacks.clear();
			mOnErrorCallback = nullptr;
		}

		IsFinishedNotifier.Signal();

		if (callbacks.empty() && !before && !after) {
			return;
		}

		AssetCallbackQueue::Get().Run(
			[before = std::move(before), callbacks = std::move(callbacks), after = std::move(after), item]()
			{
				if (before) {
					before(item);
				}

				for (const OnLoadFunc& callback : callbacks) {
					callback(item);
				}

				if (after) {
					after(item);
				}
			});
	}

	void CompleteFailed()
	{
		OnErrorFunc on_error;

		{
			std::lock_guard guard(mCallbackMutex);

			if (mState.load() != eState::Pending) {
				return;
			}

			mState.store(eState::Failed);

			mOnLoadedCallbacks.clear();
			on_error = std::move(mOnErrorCallback);
			mOnErrorCallback = nullptr;
		}

		IsFinishedNotifier.Signal();

		if (on_error) {
			AssetCallbackQueue::Get().Run([on_error = std::move(on_error)]() { on_error(); });
		}
	}

	void OnLoaded(void* item, const OnLoadFunc& on_loaded_callback)
	{
		{
			std::lock_guard guard(mCallbackMutex);

			switch (mState.load()) {
			case eState::Pending:
				mOnLoadedCallbacks.push_back(on_loaded_callback);
				return;
			case eState::Failed:
				return;
			case eState::Loaded:
				break;
			}
		}

		AssetCallbackQueue::Get().Run([on_loaded_callback, item]() { on_loaded_callback(item); });
	}


	void OnError(const OnErrorFunc& on_error_callback)
	{
		{
			std::lock_guard guard(mCallbackMutex);

			switch (mState.load()) {
			case eState::Pending:
				mOnErrorCallback = on_error_callback;
				return;
			case eState::Loaded:
				return;
			case eState::Failed:
				break;
			}
		}

		AssetCallbackQueue::Get().Run([on_error_callback]() { on_error_callback(); });
	}

	bool IsFailed() const { return mState.load() == eState::Failed; }


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
	std::atomic<eState> mState = { eState::Pending };

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

	FX_FORCE_INLINE bool IsFailed() const
	{
		if (pTicketData == nullptr) {
			return false;
		}

		return pTicketData->IsFailed();
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

		pTicketData->MarkAndSignalLoaded(mpData);
	}

	void SignalUploadedToGpu() const
	{
		Assert(pTicketData != nullptr);

		pTicketData->SignalUploadedToGpu();
	}

	void SignalFailed()
	{
		Assert(pTicketData != nullptr);

		pTicketData->CompleteFailed();
	}


	void OnLoaded(const AssetTicketData::OnLoadFunc& on_loaded_callback)
	{
		Assert(pTicketData != nullptr);

		pTicketData->OnLoaded(reinterpret_cast<void*>(mpData), on_loaded_callback);
	}

	void OnError(const AssetTicketData::OnErrorFunc& on_error_callback)
	{
		Assert(pTicketData != nullptr);

		pTicketData->OnError(on_error_callback);
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
