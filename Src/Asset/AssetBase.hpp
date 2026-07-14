#pragma once

#include <Core/DataNotifier.hpp>
#include <Core/TSRef.hpp>
#include <atomic>

namespace fx {


/**
 * An asset base class for other assets to be derived from.
 *
 * @see Model
 */
class AssetBase
{
protected:
	AssetBase() {}

public:
	virtual void WaitUntilLoaded()
	{
		if (IsFinishedNotifier.IsSignalled()) {
			return;
		}

		IsFinishedNotifier.Wait(true);
	}

	/** Returns true if the asset has been loaded and is in GPU memory. */
	FX_FORCE_INLINE bool IsLoaded() const { return mIsLoaded.load(); }
	FX_FORCE_INLINE void InvalidateLoaded() { mIsLoaded.store(false); }

	virtual void Destroy() = 0;

	virtual ~AssetBase() {}

public:
	DataNotifier IsFinishedNotifier;
	std::atomic_bool bIsUploadedToGpu = { false };

protected:
	std::atomic_bool mIsLoaded = { false };

	friend class LoaderGltf;
	friend class AssetManager;
};

} // namespace fx
