#pragma once

#include <vulkan/vulkan.h>

#include <Core/Hash.hpp>
#include <Core/Types.hpp>

namespace fx {

enum class eShaderType : uint16
{
	None = 0,
	Vertex = (1 << 0),
	Pixel = (1 << 1),
	Compute = (1 << 2),
};

FxEnumFlags(eShaderType);


using ShaderId = Hash64;

namespace ShaderUtil {
static constexpr uint32 scNumShaderTypes = static_cast<uint32>(eShaderType::Compute) + 1;

/**
 * @brief Get the underlying Vulkan shader stage bit for an ShaderType.
 */
FX_FORCE_INLINE constexpr VkShaderStageFlags ToUnderlyingType(eShaderType type)
{
	VkShaderStageFlags flags = 0;

	if ((type & eShaderType::Vertex) != 0) {
		flags |= VK_SHADER_STAGE_VERTEX_BIT;
	}

	if ((type & eShaderType::Pixel) != 0) {
		flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
	}

	if ((type & eShaderType::Compute) != 0) {
		flags |= VK_SHADER_STAGE_COMPUTE_BIT;
	}

	return flags;
}

FX_FORCE_INLINE VkPipelineBindPoint TypeToBindPoint(eShaderType type)
{
	if (type == eShaderType::Compute) {
		return VK_PIPELINE_BIND_POINT_COMPUTE;
	}

	return VK_PIPELINE_BIND_POINT_GRAPHICS;
}

FX_FORCE_INLINE const char* TypeToName(eShaderType type)
{
	switch (type) {
	case eShaderType::Vertex:
		return "Vertex";
	case eShaderType::Pixel:
		return "Pixel";
	case eShaderType::Compute:
		return "Compute";
	default:;
	}

	return "Unknown";
}
}; // namespace ShaderUtil


} // namespace fx
