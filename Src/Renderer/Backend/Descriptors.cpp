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
	SizedArray<RxDescriptorPoolSize> pool_sizes(pool_sizes_count);

	for (const auto& desc_count : RemainingDescriptorCounts) {
		pool_sizes.Insert({ .descriptor_type = desc_count.first, .count = desc_count.second });
	}

	SetCapacity = max_sets;

	uint64 handle = 0;

	const VkResult status = static_cast<VkResult>(rx_gpu_descriptor_pool_create(
		device->GetRustDevice(), pool_sizes.pData, pool_sizes.Size, max_sets, enable_descriptor_free, &handle));

	if (status != VK_SUCCESS) {
		PanicVulkan("DescriptorPool", "Failed to create descriptor pool!", status);
	}

	Pool = RxFromRaw<VkDescriptorPool>(handle);
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

	rx_gpu_descriptor_pool_destroy(gGraphics->GetDevice()->GetRustDevice(), RxRaw(Pool));
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

	const VkDescriptorSetLayout layout = gDsLayoutCache->RequestExisting(layout_id);

	if (layout == nullptr) {
		LogFatal("{} does not refer to an existing descriptor set layout.", layout_id);
		Panic("DescriptorSet::Create", "Cannot continue.");
	}

	pool.SetsUsed++;

	uint64 handle = 0;

	const VkResult status = static_cast<VkResult>(rx_gpu_descriptor_set_allocate(
		gGraphics->GetDevice()->GetRustDevice(), RxRaw(pool.Get()), RxRaw(layout), &handle));

	if (status != VK_SUCCESS) {
		LogError("Pool has {} allocated sets, with {} currently in use.", pool.SetCapacity, pool.SetsUsed);
		PanicVulkan("DescriptorSet", "Failed to allocate descriptor set!", status);
	}

	mInternalSet = RxFromRaw<VkDescriptorSet>(handle);
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

static void BindSets(const CommandBuffer& cmd, VkPipelineBindPoint bind_point, VkPipelineLayout layout,
					 uint32 first_set_index, const VkDescriptorSet* sets, uint32 set_count, const uint32* offsets,
					 uint32 offset_count)
{
	static_assert(sizeof(VkDescriptorSet) == sizeof(uint64));

	rx_gpu_cmd_bind_descriptor_sets(gGraphics->GetDevice()->GetRustDevice(), cmd.Cmd, bind_point, RxRaw(layout),
									first_set_index, reinterpret_cast<const uint64*>(sets), set_count, offsets,
									offset_count);
}

void DescriptorSet::BindMultipleOffset(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
									   const Pipeline& pipeline, const Slice<VkDescriptorSet>& sets,
									   const Slice<uint32>& offsets)
{
	BindSets(cmd, bind_point, pipeline.Layout.Get(), first_set_index, sets.pData, sets.Size, offsets.pData,
			 offsets.Size);
}

void DescriptorSet::BindWithOffset(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
								   const Pipeline& pipeline, uint32 offset) const
{
	BindSets(cmd, bind_point, pipeline.Layout.Get(), first_set_index, &mInternalSet, 1, &offset, 1);
}

void DescriptorSet::Bind(uint32 ds_set_index, const CommandBuffer& cmd, const Pipeline& pipeline,
						 const Slice<const uint32> buffer_offsets)
{
	AssertEqual(buffer_offsets.Size, mBufferCount);

	BindSets(cmd, pipeline.GetBindPoint(), pipeline.Layout.Get(), ds_set_index, &mInternalSet, 1, buffer_offsets.pData,
			 buffer_offsets.Size);
}

void DescriptorSet::Bind(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
						 const Pipeline& pipeline) const
{
	uint32 offset = 0;

	BindSets(cmd, bind_point, pipeline.Layout.Get(), first_set_index, &mInternalSet, 1, &offset,
			 (mBufferCount > 0) ? 1 : 0);
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

	StackArray<RxDescriptorWrite, scMaxDescriptorEntries> writes;

	for (const DescriptorEntry& entry : mDescriptorEntries) {
		if (entry.IsImage()) {
			writes.Insert(RxDescriptorWrite {
				.binding = entry.Binding,
				.kind = RX_DESCRIPTOR_IMAGE,
				.sampler = RxRaw(entry.pSampler->InternalSampler),
				.view = RxRaw(entry.pImage->View),
			});
		}
		else if (entry.IsBuffer()) {
			Assert(entry.pBuffer != nullptr);
			AssertNotEqual(entry.pBuffer->Type, eGpuBufferType::None);

			writes.Insert(RxDescriptorWrite {
				.binding = entry.Binding,
				.kind = RX_DESCRIPTOR_BUFFER,
				.buffer = RxRaw(entry.pBuffer->Buffer),
				.offset = entry.BufferOffset,
				.range = entry.BufferRange,
				.buffer_type = static_cast<uint32>(entry.pBuffer->Type),
			});
		}
	}

	rx_gpu_descriptor_set_update(gGraphics->GetDevice()->GetRustDevice(), RxRaw(mInternalSet), writes.pData,
								 writes.Size);

	mbIsBuilt = true;
}

void DescriptorSet::Rebuild(DescriptorPool& pool)
{
	if (mDescriptorEntries.IsEmpty()) {
		return;
	}

	// Free the old (now-stale) descriptor set from the old pool
	if (mInternalSet != nullptr) {
		rx_gpu_descriptor_set_free(gGraphics->GetDevice()->GetRustDevice(), RxRaw(pool.Get()), RxRaw(mInternalSet));
		mInternalSet = nullptr;
	}

	// Make a new descriptor set from the pool
	const VkDescriptorSetLayout layout = gDsLayoutCache->RequestExisting(LayoutID);

	if (layout == nullptr) {
		LogFatal("DescriptorSet::Rebuild: Layout {} does not refer to an existing descriptor set layout.", LayoutID);
		return;
	}

	pool.SetsUsed++;

	uint64 handle = 0;

	const VkResult status = static_cast<VkResult>(rx_gpu_descriptor_set_allocate(
		gGraphics->GetDevice()->GetRustDevice(), RxRaw(pool.Get()), RxRaw(layout), &handle));

	if (status != VK_SUCCESS) {
		PanicVulkan("DescriptorSet::Rebuild", "Failed to allocate descriptor set!", status);
		return;
	}

	mInternalSet = RxFromRaw<VkDescriptorSet>(handle);

	// Rewrite descriptors using the current (potentially new) image/buffer handles
	mbIsBuilt = false;
	Build();
}


} // namespace fx::renderer
