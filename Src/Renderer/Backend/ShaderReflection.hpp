#pragma once

#include <vulkan/vulkan.h>

#include <Core/Types.hpp>

namespace fx {

enum eShaderReflectionType : uint16
{
	StructuredBuffer,
	CBuffer,
	Texture,
};

namespace ShaderReflectionUtil {
FX_FORCE_INLINE VkDescriptorType TypeToVkDescriptorType(eShaderReflectionType refl_type)
{
	switch (refl_type) {
	case eShaderReflectionType::StructuredBuffer:
		return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	case eShaderReflectionType::CBuffer:
		return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	case eShaderReflectionType::Texture:
		return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	default:;
	}

	return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}

FX_FORCE_INLINE bool RequiresOffset(eShaderReflectionType refl_type)
{
	switch (refl_type) {
	case eShaderReflectionType::StructuredBuffer:
		[[fallthrough]];
	case eShaderReflectionType::CBuffer:
		return true;
	default:;
	}

	return false;
}
} // namespace ShaderReflectionUtil

struct ShaderReflectionEntry
{
	ShaderReflectionEntry() = delete;
	ShaderReflectionEntry(eShaderReflectionType type, uint8 set, uint8 binding) : Type(type), Set(set), Binding(binding)
	{
	}

	uint32 AsUInt() const
	{
		const uint32 value = (static_cast<uint32>(Type) << 16) | (static_cast<uint32>(Set) << 8) |
							 static_cast<uint32>(Binding);
		return value;
	}

	static ShaderReflectionEntry FromUInt(uint32 value)
	{
		ShaderReflectionEntry entry(static_cast<eShaderReflectionType>(static_cast<uint16>(value >> 16)),
									static_cast<uint8>(value >> 8), static_cast<uint8>(value));

		return entry;
	}

public:
	eShaderReflectionType Type;
	uint8 Set;
	uint8 Binding;
};

} // namespace fx
