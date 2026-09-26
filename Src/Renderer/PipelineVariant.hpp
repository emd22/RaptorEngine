#pragma once

#include <Core/Types.hpp>

namespace fx {

/**
 * @brief What a draw needs from a pipeline that depends on the object or material being drawn, rather than on the pass
 */
enum class ePipelineFeatures : uint32
{
	None = 0,

	/// The material has a normal map
	NormalMap = (1 << 0),
	/// The object is skinned
	Skinned = (1 << 1),
	/// The material is alpha masked, so the pipeline has to sample its albedo and discard
	AlphaMask = (1 << 2),
};

FxEnumFlags(ePipelineFeatures);

} // namespace fx

namespace fx::renderer {

/**
 * @brief A part of the frame that draws objects. Each has its own pipeline for each set of features.
 */
enum class ePipelinePass : uint8
{
	/// Depth and normal prepass
	Depth,
	/// Opaque geometry
	Forward,
	/// Geometry that is blended, drawn back to front
	ForwardBlend,
	/// Casters drawn into the shadow atlas
	Shadow,

	Count,
};

constexpr uint32 scNumPipelinePasses = static_cast<uint32>(ePipelinePass::Count);

/// Every combination of the ePipelineFeatures bits
constexpr uint32 scNumFeatureCombinations = 1U << 3;

} // namespace fx::renderer
