#pragma once

#include "Backend/GpuBuffer.hpp"

#include <Core/Types.hpp>
#include <cstdlib>

namespace fx::renderer {

class Uniforms
{
public:
public:
	Uniforms() = default;

	/**
	 * @brief Creates the buffer with `count` slots of `slot_size` bytes per frame in flight. Pass `StorageWithOffset`
	 * as the type to bind it as a storage buffer instead, for data too large for a uniform buffer's range limit.
	 */
	void Create(uint32 slot_size, uint32 count, eGpuBufferType type = eGpuBufferType::UniformWithOffset);

	uint8* GetBasePtr();

	/**
	 * @brief Retrieve the pointer that the buffer starts at relative to the current frame.
	 */
	uint8* GetCurrentBuffer()
	{
		// Offset by the frame currently in flight
		return GetBasePtr() + GetBaseOffset() + GetSlotOffset();
	}

	RawGpuBuffer& GetGpuBuffer() { return mGpuBuffer; }

	void FlushToGpu() { mGpuBuffer.FlushToGpu(GetBaseOffset(), mUniformIndex + 1); }

	template <typename TValueType>
	void Write(const TValueType& value)
	{
		constexpr uint32 type_size = sizeof(TValueType);

		WritePtr(&value, type_size);
	}

	template <typename TPtrType>
	void WritePtr(const TPtrType* value, uint32 size)
	{
		Assert(mGpuBuffer.IsMapped());

		if (mUniformIndex + size >= PageSize) {
			LogError(LC_RENDER, "Could not submit uniform as buffer is full!");
			return;
		}

		uint8* dest_ptr = GetCurrentBuffer() + mUniformIndex;
		std::memcpy(dest_ptr, value, size);

		// Offset for the next value
		mUniformIndex += size;
	}

	template <typename TPtrType>
	void CopyFrom(const TPtrType* buffer, uint32 size)
	{
		Assert(mGpuBuffer.IsMapped());

		if (GetSlotOffset() + size > PageSize) {
			LogError(LC_RENDER, "Could not write buffer that is larger than uniform!");
			return;
		}

		std::memcpy(GetCurrentBuffer(), buffer, size);
	}

	/**
	 * @brief Copies `count` consecutive slots of data into the current frame's page, starting at the current slot, and
	 * moves the slot cursor past them.
	 *
	 * @returns The index of the first slot written, or `UINT32_MAX` if there is not enough room left this frame.
	 */
	template <typename TPtrType>
	uint32 CopySlots(const TPtrType* buffer, uint32 count)
	{
		Assert(mGpuBuffer.IsMapped());

		if (SlotIndex + count > Capacity) {
			return UINT32_MAX;
		}

		const uint32 first_slot = SlotIndex;

		std::memcpy(GetCurrentBuffer(), buffer, count * SlotSize);

		SlotIndex += count;
		mUniformIndex = 0;

		return first_slot;
	}

	void AssertSize(uint32 expected_size) { Assert(mUniformIndex == expected_size); }

	void NextSlot()
	{
		++SlotIndex;
		mUniformIndex = 0;
	}

	template <typename TValueType>
	void SetAllValues(const TValueType& value, bool all_frames)
	{
		SetAllValuesRaw(reinterpret_cast<const void*>(&value), sizeof(TValueType), all_frames);
	}

	/**
	 * @brief Get the index for start of the buffer at the current frame.
	 */
	uint32 GetBaseOffset() const;
	uint32 GetBaseOffset(uint32 frame_index) const;

	uint32 GetSlotOffset() const;
	uint32 GetUniformIndex() const { return mUniformIndex; }

	FX_FORCE_INLINE uint32 GetSlotSize() const { return SlotSize; }

	/**
	 * @brief Resets the uniform buffer back to the start.
	 */
	void Rewind();

	void Destroy() { mGpuBuffer.Destroy(); }

private:
	void SetAllValuesRaw(const void* data, uint32 value_size, bool all_frames);

public:
	/// The current element index (a la `Size` in `SizedArray`)
	uint32 SlotIndex = 0;
	/// The size of each slot in bytes.
	uint32 SlotSize = 0;

	/// The number of slots that are allocated.
	uint32 Capacity = 0;

	uint32 PageSize = 0;

private:
	/// Gpu buffer that stores current uniform buffer data. Note that this is a continguous buffer that stores
	/// `FramesInFlight` number of uniform structures that are of size `scUniformBufferSize`.
	RawGpuBuffer mGpuBuffer {};


	uint32 mUniformIndex = 0;
};

} // namespace fx::renderer
