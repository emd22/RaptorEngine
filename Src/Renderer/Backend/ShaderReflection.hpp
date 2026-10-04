#pragma once

#include <vulkan/vulkan.h>

#include <Core/Types.hpp>
#include <raptor_ffi.h>

namespace fx {

enum eShaderReflectionType : uint16
{
	StructuredBuffer,
	CBuffer,
	Texture,
};

namespace ShaderReflectionUtil {
static_assert(eShaderReflectionType::StructuredBuffer == 0);
static_assert(eShaderReflectionType::CBuffer == 1);
static_assert(eShaderReflectionType::Texture == 2);

FX_FORCE_INLINE VkDescriptorType TypeToVkDescriptorType(eShaderReflectionType refl_type)
{
	return static_cast<VkDescriptorType>(rx_reflection_descriptor_type(static_cast<uint16>(refl_type)));
}

FX_FORCE_INLINE bool RequiresOffset(eShaderReflectionType refl_type)
{
	return rx_reflection_requires_offset(static_cast<uint16>(refl_type)) != 0;
}

FX_FORCE_INLINE const char* GetName(eShaderReflectionType refl_type)
{
	return rx_reflection_name(static_cast<uint16>(refl_type));
}

} // namespace ShaderReflectionUtil

struct ShaderReflectionEntry
{
	ShaderReflectionEntry() = delete;
	ShaderReflectionEntry(eShaderReflectionType type, uint8 set, uint8 binding) : Type(type), Set(set), Binding(binding)
	{
	}

	bool RequiresOffset() const { return ShaderReflectionUtil::RequiresOffset(Type); }

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
