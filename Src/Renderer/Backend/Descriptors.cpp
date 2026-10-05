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
	return AsImage(bind_index, shader_stages, image->GetRecord(), sampler);
}

DescriptorEntry DescriptorEntry::AsImage(uint32 bind_index, eShaderType shader_stages, const RxImage* image,
										 Sampler* sampler)
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

RxDescriptorEntry ToRustEntry(const DescriptorEntry& entry)
{
	RxDescriptorEntry rust_entry = {
		.binding = entry.Binding,
		.stages = ShaderUtil::ToUnderlyingType(entry.ShaderStages),
	};

	if (entry.IsBuffer()) {
		Assert(entry.pBuffer != nullptr);
		Assert(entry.pBuffer->Get() != VK_NULL_HANDLE);

		rust_entry.kind = RX_DESCRIPTOR_BUFFER;
		rust_entry.buffer = entry.pBuffer->GetRecord();
		rust_entry.offset = entry.BufferOffset;
		rust_entry.range = entry.BufferRange;
	}
	else if (entry.IsImage()) {
		Assert(entry.pImage != nullptr);
		Assert(entry.pSampler != nullptr);

		rust_entry.kind = RX_DESCRIPTOR_IMAGE;
		rust_entry.image = entry.pImage;
		rust_entry.sampler = RxRaw(entry.pSampler->InternalSampler);
	}

	return rust_entry;
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
// Descriptor Sets
/////////////////////////////////////

static void BindSet(const RxDescriptorSet* set, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
					VkPipelineLayout layout, uint32 first_set_index, const uint32* offsets, uint32 offset_count)
{
	rx_descriptor_set_bind(set, gGraphics->GetDevice()->GetRustDevice(), cmd.Cmd, bind_point, RxRaw(layout),
						   first_set_index, offsets, offset_count);
}

void DescriptorSet::BindWithOffset(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
								   const Pipeline& pipeline, uint32 offset) const
{
	BindSet(mpRecord, cmd, bind_point, pipeline.GetLayout(), first_set_index, &offset, 1);
}

void DescriptorSet::Bind(uint32 ds_set_index, const CommandBuffer& cmd, const Pipeline& pipeline,
						 const Slice<const uint32> buffer_offsets)
{
	AssertEqual(buffer_offsets.Size, mpRecord->buffer_count);

	BindSet(mpRecord, cmd, pipeline.GetBindPoint(), pipeline.GetLayout(), ds_set_index, buffer_offsets.pData,
			buffer_offsets.Size);
}

void DescriptorSet::Bind(uint32 first_set_index, const CommandBuffer& cmd, VkPipelineBindPoint bind_point,
						 const Pipeline& pipeline) const
{
	uint32 offset = 0;

	BindSet(mpRecord, cmd, bind_point, pipeline.GetLayout(), first_set_index, &offset,
			(mpRecord->buffer_count > 0) ? 1 : 0);
}

} // namespace fx::renderer
