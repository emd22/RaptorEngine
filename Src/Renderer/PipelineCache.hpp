#pragma once

#include "Backend/Pipeline.hpp"
#include "PipelineKey.hpp"
#include "PipelineNames.hpp"
#include "PipelineVariant.hpp"

#include <unordered_map>
#include <vector>

namespace fx::renderer {

class PipelineCache
{
public:
	PipelineCache();

	Pipeline& Request(const ePipelineName name);
	ePipelineName GetName(const Pipeline* pipeline) const;
	void Bind(const ePipelineName name, const CommandBuffer& cmd);

	/////////////////////////////////////
	// Handles
	/////////////////////////////////////

	/**
	 * @brief The handle of a pipeline that has an ePipelineName. These are the first handles, so it is the enum value.
	 */
	FX_FORCE_INLINE static PipelineHandle GetHandle(const ePipelineName name)
	{
		return PipelineHandle { static_cast<uint32>(name) };
	}

	/**
	 * @brief Returns the pipeline behind a handle. This is an array access, so it is fine to call per draw.
	 */
	Pipeline& Get(const PipelineHandle handle);
	void Bind(const PipelineHandle handle, const CommandBuffer& cmd);

	/////////////////////////////////////
	// Keys
	/////////////////////////////////////

	/**
	 * @brief Records the key a pipeline was built with, so that it can be found by it later. Registering a handle again
	 * (the pipeline was rebuilt) replaces its key.
	 *
	 * Two pipelines with an identical key are the same pipeline. The first one registered keeps the key, and the second
	 * is reported.
	 *
	 * @returns False if another pipeline already has an identical key
	 */
	bool RegisterKey(const PipelineHandle handle, const PipelineKey& key);

	/**
	 * @brief Finds the pipeline built from a key. This hashes the key, so do it when a material builds, not per draw.
	 * @returns an invalid handle if there is no such pipeline
	 */
	PipelineHandle Find(const PipelineKey& key) const;

	/**
	 * @brief The key a pipeline was registered with, or nullptr if it has none
	 */
	const PipelineKey* GetKey(const PipelineHandle handle) const;

	/////////////////////////////////////
	// Variants
	/////////////////////////////////////

	/**
	 * @brief Makes a pipeline the one that draws `features` in `pass`. Several sets of features can share a pipeline, for
	 * example when a skinned pipeline also handles normal maps.
	 */
	void RegisterVariant(const ePipelinePass pass, const ePipelineFeatures features, const PipelineHandle handle);

	/**
	 * @brief Finds the pipeline that draws `features` in `pass`. This is a table lookup, so it is fine to call per draw.
	 * @returns an invalid handle if the pass has no pipeline for the features
	 */
	PipelineHandle FindVariant(const ePipelinePass pass, const ePipelineFeatures features) const;

	/**
	 * @brief Finds the pipeline that draws the same features as `handle` in another pass, for example the prepass
	 * pipeline that goes with a forward one.
	 * @returns an invalid handle if `handle` is not a variant, or the other pass has no such pipeline
	 */
	PipelineHandle FindVariantInPass(const PipelineHandle handle, const ePipelinePass pass) const;

	/**
	 * @brief Every pipeline that draws in a pass, each once, in the order they were registered
	 */
	const std::vector<PipelineHandle>& GetPassPipelines(const ePipelinePass pass) const;

	void AddBufferOffset(uint32 set_index, uint32 offset);

private:
	void Reset();

private:
	struct KeyEntry
	{
		PipelineKey Key;
		Hash64 Hash = 0;
		bool bRegistered = false;
	};

private:
	SizedArray<Pipeline> mCache;
	SizedArray<SizedArray<uint32>> mOffsets;

	struct VariantInfo
	{
		ePipelinePass Pass = ePipelinePass::Count;
		ePipelineFeatures Features = ePipelineFeatures::None;
	};

private:
	/// Indexed by handle
	std::vector<KeyEntry> mKeys;

	/// The pipeline for each pass and set of features
	PipelineHandle mVariants[scNumPipelinePasses][scNumFeatureCombinations];
	std::vector<PipelineHandle> mPassPipelines[scNumPipelinePasses];

	/// What each handle was first registered as, indexed by handle
	std::vector<VariantInfo> mVariantInfos;
	std::unordered_map<Hash64, PipelineHandle, Hash64Stl> mKeyLookup;
};


} // namespace fx::renderer
