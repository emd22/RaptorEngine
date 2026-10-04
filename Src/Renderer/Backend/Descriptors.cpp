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

		return GpuBufferUtil::BufferTypeToDescriptorType(pBuffer->GetType());
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

DescriptorPool::DescriptorPool(DescriptorPool&& other)
{
	mpRecord = other.mpRecord;
	mPendingSizes = std::move(other.mPendingSizes);

	other.mpRecord = nullptr;
}

DescriptorPool& DescriptorPool::operator=(DescriptorPool&& other)
{
	mpRecord = other.mpRecord;
	mPendingSizes = std::move(other.mPendingSizes);

	other.mpRecord = nullptr;

	return *this;
}

void DescriptorPool::AddPoolSize(VkDescriptorType type, uint32_t count)
{
	for (RxDescriptorPoolSize& size : mPendingSizes) {
		if (size.descriptor_type == static_cast<int32>(type)) {
			size.count = count;
			return;
		}
	}

	mPendingSizes.push_back({ .descriptor_type = static_cast<int32>(type), .count = count });
}

void DescriptorPool::Create(GpuDevice* device, uint32 max_sets, bool enable_descriptor_free)
{
	Destroy();

	int32 status = 0;

	mpRecord = rx_descriptor_pool_new(device->GetRustDevice(), mPendingSizes.data(), mPendingSizes.size(), max_sets,
									  enable_descriptor_free, &status);

	if (mpRecord == nullptr) {
		PanicVulkan("DescriptorPool", "Failed to create descriptor pool!", static_cast<VkResult>(status));
	}
}

VkDescriptorSet DescriptorPool::AllocateSet(VkDescriptorSetLayout layout, VkResult& out_status)
{
	uint64 handle = 0;

	out_status = static_cast<VkResult>(
		rx_descriptor_pool_allocate_set(mpRecord, gGraphics->GetDevice()->GetRustDevice(), RxRaw(layout), &handle));

	return (out_status == VK_SUCCESS) ? RxFromRaw<VkDescriptorSet>(handle) : nullptr;
}

void DescriptorPool::FreeSet(VkDescriptorSet set)
{
	rx_descriptor_pool_free_set(mpRecord, gGraphics->GetDevice()->GetRustDevice(), RxRaw(set));
}

void DescriptorPool::Recreate()
{
	const VkResult status = static_cast<VkResult>(
		rx_descriptor_pool_recreate(mpRecord, gGraphics->GetDevice()->GetRustDevice()));

	if (status != VK_SUCCESS) {
		PanicVulkan("DescriptorPool", "Failed to recreate descriptor pool!", status);
	}
}

void DescriptorPool::Destroy()
{
	if (mpRecord == nullptr) {
		return;
	}

	rx_descriptor_pool_destroy(mpRecord, gGraphics->GetDevice()->GetRustDevice());
	mpRecord = nullptr;
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

	VkResult status = VK_SUCCESS;

	mInternalSet = pool.AllocateSet(layout, status);

	if (status != VK_SUCCESS) {
		LogError("Pool has {} allocated sets, with {} currently in use.", pool.GetSetCapacity(), pool.GetSetsUsed());
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
				.view = entry.pImage->GetRawView(),
			});
		}
		else if (entry.IsBuffer()) {
			Assert(entry.pBuffer != nullptr);
			AssertNotEqual(entry.pBuffer->GetType(), eGpuBufferType::None);

			writes.Insert(RxDescriptorWrite {
				.binding = entry.Binding,
				.kind = RX_DESCRIPTOR_BUFFER,
				.buffer = entry.pBuffer->GetRaw(),
				.offset = entry.BufferOffset,
				.range = entry.BufferRange,
				.buffer_type = static_cast<uint32>(entry.pBuffer->GetType()),
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
		pool.FreeSet(mInternalSet);
		mInternalSet = nullptr;
	}

	// Make a new descriptor set from the pool
	const VkDescriptorSetLayout layout = gDsLayoutCache->RequestExisting(LayoutID);

	if (layout == nullptr) {
		LogFatal("DescriptorSet::Rebuild: Layout {} does not refer to an existing descriptor set layout.", LayoutID);
		return;
	}

	VkResult status = VK_SUCCESS;

	mInternalSet = pool.AllocateSet(layout, status);

	if (status != VK_SUCCESS) {
		PanicVulkan("DescriptorSet::Rebuild", "Failed to allocate descriptor set!", status);
		return;
	}

	// Rewrite descriptors using the current (potentially new) image/buffer handles
	mbIsBuilt = false;
	Build();
}


} // namespace fx::renderer
