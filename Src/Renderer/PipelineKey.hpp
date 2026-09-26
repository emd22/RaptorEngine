#pragma once

#include "ShaderNames.hpp"
#include "Vertex.hpp"

#include <vulkan/vulkan.h>

#include <Core/Hash.hpp>
#include <Core/Types.hpp>

namespace fx::renderer {

/**
 * @brief The index of the pipeline into the pipeline cache. Smaller than something like ObjectID, as it is intended to
 * be just the index and no other identifying information.
 */
struct PipelineHandle
{
	static constexpr uint32 scInvalidIndex = UINT32_MAX;

public:
	FX_FORCE_INLINE bool IsValid() const { return Index != scInvalidIndex; }
	FX_FORCE_INLINE bool operator==(const PipelineHandle& other) const { return Index == other.Index; }

public:
	uint32 Index = scInvalidIndex;
};

/**
 * @brief The key used to identify a pipeline. This is used to get the PipelineHandle that points directly into the
 * pipeline cache.
 */
struct PipelineKey
{
	/// Shader and the hash of the macros it was compiled with
	eShaderName Shader = eShaderName::NumShaders;
	Hash64 MacroHash = 0;

	eVertexType VertexType = eVertexType::Default;
	bool bIsCompute = false;
	/// False for pipelines that don't process any vertices (fullscreen passes)
	bool bHasVertexInput = true;

	VkCullModeFlags CullMode = VK_CULL_MODE_NONE;
	VkFrontFace WindingOrder = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	VkPolygonMode PolygonMode = VK_POLYGON_MODE_FILL;
	VkCompareOp DepthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

	bool bDepthTest = true;
	bool bDepthWrite = true;
	bool bRenderLines = false;

	/// Blend state of each colour attachment
	Hash64 BlendHash = 0;
	/// Format and sample count of each attachment, which is what a render pass has to match to be compatible
	Hash64 PassHash = 0;
	/// Content hashes of the descriptor set layouts, and the push constant ranges
	Hash64 LayoutHash = 0;

public:
	bool operator==(const PipelineKey& other) const = default;

	/// Not for the draw path, resolve a PipelineHandle once and keep it
	Hash64 GetHash() const
	{
		Hash64 hash = FX_HASH64_FNV1A_INIT;

		// Eh, padding doesn't really matter here since we aren't going to be caching the pipelines across platforms.
		hash = HashObj64(*this, hash);

		return hash;
	}

	/**
	 * @brief Folds a value into a running hash. Used for the fields above and by whatever builds the sub-hashes.
	 */
	template <typename TValue>
	static Hash64 MixHash(Hash64 hash, const TValue& value)
	{
		// Fields are hashed one at a time, so padding in this struct never reaches the hash
		return HashObj64(value, hash);
	}
};

} // namespace fx::renderer
