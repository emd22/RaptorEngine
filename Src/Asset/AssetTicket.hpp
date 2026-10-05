#pragma once

#include <raptor_ffi.h>

#include <Core/Assert.hpp>
#include <Core/Defines.hpp>
#include <functional>
#include <utility>

namespace fx {


/**
 * @brief Functions as a "carrier" for an asset that holds all of the signalling logic to communicate between the asset
 * manager and the code requesting the asset. Copies of a ticket share the same state, which is kept in Rust, much like
 * a shared ptr.
 */
class AssetTicket
{
public:
	using OnLoadFunc = std::function<void(void*)>;
	using OnErrorFunc = std::function<void()>;

public:
	AssetTicket() = delete;
	explicit AssetTicket(void* data)
	{
		mpData = data;
		mpTicket = rx_ticket_new();
	}

	AssetTicket(const AssetTicket& other) { (*this) = other; }
	AssetTicket(AssetTicket&& other) { (*this) = std::move(other); }

	AssetTicket& operator=(const AssetTicket& other)
	{
		if (this == &other) {
			return *this;
		}

		// Take the new reference first to handle both tickets sharing the same state
		if (other.mpTicket) {
			rx_ticket_retain(other.mpTicket);
		}

		if (mpTicket) {
			rx_ticket_release(mpTicket);
		}

		mpTicket = other.mpTicket;
		mpData = other.mpData;

		return *this;
	}

	AssetTicket& operator=(AssetTicket&& other)
	{
		if (this == &other) {
			return *this;
		}

		if (mpTicket) {
			rx_ticket_release(mpTicket);
		}

		mpTicket = other.mpTicket;
		mpData = other.mpData;

		other.mpTicket = nullptr;
		other.mpData = nullptr;

		return *this;
	}

	/**
	 * @brief Returns true if the asset has been loaded and is in GPU memory.
	 */
	FX_FORCE_INLINE bool IsLoaded() const { return mpTicket != nullptr && rx_ticket_is_loaded(mpTicket) != 0; }

	FX_FORCE_INLINE void* Get() { return mpData; }
	FX_FORCE_INLINE const void* Get() const { return mpData; }

	void WaitUntilLoaded()
	{
		Assert(mpTicket != nullptr);

		rx_ticket_wait_finished(mpTicket);
	}

	void WaitUntilUploaded()
	{
		Assert(mpTicket != nullptr);

		rx_ticket_wait_uploaded(mpTicket);
	}

	void MarkAndSignalLoaded() const
	{
		Assert(mpTicket != nullptr);

		rx_ticket_mark_loaded(mpTicket);
	}

	void SignalUploadedToGpu() const
	{
		Assert(mpTicket != nullptr);

		rx_ticket_signal_uploaded(mpTicket);
	}

	void SignalFinished()
	{
		Assert(mpTicket != nullptr);

		rx_ticket_signal_finished(mpTicket);
	}

	/**
	 * @brief Calls `callback` with the asset once it has loaded, or at once if it already has.
	 */
	void OnLoaded(const OnLoadFunc& callback)
	{
		Assert(mpTicket != nullptr);

		rx_ticket_on_loaded(mpTicket, mpData, &CallLoaded, new OnLoadFunc(callback), &DestroyLoaded);
	}

	/**
	 * @brief Calls `callback` if the asset fails to load. A ticket holds one such callback.
	 */
	void OnError(const OnErrorFunc& callback)
	{
		Assert(mpTicket != nullptr);

		rx_ticket_on_error(mpTicket, &CallError, new OnErrorFunc(callback), &DestroyError);
	}

	/**
	 * @brief The asset is loaded and uploaded. Calls what was waiting on it with `asset` if `run_callbacks` is set,
	 * then marks the ticket loaded.
	 */
	void Complete(void* asset, bool run_callbacks)
	{
		Assert(mpTicket != nullptr);

		rx_ticket_complete(mpTicket, asset, run_callbacks ? 1 : 0);
	}

	/**
	 * @brief The asset could not be loaded. Finishes the ticket, and calls the error callback if `run_error_callback`
	 * is set.
	 */
	void Fail(bool run_error_callback)
	{
		Assert(mpTicket != nullptr);

		rx_ticket_fail(mpTicket, run_error_callback ? 1 : 0);
	}

	void DecRef()
	{
		if (mpTicket) {
			rx_ticket_release(mpTicket);
		}

		mpTicket = nullptr;
		mpData = nullptr;
	}

	FX_FORCE_INLINE bool IsValid() const { return mpData != nullptr && mpTicket != nullptr; }
	FX_FORCE_INLINE bool IsInvalid() const { return mpData == nullptr || mpTicket == nullptr; }

	~AssetTicket() { DecRef(); }

private:
	static void CallLoaded(void* user, void* asset) { (*static_cast<OnLoadFunc*>(user))(asset); }
	static void DestroyLoaded(void* user) { delete static_cast<OnLoadFunc*>(user); }

	static void CallError(void* user, void*) { (*static_cast<OnErrorFunc*>(user))(); }
	static void DestroyError(void* user) { delete static_cast<OnErrorFunc*>(user); }

private:
	const RxTicket* mpTicket = nullptr;
	void* mpData = nullptr;
};


} // namespace fx
