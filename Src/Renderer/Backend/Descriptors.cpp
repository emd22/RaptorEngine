#include "Descriptors.hpp"

#include <Renderer/Backend/DescriptorCache.hpp>
#include <Renderer/Backend/Image.hpp>
#include <Renderer/Backend/Pipeline.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/Target.hpp>

namespace fx::renderer {

/////////////////////////////////////
// DescriptorEntry
/////////////////////////////////////

namespace DescriptorEntryUtil {
#define ENUM_TYPE eDescriptorEntryType

const char* GetTypeName(eDescriptorEntryType type)
{
	switch (type) {
		FX_ENUM_CASE_NAME(None);
		FX_ENUM_CASE_NAME(Image);
		FX_ENUM_CASE_NAME(Buffer);
	default:;
	}

	return "Unknown";
}

#undef ENUM_TYPE
} // namespace DescriptorEntryUtil


DescriptorEntry DescriptorEntry::AsBuffer(uint32 bind_index, eShaderType shader_stages, RawGpuBuffer* buffer,
										  uint64 offset, uint64 range)
{
	DescriptorEntry entry {
		.Type = eDescriptorEntryType::Buffer,
		.Binding = bind_index,
		.ShaderStages = shader_stages,
		.pBuffer = buffer,
		.BufferOffset = offset,
		.BufferRange = range,
	};

	return entry;
}


DescriptorEntry DescriptorEntry::AsImage(uint32 bind_index, eShaderType shader_stages, Image* image, Sampler* sampler)
{
	DescriptorEntry entry {
		.Type = eDescriptorEntryType::Image,
		.Binding = bind_index,
		.ShaderStages = shader_stages,
		.pImage = image,
		.pSampler = sampler,
	};

	return entry;
}

VkDescriptorType DescriptorEntry::GetDescriptorType() const
{
	if (IsBuffer()) {
		// Get the buffer type

		return GpuBufferUtil::BufferTypeToDescriptorType(pBuffer->Type);
	}
	else if (IsImage()) {
		return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	}

	Panic("DescriptorEntry", "Unknown descriptor entry type");

	return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}


/////////////////////////////////////
// Descriptor Pool Functions
/////////////////////////////////////

void DescriptorPool::Create(GpuDevice* device, uint32 max_sets, bool enable_descriptor_free)
{
	const uint32 pool_sizes_count = RemainingDescriptorCounts.size();
	SizedArray<VkDescriptorPoolSize> pool_sizes(pool_sizes_count);

	for (const auto& desc_count : RemainingDescriptorCounts) {
		pool_sizes.Insert({ .type = desc_count.first, .descriptorCount = desc_count.second });
	}


	VkDescriptorPoolCreateInfo pool_info {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.maxSets = max_sets;
	pool_info.poolSizeCount = pool_sizes.Size;
	pool_info.pPoolSizes = pool_sizes.pData;

	if (enable_descriptor_free) {
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	}

	SetCapacity = max_sets;

	VkResult status = vkCreateDescriptorPool(device->Device, &pool_info, nullptr, &Pool);

	if (status != VK_SUCCESS) {
		PanicVulkan("DescriptorPool", "Failed to create descriptor pool!", status);
	}
}

void DescriptorPool::Recreate()
{
	Destroy();
	Create(gGraphics->GetDevice(), SetCapacity);
}

void DescriptorPool::Destroy()
{
	if (!Pool) {
		return;
	}

	vkDestroyDescriptorPool(gGraphics->GetDevice()->Device, Pool, nullptr);
	Pool = nullptr;
}

/////////////////////////////////////
// Descriptor Sets
/////////////////////////////////////

void DescriptorSet::Create(DescriptorPool& pool, DescriptorID id, DsLayoutID layout_id, bool has_dynamic_offsets,
						   uint32 count)
{
	AssertMsg(pool.IsInited(), "Descriptor pool is not initialized!");

	ID = id;
	LayoutID = layout_id;

	mbHasDynamicOffsets = has_dynamic_offsets;

	VkDescriptorSetAllocateInfo alloc_info {};
	alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc_info.descriptorPool = pool.Get();
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = gDsLayoutCache->RequestExisting(layout_id);

	if (alloc_info.pSetLayouts == nullptr) {
		LogFatal("{} does not refer to an existing descriptor set layout.", layout_id);
		Panic("DescriptorSet::Create", "Cannot continue.");
	}

	pool.SetsUsed++;

	VkResult status = vkAllocateDescriptorSets(gGraphics->GetDevice()->Device, &alloc_info, &mInternalSet);

	if (status != VK_SUCCESS) {
		LogError("Pool has {} allocated sets, with {} currently in use.", pool.SetCapacity, pool.SetsUsed);
		PanicVulkan("DescriptorSet", "Failed to allocate descriptor set!", status);
	}
}

// void DescriptorSet::BindMultiple(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
//                                  const Pipeline& pipeline, VkDescriptorSet* sets, uint32 sets_count)
// {
//     uint32 offsets[10] = { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U };
//     vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout.Get(), first_set_index, sets_count, sets, sets_count,
//                             offsets);
// }

// void DescriptorSet::BindMultiple(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
//                                  const Pipeline& pipeline, const Slice<VkDescriptorSet>& sets)
// {
//     uint32 offsets[10] = { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U };
//     vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout.Get(), first_set_index, sets.Size, sets.pData,
//     sets.Size,
//                             offsets);
// }

void DescriptorSet::BindMultipleOffset(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
									   const Pipeline& pipeline, const Slice<VkDescriptorSet>& sets,
									   const Slice<uint32>& offsets)
{
	vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout.Get(), first_set_index, sets.Size, sets.pData,
							offsets.Size, offsets.pData);
}

void DescriptorSet::BindWithOffset(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
								   const Pipeline& pipeline, uint32 offset) const
{
	vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout.Get(), first_set_index, 1, &mInternalSet, 1, &offset);
}

void DescriptorSet::Bind(uint32 ds_set_index, const CommandBuffer& cmd, const Pipeline& pipeline,
						 const Slice<const uint32> buffer_offsets)
{
	AssertEqual(buffer_offsets.Size, mBufferCount);
	vkCmdBindDescriptorSets(cmd, pipeline.GetBindPoint(), pipeline.Layout.Get(), ds_set_index, 1, &mInternalSet,
							buffer_offsets.Size, buffer_offsets.pData);
}

void DescriptorSet::Bind(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
						 const Pipeline& pipeline) const
{
	uint32 offset = 0;
	vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout.Get(), first_set_index, 1, &mInternalSet,
							(mBufferCount > 0) ? 1 : 0, &offset);
}


void DescriptorSet::AddBuffer(uint32 bind_index, RawGpuBuffer* buffer, uint64 offset, uint64 range)
{
	if (!mDescriptorEntries.IsInited()) {
		mDescriptorEntries.InitCapacity(scMaxDescriptorEntries);
	}

	AssertMsg(buffer != nullptr, "Input buffer cannot be null!");

	// DescriptorEntry input_buffer {
	// 	.Type = eDescriptorEntryType::Buffer,
	// 	.Binding = bind_index,
	// 	.pImage = nullptr,
	// 	.pSampler = nullptr,
	// 	.pBuffer = buffer,
	// 	.BufferOffset = offset,
	// 	.BufferRange = range,
	// };

	mDescriptorEntries.Insert(DescriptorEntry::AsBuffer(bind_index, eShaderType::None, buffer, offset, range));


	++mBufferCount;

	mbIsBuilt = false;
}

void DescriptorSet::AddImageFromTarget(uint32 bind_index, Target* target, Sampler* sampler)
{
	AssertMsg(target != nullptr, "Input target cannot be null!");
	AddImage(bind_index, &target->Image, sampler);
}

void DescriptorSet::AddImage(uint32 bind_index, Image* image, Sampler* sampler)
{
	if (!mDescriptorEntries.IsInited()) {
		mDescriptorEntries.InitCapacity(scMaxDescriptorEntries);
	}

	AssertMsg(image != nullptr, "Input image cannot be null!");

	// DescriptorEntry input_target {
	// 	.Type = eDescriptorEntryType::Image,
	// 	.Binding = bind_index,
	// 	.pImage = image,
	// 	.pSampler = sampler,
	// 	.pBuffer = nullptr,
	// };

	mDescriptorEntries.Insert(DescriptorEntry::AsImage(bind_index, eShaderType::None, image, sampler));

	mbIsBuilt = false;
}

void DescriptorSet::Build()
{
	if (mDescriptorEntries.IsEmpty()) {
		LogWarning("Building empty descriptor set {:x}", reinterpret_cast<uintptr_t>(mInternalSet));
		return;
	}

	Assert(mbIsBuilt == false);

	StackArray<VkDescriptorImageInfo, scMaxImages> image_infos;
	StackArray<VkDescriptorBufferInfo, scMaxBuffers> buffer_infos;
	StackArray<VkWriteDescriptorSet, scMaxDescriptorEntries> write_infos;

	LogInfo("Building DESCRIPTOR: ");

	for (const DescriptorEntry& entry : mDescriptorEntries) {
		if (entry.IsImage()) {
			VkDescriptorImageInfo image_info {
				.sampler = entry.pSampler->InternalSampler,
				.imageView = entry.pImage->View,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};

			const VkWriteDescriptorSet image_write {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = mInternalSet,
				.dstBinding = entry.Binding,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = image_infos.Insert(image_info),
			};

			write_infos.Insert(image_write);
		}
		else if (entry.IsBuffer()) {
			Assert(entry.pBuffer != nullptr);
			AssertNotEqual(entry.pBuffer->Type, eGpuBufferType::None);

			const VkDescriptorBufferInfo buffer_info {
				.buffer = entry.pBuffer->Buffer,
				.offset = entry.BufferOffset,
				.range = entry.BufferRange,
			};

			const VkWriteDescriptorSet buffer_write {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = mInternalSet,
				.dstBinding = entry.Binding,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = GpuBufferUtil::BufferTypeToDescriptorType(entry.pBuffer->Type),
				.pImageInfo = nullptr,
				.pBufferInfo = buffer_infos.Insert(buffer_info),
			};

			write_infos.Insert(buffer_write);
		}
	}


	vkUpdateDescriptorSets(gGraphics->GetDevice()->Device, write_infos.Size, write_infos.pData, 0, nullptr);

	mbIsBuilt = true;
}

void DescriptorSet::Rebuild(DescriptorPool& pool)
{
	if (mDescriptorEntries.IsEmpty()) {
		return;
	}

	// Free the old (now-stale) descriptor set from the old pool
	if (mInternalSet != nullptr) {
		vkFreeDescriptorSets(gGraphics->GetDevice()->Device, pool.Get(), 1, &mInternalSet);
		mInternalSet = nullptr;
	}

	// Make a new descriptor set from the pool
	VkDescriptorSetAllocateInfo alloc_info {};
	alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc_info.descriptorPool = pool.Get();
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = gDsLayoutCache->RequestExisting(LayoutID);

	if (alloc_info.pSetLayouts == nullptr) {
		LogFatal("DescriptorSet::Rebuild: Layout {} does not refer to an existing descriptor set layout.", LayoutID);
		return;
	}

	pool.SetsUsed++;

	VkResult status = vkAllocateDescriptorSets(gGraphics->GetDevice()->Device, &alloc_info, &mInternalSet);

	if (status != VK_SUCCESS) {
		PanicVulkan("DescriptorSet::Rebuild", "Failed to allocate descriptor set!", status);
		return;
	}

	// Rewrite descriptors using the current (potentially new) image/buffer handles
	mbIsBuilt = false;
	Build();
}


} // namespace fx::renderer
