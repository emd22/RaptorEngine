#pragma once

#include "Backend/Pipeline.hpp"
#include "PipelineDesc.hpp"
#include "PipelineKey.hpp"
#include "PipelineNames.hpp"
#include "PipelineVariant.hpp"

#include <functional>
#include <raptor_ffi.h>

namespace fx::renderer {

/**
 * @brief The pipelines. They are made, owned and looked up by the cache in Rust, this gives out references to them and
 * is where the C++ renderer supplies what builds them.
 *
 * The fixed pipelines (ePipelineName) are built through PSOBuild. The ones that draw objects and materials are made from
 * the descriptions that the pass templates give when one is asked for with GetOrCreateVariant().
 */
class PipelineCache
{
public:
	PipelineCache();
	~PipelineCache();

	PipelineCache(const PipelineCache&) = delete;
	PipelineCache& operator=(const PipelineCache&) = delete;

	Pipeline& Request(const ePipelineName name);
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

	/**
	 * @brief Builds the pipelines that were asked for on other threads.
	 * @note Call only on the main/game thread
	 */
	void BuildPending();

	/**
	 * @brief Builds one of the fixed pipelines from a description, which is taken
	 */
	void BuildStatic(const PipelineHandle handle, RxPipelineDesc* desc);

	/////////////////////////////////////
	// Variants
	/////////////////////////////////////

	PipelineHandle FindVariant(const ePipelinePass pass, const ePipelineFeatures features) const;
	PipelineHandle FindVariantInPass(const PipelineHandle handle, const ePipelinePass pass) const;

	template <typename TFunc>
	void ForEachPassPipeline(const ePipelinePass pass, TFunc&& fn) const
	{
		for (size_t i = 0;; i++) {
			uint32 handle = RX_INVALID_INDEX;

			if (rx_pipeline_cache_pass_pipeline(mpCache, static_cast<uint32>(pass), i, &handle) == 0) {
				break;
			}

			fn(PipelineHandle { handle });
		}
	}

	PipelineHandle GetOrCreateVariant(const ePipelinePass pass, const ePipelineFeatures features);
	PipelineHandle GetOrCreateVariantInPass(const PipelineHandle handle, const ePipelinePass pass);

	/**
	 * @brief Fills in the description of the pipeline that draws `features` in a pass, or returns false if the pass has
	 * none. It is called with the cache locked, and on whichever thread asked for the pipeline.
	 */
	using PassTemplate = std::function<bool(ePipelineFeatures features, PipelineDesc& out_desc)>;

	void RegisterPassTemplate(const ePipelinePass pass, PassTemplate pass_template);
	void AddBufferOffset(uint32 set_index, uint32 offset);

	RxPipelineCache* GetRust() const { return mpCache; }

private:
	RxPipelineContext MakeContext() const;

	/// Reports a pipeline that could not be built
	static void CheckStatus(int32 status, int32 vk_result);

private:
	RxPipelineCache* mpCache = nullptr;

	RxLogSink mLog {};
};


} // namespace fx::renderer
