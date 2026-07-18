#pragma once

#include "Descriptors.hpp"

#include <Core/Hash.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Types.hpp>
#include <unordered_map>


namespace fx {

struct ShaderReflectionEntry;
enum class eShaderType : uint16;

namespace renderer {

class DsLayoutCache
{
public:
	DsLayoutCache() = default;

	VkDescriptorSetLayout Request(eShaderType shader_type, const SizedArray<ShaderReflectionEntry>& refl, uint32 set);

	void Destroy();
	~DsLayoutCache() { Destroy(); }

public:
	std::unordered_map<Hash64, VkDescriptorSetLayout, Hash64Stl> Cache;
};


class DescriptorCache
{
public:
	DescriptorCache();

	DescriptorSet* Request(eShaderType shader_type, const SizedArray<ShaderReflectionEntry>& refl);

	void Destroy();
	~DescriptorCache() { Destroy(); }

public:
	PagedArray<DescriptorPool> Pools;
	std::unordered_map<Hash32, DescriptorSet, Hash32Stl> Cache;
};

} // namespace renderer

} // namespace fx
