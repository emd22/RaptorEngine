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
	// GetHash() hashes the bytes of the whole key, so there must be no padding in it, as padding bytes are not kept
	// through copies and two equal keys could hash differently. The fields are ordered largest first to leave none, and
	// the static_assert below catches a new field that breaks it.

	/// Hash of the macros the shader was compiled with
	Hash64 MacroHash = 0;
	/// Blend state of each colour attachment
	Hash64 BlendHash = 0;
	/// Format and sample count of each attachment, which is what a render pass has to match to be compatible
	Hash64 PassHash = 0;
	/// Content hashes of the descriptor set layouts, and the push constant ranges
	Hash64 LayoutHash = 0;

	eShaderName Shader = eShaderName::NumShaders;
	eVertexType VertexType = eVertexType::Default;

	VkCullModeFlags CullMode = VK_CULL_MODE_NONE;
	VkFrontFace WindingOrder = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	VkPolygonMode PolygonMode = VK_POLYGON_MODE_FILL;
	VkCompareOp DepthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

	bool bIsCompute = false;
	/// False for pipelines that don't process any vertices (fullscreen passes)
	bool bHasVertexInput = true;
	bool bDepthTest = true;
	bool bDepthWrite = true;
	bool bRenderLines = false;

	/// Fills the struct out to a multiple of its alignment. Always zero.
	uint8 Reserved[3] = {};

public:
	bool operator==(const PipelineKey& other) const = default;

	/// Not for the draw path, resolve a PipelineHandle once and keep it
	Hash64 GetHash() const { return HashObj64(*this); }

	/**
	 * @brief Folds a value into a running hash, for building the sub-hashes that go in the key.
	 */
	template <typename TValue>
	static Hash64 MixHash(Hash64 hash, const TValue& value)
	{
		return HashObj64(value, hash);
	}
};

static_assert(std::has_unique_object_representations_v<PipelineKey>,
			  "PipelineKey is hashed as raw bytes, so it can not have padding. Reorder the fields or add to Reserved.");

} // namespace fx::renderer
