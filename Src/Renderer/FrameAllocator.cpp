#include "FrameAllocator.hpp"

#include "Backend/Commands.hpp"
#include "Backend/Pipeline.hpp"
#include "Constants.hpp"
#include "GraphicsBackend.hpp"

#include <Core/Log.hpp>
#include <Renderer/Globals.hpp>

namespace fx::renderer {

void FrameAllocator::Create()
{
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(gGraphics->GetDevice()->Physical, &properties);

	mAlignment = std::max<uint32>(static_cast<uint32>(properties.limits.minUniformBufferOffsetAlignment), 16);

	AssertMsg((scPageSize % mAlignment) == 0, "Frame allocator page size must be a multiple of the alignment");

	mBuffer.Create(eGpuBufferType::UniformWithOffset, (scPageSize * FramesInFlight) + scMaxAllocationSize,
				   VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, eGpuBufferFlags::PersistentMapped);

	Rewind();
}

void FrameAllocator::Destroy() { mBuffer.Destroy(); }

void FrameAllocator::Rewind()
{
	mPageBase = scPageSize * gGraphics->GetFrameNumber();
	mCursor = mPageBase;
	++mGeneration;

	mBoundCmd = nullptr;
	mBoundOffset = UINT32_MAX;
}

void FrameAllocator::Flush()
{
	if (mCursor > mPageBase) {
		mBuffer.FlushToGpu(mPageBase, mCursor - mPageBase);
	}
}

uint32 FrameAllocator::Allocate(uint32 size, void** out_data)
{
	AssertLess(size, scMaxAllocationSize + 1);

	const uint32 offset = (mCursor + (mAlignment - 1)) & ~(mAlignment - 1);

	if (offset + size > mPageBase + scPageSize) {
		static bool sbWarned = false;

		if (!sbWarned) {
			LogError(LC_RENDER, "Frame allocator is out of space ({} bytes per frame)", scPageSize);
			sbWarned = true;
		}

		*out_data = static_cast<uint8*>(mBuffer.pMappedBuffer) + mPageBase;
		return mPageBase;
	}

	mCursor = offset + size;

	*out_data = static_cast<uint8*>(mBuffer.pMappedBuffer) + offset;

	return offset;
}

void FrameAllocator::Bind(const CommandBuffer& cmd, const Pipeline& pipeline, uint32 offset)
{
	AssertMsg(pipeline.HasFrameData(), "Pipeline was not built with frame data");

	const uint32 serial = Pipeline::GetBindSerial();

	if (mBoundCmd == cmd.Get() && mBoundSerial == serial && mBoundOffset == offset) {
		return;
	}

	for (const Pipeline::DescriptorRef& ref : pipeline.DescriptorIDs) {
		if (ref.SetIndex != pipeline.FrameDataSet) {
			continue;
		}

		ref.pSet->BindWithOffset(ref.SetIndex, cmd, pipeline.GetBindPoint(), pipeline, offset);

		mBoundCmd = cmd.Get();
		mBoundSerial = serial;
		mBoundOffset = offset;

		return;
	}

	AssertMsg(false, "Pipeline has no frame data descriptor set");
}

DescriptorEntry FrameAllocator::GetDescriptorEntry()
{
	return DescriptorEntry::AsBuffer(scBinding, scStages, &mBuffer, 0, scMaxAllocationSize);
}

} // namespace fx::renderer
