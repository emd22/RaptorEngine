#pragma once

#include <Core/Allocator.hpp>
#include <Core/Assert.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace fx {


class ByteBuffer
{
public:
	/**
	 * @brief Acts as a view into a data buffer, with the functionality of a normal ByteBuffer. Does not copy the data.
	 * Use `InsertRaw` if you want to copy into a new byte buffer.
	 */
	static ByteBuffer FromData(const void* data, size_t size)
	{
		Assert(size <= UINT32_MAX);

		ByteBuffer bb;
		bb.pData = const_cast<void*>(data);
		bb.Size = static_cast<uint32>(size);
		bb.Capacity = static_cast<uint32>(size);
		bb.bIsExternalPtr = true;
		bb.bIsReadOnly = true;
		return bb;
	}

public:
	ByteBuffer() = default;

	explicit ByteBuffer(uint32 size) { Create(size); }
	ByteBuffer(ByteBuffer&& other) noexcept;

	ByteBuffer& operator=(ByteBuffer&& other) noexcept;

	template <typename T>
	ByteBuffer(SizedArray<T>&& other)
	{
		(*this) = std::move(other);
	}

	ByteBuffer(const ByteBuffer& other) = delete;

	template <typename T>
	ByteBuffer& operator=(SizedArray<T>&& other) noexcept
	{
		static_assert(std::is_trivially_copyable_v<T>);

		const size_t size_in_bytes = other.GetSizeInBytes();
		const size_t capacity_in_bytes = other.GetCapacityInBytes();
		Assert(capacity_in_bytes <= UINT32_MAX);

		Free();

		pData = other.pData;
		Size = static_cast<uint32>(size_in_bytes);
		Capacity = static_cast<uint32>(capacity_in_bytes);
		bIsExternalPtr = other.bDoNotDestroy;

		other.pData = nullptr;
		other.Size = 0;
		other.Capacity = 0;
		other.bDoNotDestroy = false;

		return *this;
	}

	ByteBuffer& operator=(const ByteBuffer& other) = delete;

	void Create(uint32 size);

	template <typename T, typename TAllocator>
	void InitAsCopyOf(const SizedArray<T, TAllocator>& data)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		Assert(data.GetCapacityInBytes() <= UINT32_MAX);

		Create(static_cast<uint32>(data.GetCapacityInBytes()));

		const uint32 size_in_bytes = static_cast<uint32>(data.GetSizeInBytes());
		if (size_in_bytes > 0) {
			memcpy(pData, data.pData, size_in_bytes);
		}

		Size = size_in_bytes;
	}

	template <typename T>
	void Insert(const T& object)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		InsertRaw(&object, sizeof(T));
	}

	void InsertRaw(const void* value, uint32 value_size);

	FX_FORCE_INLINE bool IsEmpty() const { return pData == nullptr || Size == 0; }
	FX_FORCE_INLINE bool IsNotEmpty() const { return !IsEmpty(); }

	FX_FORCE_INLINE uint32 GetSize() const { return Size; }
	FX_FORCE_INLINE uint32 GetRemainingSize() const { return Size - ReadOffset; }
	FX_FORCE_INLINE uint32 GetRemainingCapacity() const { return Capacity - Size; }

	FX_FORCE_INLINE void Rewind() { ReadOffset = 0; }

	FX_FORCE_INLINE void Clear()
	{
		Size = 0;
		ReadOffset = 0;
	}


	template <typename T>
	T Get(uint32 index) const
	{
		static_assert(std::is_trivially_copyable_v<T>);
		Assert(index <= Size && sizeof(T) <= Size - index);

		T value {};
		memcpy(&value, static_cast<const uint8*>(pData) + index, sizeof(T));
		return value;
	}

	/**
	 * @brief Returns the value at the current read offset of the byte buffer and advances the read offset by the size
	 * of T.
	 */
	template <typename T>
	T Get()
	{
		T value = Get<T>(ReadOffset);
		ReadOffset += sizeof(T);
		return value;
	}

	template <typename T>
	T* GetPtr()
	{
		return GetPtr<T>(ReadOffset);
	}

	template <typename T>
	T* GetPtr(uint32 index)
	{
		Assert(index <= Size);
		return reinterpret_cast<T*>(static_cast<uint8*>(pData) + index);
	}

	template <typename T>
	const T* GetPtr(uint32 index) const
	{
		Assert(index <= Size);
		return reinterpret_cast<const T*>(static_cast<const uint8*>(pData) + index);
	}

	void* GetRaw(uint32 index);
	const void* GetRaw(uint32 index) const;

	void Free();

	~ByteBuffer() { Free(); }

public:
	void* pData = nullptr;

	uint32 Size = 0;
	uint32 Capacity = 0;
	uint32 ReadOffset = 0;

	bool bIsExternalPtr = false;
	bool bIsReadOnly = false;
};

} // namespace fx
