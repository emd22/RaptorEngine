/*
 * File:        FrameAllocator.hpp
 * Author:      emd22
 * Created:     24/10/2026
 * Description: An allocator used for passing information per frame to shaders. Resizes as needed.
 */

#pragma once

#include "Backend/Descriptors.hpp"
#include "Backend/GpuBuffer.hpp"
#include "Backend/ShaderType.hpp"

#include <Core/Types.hpp>
#include <cstring>

namespace fx::renderer {

class CommandBuffer;
class Pipeline;

class FrameAllocator
{
public:
	/// The size of an allocated memory page. Allocated more pages as space is needed.
	/// Note: set this to something fairly high to avoid reallocating gpu buffers on the fly, but not too high as we
	/// don't want to have too much slack space.
	static constexpr uint32 scPageSize = 1024 * 1024;

	/// The maximum size of any single allocation.
	static constexpr uint32 scMaxAllocationSize = 4096;

	static constexpr uint32 scBinding = 0;
	static constexpr eShaderType scStages = eShaderType::Vertex | eShaderType::Pixel | eShaderType::Compute;

public:
	void Create();
	void Destroy();

	void Rewind();
	void Flush();

	uint32 Allocate(uint32 size, void** out_data);

	template <typename T>
	uint32 Push(const T& value)
	{
		static_assert(sizeof(T) <= scMaxAllocationSize);

		void* data = nullptr;
		const uint32 offset = Allocate(sizeof(T), &data);

		std::memcpy(data, &value, sizeof(T));

		return offset;
	}

	void Bind(const CommandBuffer& cmd, const Pipeline& pipeline, uint32 offset);

	DescriptorEntry GetDescriptorEntry();

	uint32 GetUsedBytes() const { return mCursor - mPageBase; }
	uint32 GetGeneration() const { return mGeneration; }

private:
	RawGpuBuffer mBuffer;

	uint32 mAlignment = 256;

	uint32 mPageBase = 0;
	uint32 mCursor = 0;
	uint32 mGeneration = 0;

	VkCommandBuffer mBoundCmd = nullptr;
	uint32 mBoundSerial = 0;
	uint32 mBoundOffset = UINT32_MAX;
};

} // namespace fx::renderer
