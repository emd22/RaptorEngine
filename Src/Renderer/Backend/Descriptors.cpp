#include "Descriptors.hpp"

#include <Renderer/Backend/Image.hpp>
#include <Renderer/Backend/Pipeline.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>
#include <Renderer/Target.hpp>

namespace fx::renderer {

/////////////////////////////////////
// Descriptor Pool Functions
/////////////////////////////////////

void DescriptorPool::Create(GpuDevice* device, uint32 max_sets)
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

	SetCapacity = max_sets;

	VkResult status = vkCreateDescriptorPool(device->Device, &pool_info, nullptr, &Pool);

	if (status != VK_SUCCESS) {
		PanicVulkan("DescriptorPool", "Failed to create descriptor pool!", status);
	}
}

void DescriptorPool::Recreate()
{
	Destroy();
	Create(gRenderer->GetDevice(), SetCapacity);
}

void DescriptorPool::Destroy()
{
	if (!Pool) {
		return;
	}

	vkDestroyDescriptorPool(gRenderer->GetDevice()->Device, Pool, nullptr);
	Pool = nullptr;
}

/////////////////////////////////////
// Descriptor Sets
/////////////////////////////////////

void DescriptorSet::Create(DescriptorPool& pool, VkDescriptorSetLayout layout, bool has_dynamic_offsets, uint32 count)
{
	AssertMsg(pool.IsInited(), "Descriptor pool is not initialized!");

	Layout = layout;
	mbHasDynamicOffsets = has_dynamic_offsets;

	VkDescriptorSetAllocateInfo alloc_info {};
	alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc_info.descriptorPool = pool.Get();
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &layout;

	pool.SetsUsed++;

	VkResult status = vkAllocateDescriptorSets(gRenderer->GetDevice()->Device, &alloc_info, &Set);

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
	vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout.Get(), first_set_index, 1, &Set, 1, &offset);
}

void DescriptorSet::Bind(uint32 ds_set_index, const CommandBuffer& cmd, const Pipeline& pipeline,
						 const Slice<uint32> buffer_offsets)
{
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Layout.Get(), ds_set_index, 1, &Set,
							buffer_offsets.Size, buffer_offsets.pData);
}

void DescriptorSet::Bind(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
						 const Pipeline& pipeline) const
{
	uint32 offset = 0;
	vkCmdBindDescriptorSets(cmd, bind_point, pipeline.Layout.Get(), first_set_index, 1, &Set, (NumBuffers > 0) ? 1 : 0,
							&offset);
}


void DescriptorSet::AddBuffer(uint32 bind_index, RawGpuBuffer* buffer, uint64 offset, uint64 range)
{
	if (!mDescriptorEntries.IsInited()) {
		mDescriptorEntries.InitCapacity(scMaxDescriptorEntries);
	}

	AssertMsg(buffer != nullptr, "Input buffer cannot be null!");

	DescriptorEntry input_buffer {
		.BindIndex = bind_index,
		.pImage = nullptr,
		.pSampler = nullptr,
		.pBuffer = buffer,
		.BufferOffset = offset,
		.BufferRange = range,
	};

	mDescriptorEntries.Insert(input_buffer);

	++NumBuffers;

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

	DescriptorEntry input_target {
		.BindIndex = bind_index,
		.pImage = image,
		.pSampler = sampler,
		.pBuffer = nullptr,
	};

	mDescriptorEntries.Insert(input_target);

	mbIsBuilt = false;
}

void DescriptorSet::Build()
{
	if (mDescriptorEntries.IsEmpty()) {
		return;
	}

	Assert(mbIsBuilt == false);

	StackArray<VkDescriptorImageInfo, scMaxImages> image_infos;
	StackArray<VkDescriptorBufferInfo, scMaxBuffers> buffer_infos;
	StackArray<VkWriteDescriptorSet, scMaxDescriptorEntries> write_infos;

	for (const DescriptorEntry& entry : mDescriptorEntries) {
		if (entry.pImage) {
			VkDescriptorImageInfo image_info {
				.sampler = entry.pSampler->InternalSampler,
				.imageView = entry.pImage->View,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};

			const VkWriteDescriptorSet image_write {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = Set,
				.dstBinding = entry.BindIndex,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = image_infos.Insert(image_info),
			};

			write_infos.Insert(image_write);
		}
		else if (entry.pBuffer) {
			const VkDescriptorBufferInfo buffer_info {
				.buffer = entry.pBuffer->Buffer,
				.offset = entry.BufferOffset,
				.range = entry.BufferRange,
			};

			const VkWriteDescriptorSet buffer_write {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = Set,
				.dstBinding = entry.BindIndex,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = GpuBufferUtil::BufferTypeToDescriptorType(entry.pBuffer->Type),
				.pImageInfo = nullptr,
				.pBufferInfo = buffer_infos.Insert(buffer_info),
			};

			write_infos.Insert(buffer_write);
		}
	}

	vkUpdateDescriptorSets(gRenderer->GetDevice()->Device, write_infos.Size, write_infos.pData, 0, nullptr);

	mbIsBuilt = true;

	mDescriptorEntries.Free();
}

void DescriptorSet::DestroyLayout()
{
	if (Layout == nullptr) {
		return;
	}
	vkDestroyDescriptorSetLayout(gRenderer->GetDevice()->Device, Layout, nullptr);
	Layout = nullptr;
}

void DescriptorSet::Destroy()
{
	// if (Layout != nullptr) {
	//     vkDestroyDescriptorSetLayout(gRenderer->GetDevice()->Device, Layout, nullptr);
	//     Layout = nullptr;
	// }

	mDescriptorEntries.Clear();
}

} // namespace fx::renderer
