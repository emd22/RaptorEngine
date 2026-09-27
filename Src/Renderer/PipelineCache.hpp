#pragma once

#include "Backend/Pipeline.hpp"
#include "PipelineDesc.hpp"
#include "PipelineKey.hpp"
#include "PipelineNames.hpp"
#include "PipelineVariant.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fx::renderer {


class PipelineCache
{
public:
	PipelineCache();
	~PipelineCache();

	Pipeline& Request(const ePipelineName name);
	ePipelineName GetName(const Pipeline* pipeline) const;
	void Bind(const ePipelineName name, const CommandBuffer& cmd);

	/////////////////////////////////////
	// Handles
	/////////////////////////////////////

	FX_FORCE_INLINE static PipelineHandle GetHandle(const ePipelineName name)
	{
		return PipelineHandle { static_cast<uint32>(name) };
	}

	/**
	 * @brief Returns the pipeline behind a handle.
	 * @note This does not lock, so it is fine to call per draw.
	 */
	Pipeline& Get(const PipelineHandle handle);
	void Bind(const PipelineHandle handle, const CommandBuffer& cmd);

	const char* GetDebugName(const PipelineHandle handle) const;

	/////////////////////////////////////
	// Creating pipelines
	/////////////////////////////////////

	/**
	 * @brief Makes a new pipeline from a description. This does not look for an equal pipeline that already exists, ask
	 * for it through GetOrCreateVariant() for that.
	 *
	 * @returns an invalid handle if there is no room for another pipeline, or (on the main thread) it could not be
	 * built
	 */
	PipelineHandle Create(const PipelineDesc& desc);

	/**
	 * @brief Builds the pipelines that were asked for on other threads.
	 * @note Call only on the main/game thread
	 */
	void BuildPending();

	bool IsMainThread() const { return std::this_thread::get_id() == mMainThread; }

	/////////////////////////////////////
	// Keys
	/////////////////////////////////////

	/**
	 * @brief Records the key a pipeline was built with
	 * @returns False if another pipeline already has an identical key
	 */
	bool RegisterKey(const PipelineHandle handle, const PipelineKey& key);

	PipelineHandle Find(const PipelineKey& key) const;

	/**
	 * @brief The key a pipeline was registered with, if it has one
	 */
	std::optional<PipelineKey> GetKey(const PipelineHandle handle) const;

	/////////////////////////////////////
	// Variants
	/////////////////////////////////////

	void RegisterVariant(const ePipelinePass pass, const ePipelineFeatures features, const PipelineHandle handle);
	PipelineHandle FindVariant(const ePipelinePass pass, const ePipelineFeatures features) const;
	PipelineHandle FindVariantInPass(const PipelineHandle handle, const ePipelinePass pass) const;

	template <typename TFunc>
	void ForEachPassPipeline(const ePipelinePass pass, TFunc&& fn) const
	{
		const std::vector<PipelineHandle>& pipelines = mPassPipelines[static_cast<uint32>(pass)];

		for (size_t i = 0;; i++) {
			PipelineHandle handle;

			{
				// Not held while `fn` runs, as it can make pipelines and add to the list
				std::lock_guard lock(mMutex);

				if (i >= pipelines.size()) {
					break;
				}

				handle = pipelines[i];
			}

			fn(handle);
		}
	}


	PipelineHandle GetOrCreateVariant(const ePipelinePass pass, const ePipelineFeatures features);
	PipelineHandle GetOrCreateVariantInPass(const PipelineHandle handle, const ePipelinePass pass);

	using PassTemplate = std::function<bool(ePipelineFeatures features, PipelineDesc& out_desc)>;

	void RegisterPassTemplate(const ePipelinePass pass, PassTemplate pass_template);
	void AddBufferOffset(uint32 set_index, uint32 offset);

private:
	void Reset();

	struct KeyEntry
	{
		PipelineKey Key;
		Hash64 Hash = 0;
		bool bRegistered = false;
	};

	struct VariantInfo
	{
		ePipelinePass Pass = ePipelinePass::Count;
		ePipelineFeatures Features = ePipelineFeatures::None;
	};

	struct DynamicPipeline
	{
		Pipeline Pipe;
		PipelineDesc Desc;
		std::string DebugName;
	};

	static constexpr uint32 scMaxDynamicPipelines = 256;

private:
	// These need the lock to be called

	PipelineHandle CreateLocked(const PipelineDesc& desc);
	void RegisterVariantLocked(const ePipelinePass pass, const ePipelineFeatures features, const PipelineHandle handle);

private:
	SizedArray<Pipeline> mCache;
	SizedArray<SizedArray<uint32>> mOffsets;

	/// The pipelines made from descriptions, after the ones in mCache. Filled in order, and each is stored once it is
	/// set up so a reader that finds it sees all of it.
	std::atomic<DynamicPipeline*> mDynamic[scMaxDynamicPipelines];
	std::atomic<uint32> mDynamicCount { 0 };

	/// The pipeline (as a handle index) for each pass and set of features, or PipelineHandle::scInvalidIndex
	std::atomic<uint32> mVariants[scNumPipelinePasses][scNumFeatureCombinations];

	/////////////////////////////////////
	// Mutex guarded below:
	/////////////////////////////////////

	mutable std::mutex mMutex;

	/// Indexed by handle
	std::vector<KeyEntry> mKeys;
	std::vector<VariantInfo> mVariantInfos;

	std::unordered_map<Hash64, PipelineHandle, Hash64Stl> mKeyLookup;

	std::vector<PipelineHandle> mPassPipelines[scNumPipelinePasses];
	PassTemplate mPassTemplates[scNumPipelinePasses];

	/// Dynamic pipelines waiting for the main thread to build them
	std::vector<uint32> mPending;
	std::atomic<bool> mbHasPending { false };

	std::thread::id mMainThread;

	/// Set while BuildPending() is in progress
	bool mbBuilding = false;
};


} // namespace fx::renderer
