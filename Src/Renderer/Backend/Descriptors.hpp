#pragma once

#include "DescriptorID.hpp"
#include "Device.hpp"
#include "GpuBuffer.hpp"
#include "Sampler/Sampler.hpp"
#include "ShaderType.hpp"

#include <vulkan/vulkan.h>

#include <Core/Assert.hpp>
#include <Core/SizedArray.hpp>
#include <Renderer/Constants.hpp>

#include "vulkan/vulkan_core.h"

namespace fx {

class Image;

namespace renderer {

class Pipeline;
class CommandBuffer;
struct Target;

enum class eDescriptorEntryType
{
	None,
	Image,
	Buffer,
};

namespace DescriptorEntryUtil {
const char* GetTypeName(eDescriptorEntryType type);
}

struct DescriptorEntry
{
	/**
	 * @brief Builds a descriptor entry for a GPU buffer
	 */
	static DescriptorEntry AsBuffer(uint32 bind_index, eShaderType shader_stages, RawGpuBuffer* buffer, uint64 offset,
									uint64 range);

	/**
	 * @brief Builds a descriptor entry for an image
	 */
	static DescriptorEntry AsImage(uint32 bind_index, eShaderType shader_stages, Image* image, Sampler* sampler);

	/**
	 * @brief Builds a descriptor entry for an image that is only known by its record, such as a target's
	 */
	static DescriptorEntry AsImage(uint32 bind_index, eShaderType shader_stages, const RxImage* image,
								   Sampler* sampler);

	VkDescriptorType GetDescriptorType() const;
	FX_FORCE_INLINE eDescriptorEntryType GetType() const { return Type; }

	FX_FORCE_INLINE bool IsImage() const { return (Type == eDescriptorEntryType::Image); }
	FX_FORCE_INLINE bool IsBuffer() const { return (Type == eDescriptorEntryType::Buffer); }
	FX_FORCE_INLINE bool IsInvalid() const { return (Type == eDescriptorEntryType::None); }

public:
	eDescriptorEntryType Type = eDescriptorEntryType::None;

	uint32 Binding = 0;
	eShaderType ShaderStages = eShaderType::None;

	const RxImage* pImage = nullptr;
	Sampler* pSampler = nullptr;
	RawGpuBuffer* pBuffer = nullptr;

	uint64 BufferOffset = 0;
	uint64 BufferRange = 0;
};


/**
 * @brief A descriptor set made by the `DescriptorCache`. The set and the references it holds onto live in a record
 * owned by Rust, this is the way to bind it.
 */
/// The entry as Rust takes it. The buffer or image has to exist.
RxDescriptorEntry ToRustEntry(const DescriptorEntry& entry);

class DescriptorSet
{
public:
	DescriptorSet(RxDescriptorSet* record, DescriptorID id, DsLayoutID layout_id)
		: ID(id), LayoutID(layout_id), mpRecord(record)
	{
	}

	FX_FORCE_INLINE VkDescriptorSet Get() const { return reinterpret_cast<VkDescriptorSet>(mpRecord->set); }
	FX_FORCE_INLINE bool IsInited() const { return mpRecord->set != 0; }
	FX_FORCE_INLINE bool HasDynamicOffsets() const { return mpRecord->has_dynamic_offsets != 0; }

	/// Binds the set at `ds_set_index` with one dynamic offset for each of its buffers.
	void Bind(uint32 ds_set_index, const CommandBuffer& cmd, const Pipeline& pipeline,
			  const Slice<const uint32> buffer_offsets);

	void BindWithOffset(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
						const Pipeline& pipeline, uint32 offset) const;

	void Bind(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
			  const Pipeline& pipeline) const;

public:
	DescriptorID ID;
	DsLayoutID LayoutID;

private:
	RxDescriptorSet* mpRecord = nullptr;
};

} // namespace renderer

} // namespace fx
