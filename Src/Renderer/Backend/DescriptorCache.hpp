#pragma once

#include "DescriptorID.hpp"
#include "Descriptors.hpp"

#include <Core/Hash.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Types.hpp>
#include <unordered_map>
#include <Core/HashMap.hpp>


namespace fx {

struct ShaderReflectionEntry;
enum class eShaderType : uint16;

namespace renderer {


class DsLayoutCache
{
public:
	DsLayoutCache();

	std::pair<DsLayoutID, VkDescriptorSetLayout> Request(const SizedArray<DescriptorEntry>& entries);

	/**
	 * @brief Returns the cached layout for `layout_id`, or a null handle if there is none.
	 */
	VkDescriptorSetLayout RequestExisting(DsLayoutID layout_id);

	/**
	 * @brief Frees a descriptor set layout from the cache.
	 */
	void Free(DsLayoutID layout_id);

	DsLayoutID GetID(const SizedArray<DescriptorEntry>& entries);

	const RxDsLayoutCache* GetRustCache() const { return mpCache; }

	void Destroy();
	~DsLayoutCache() { Destroy(); }

	DsLayoutCache(const DsLayoutCache&) = delete;
	DsLayoutCache& operator=(const DsLayoutCache&) = delete;

private:
	RxDsLayoutCache* mpCache = nullptr;
};


/**
 * @brief Makes and keeps the descriptor sets. There is one set for each distinct combination of resources, and the
 * sets hold onto the images and buffers they use, so they follow them when they are recreated.
 */
class DescriptorCache
{
public:
	DescriptorCache();

	DescriptorCache(const DescriptorCache&) = delete;
	DescriptorCache& operator=(const DescriptorCache&) = delete;

	std::pair<DescriptorID, DescriptorSet*> Request(const SizedArray<DescriptorEntry>& entries);
	DescriptorSet* RequestExisting(DescriptorID descriptor_id);

	RxDescriptorCache* GetRust() const { return mpCache; }

	/**
	 * @brief The set for a record that Rust made, such as when it built a pipeline's descriptors
	 */
	DescriptorSet* Wrap(RxDescriptorSet* record);

	/**
	 * @brief Frees a descriptor set, and the references it held. The layout stays in the layout cache.
	 */
	void Free(DescriptorID descriptor_id);

	/**
	 * @brief Rewrites every set with the current handles of its images and buffers.
	 */
	void RebuildAll();

	void Destroy();
	~DescriptorCache() { Destroy(); }

private:
	RxDescriptorCache* mpCache = nullptr;

	/// The sets stay at the same address until they are freed
	std::unordered_map<Hash32, DescriptorSet, Hash32Stl> mSets;
};

} // namespace renderer

} // namespace fx
