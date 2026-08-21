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
struct Target;

class DescriptorPool
{
public:
	void Create(GpuDevice* device, uint32 max_sets = 10, bool enable_descriptor_free = false);

	bool IsInited() const { return (Pool != nullptr); }
	FX_FORCE_INLINE VkDescriptorPool Get() const { return Pool; }
	void AddPoolSize(VkDescriptorType type, uint32_t count) { RemainingDescriptorCounts[type] = count; }

	void Recreate();

	void Destroy();
	~DescriptorPool() { Destroy(); }

public:
	VkDescriptorPool Pool = nullptr;

	uint16 SetCapacity = 0;
	uint16 SetsUsed = 0;

	std::unordered_map<VkDescriptorType, uint32> RemainingDescriptorCounts;

private:
	friend class DescriptorSet;
};

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

	VkDescriptorType GetDescriptorType() const;
	FX_FORCE_INLINE eDescriptorEntryType GetType() const { return Type; }

	FX_FORCE_INLINE bool IsImage() const { return (Type == eDescriptorEntryType::Image); }
	FX_FORCE_INLINE bool IsBuffer() const { return (Type == eDescriptorEntryType::Buffer); }
	FX_FORCE_INLINE bool IsInvalid() const { return (Type == eDescriptorEntryType::None); }

public:
	eDescriptorEntryType Type = eDescriptorEntryType::None;

	uint32 Binding = 0;
	eShaderType ShaderStages = eShaderType::None;

	Image* pImage = nullptr;
	Sampler* pSampler = nullptr;
	RawGpuBuffer* pBuffer = nullptr;

	uint64 BufferOffset = 0;
	uint64 BufferRange = 0;
};


class DescriptorSet
{
private:
	static constexpr uint32 scMaxBuffers = 4;
	static constexpr uint32 scMaxImages = 6;

	static constexpr uint32 scMaxDescriptorEntries = scMaxBuffers + scMaxImages;

public:
	DescriptorSet() = default;

	void Create(DescriptorPool& pool, DescriptorID id, DsLayoutID layout_id, bool has_dynamic_offsets,
				uint32 count = 1);
	bool IsInited() const { return mInternalSet != nullptr; }

	static void BindMultiple(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
							 const Pipeline& pipeline, VkDescriptorSet* sets, uint32 sets_count);

	static void BindMultiple(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
							 const Pipeline& pipeline, const Slice<VkDescriptorSet>& sets);

	static void BindMultipleOffset(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
								   const Pipeline& pipeline, const Slice<VkDescriptorSet>& sets,
								   const Slice<uint32>& offsets);

	void Bind(uint32 ds_set_index, const CommandBuffer& cmd, const Pipeline& pipeline,
			  const Slice<const uint32> buffer_offsets);

	void BindWithOffset(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
						const Pipeline& pipeline, uint32 offset) const;

	void Bind(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
			  const Pipeline& pipeline) const;

	void AddBuffer(uint32 bind_index, RawGpuBuffer* buffer, uint64 offset, uint64 range);
	void AddImage(uint32 bind_index, Image* image, Sampler* sampler);
	void AddImageFromTarget(uint32 bind_index, Target* target, Sampler* sampler);

	void Build();

	VkDescriptorSet Get()
	{
		if (!mbIsBuilt) {
			Build();
		}

		return mInternalSet;
	}

	bool HasDynamicOffsets() const { return mbHasDynamicOffsets; }


	~DescriptorSet() = default;

public:
	DescriptorID ID { HashNull32 };
	DsLayoutID LayoutID { HashNull32 };

private:
	VkDescriptorSet mInternalSet = nullptr;

	uint32 mBufferCount = 0;

	bool mbHasDynamicOffsets : 1 = false;
	bool mbIsBuilt : 1 = false;

	SizedArray<DescriptorEntry> mDescriptorEntries;
};

} // namespace renderer

} // namespace fx
