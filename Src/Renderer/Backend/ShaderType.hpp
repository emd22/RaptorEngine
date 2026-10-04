#pragma once

#include <vulkan/vulkan.h>

#include <Core/Hash.hpp>
#include <Core/Types.hpp>
#include <raptor_ffi.h>

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

static_assert(static_cast<uint32>(eShaderType::Vertex) == RX_SHADER_VERTEX);
static_assert(static_cast<uint32>(eShaderType::Pixel) == RX_SHADER_PIXEL);
static_assert(static_cast<uint32>(eShaderType::Compute) == RX_SHADER_COMPUTE);

/**
 * @brief Get the underlying Vulkan shader stage bit for an ShaderType.
 */
FX_FORCE_INLINE VkShaderStageFlags ToUnderlyingType(eShaderType type)
{
	return rx_shader_stage_flags(static_cast<uint32>(type));
}

FX_FORCE_INLINE VkPipelineBindPoint TypeToBindPoint(eShaderType type)
{
	return static_cast<VkPipelineBindPoint>(rx_shader_bind_point(static_cast<uint32>(type)));
}

FX_FORCE_INLINE const char* TypeToName(eShaderType type) { return rx_shader_type_name(static_cast<uint32>(type)); }
}; // namespace ShaderUtil


} // namespace fx
