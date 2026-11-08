#pragma once

#include <Core/Assert.hpp>
#include <Core/Types.hpp>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace fx {

namespace GrowthFunctions {
inline constexpr uint32 Double(uint32 v, uint32) { return v * 2; }
inline constexpr uint32 InPages(uint32 v, uint32 page_size) { return v + page_size; }
} // namespace GrowthFunctions

using GrowthFunction = uint32(uint32, uint32);

template <typename TElementType, GrowthFunction TGrow = GrowthFunctions::InPages>
class DynArray
{
	static constexpr uint32 scInitialSize = 2;


	using Iterator = TElementType*;
	using ConstIterator = const TElementType*;

public:
	DynArray() = default;

	DynArray(DynArray&& other) noexcept { (*this) = std::move(other); }

	TElementType& operator[](uint32 index) noexcept { return pData[index]; }
	const TElementType& operator[](uint32 index) const noexcept { return pData[index]; }


	/**
	 * @brief Explicit copy function
	 */
	DynArray Clone() const
	{
		DynArray result;
		result.PageSize = PageSize;
		result.EnsureCapacity(Size);

		for (uint32 i = 0; i < Size; i++) {
			result.Insert(pData[i]);
		}

		return result;
	}

	TElementType* Get(uint32 index)
	{
		if (index >= Size) {
			return nullptr;
		}

		return &pData[index];
	}


	const TElementType* Get(uint32 index) const
	{
		if (index >= Size) {
			return nullptr;
		}

		return &pData[index];
	}

	template <typename... TArgs>
	TElementType& Emplace(TArgs&&... args)
	{
		if (Size < Capacity) {
			TElementType* element = ::new (static_cast<void*>(pData + Size)) TElementType(std::forward<TArgs>(args)...);
			++Size;

			return *element;
		}

		return EmplaceGrow(std::forward<TArgs>(args)...);
	}

	TElementType* GetLast()
	{
		if (Size == 0) {
			return nullptr;
		}

		return &pData[Size - 1];
	}

	void RemoveAt(uint32 index)
	{
		if (index >= Size) {
			return;
		}

		for (uint32 i = index; i + 1 < Size; i++) {
			pData[i] = std::move(pData[i + 1]);
		}

		RemoveLast();
	}

	void RemoveLast()
	{
		if (Size == 0) {
			return;
		}

		TElementType& element = pData[Size - 1];
		element.~TElementType();

		--Size;
	}

	TElementType& Insert(const TElementType& object)
	{
		static_assert(std::is_copy_constructible_v<TElementType>, "DynArray element type is not copyable");

		return Emplace(object);
	}

	TElementType& Insert(TElementType&& object) { return Emplace(std::move(object)); }

	FX_FORCE_INLINE bool IsEmpty() const { return Size == 0; }
	FX_FORCE_INLINE bool IsNotEmpty() const { return !IsEmpty(); }

	void Clear()
	{
		if constexpr (std::is_destructible_v<TElementType>) {
			for (uint32 i = 0; i < Size; i++) {
				pData[i].~TElementType();
			}
		}


		Size = 0;
	}

	FX_FORCE_INLINE void SetPageSize(uint32 page_size) { PageSize = page_size; }


	/**
	 * @brief Copies `size` objects of type `TElementType` from `src` into a DynArray
	 */
	FX_FORCE_INLINE void Assign(const TElementType* src, uint32 size)
	{
		EnsureCapacity(size);

		const uint32 old_size = Size;
		const uint32 common_size = std::min(old_size, size);

		// Overwrite existing elements that overlap
		for (uint32 i = 0; i < common_size; i++) {
			pData[i] = src[i];
		}

		// Destroy the leftover old elements if we are shrinking the size
		if constexpr (std::is_destructible_v<TElementType>) {
			for (uint32 i = size; i < old_size; i++) {
				pData[i].~TElementType();
			}
		}

		// Construct the new elements if we are growing
		for (uint32 i = old_size; i < size; i++) {
			::new (static_cast<void*>(pData + i)) TElementType(src[i]);
		}

		Size = size;
	}

	Iterator begin() { return pData; }
	Iterator end() { return pData + Size; }

	ConstIterator begin() const { return pData; }
	ConstIterator end() const { return pData + Size; }

	DynArray& operator=(DynArray&& other) noexcept
	{
		if (this == &other) {
			return *this;
		}

		// Release what we are holding before taking over the other array's buffer
		Clear();
		std::free(static_cast<void*>(pData));

		Size = other.Size;
		Capacity = other.Capacity;
		PageSize = other.PageSize;

		pData = other.pData;

		// Leave the other array empty, otherwise its destructor destroys the elements we now own
		other.pData = nullptr;
		other.Size = 0;
		other.Capacity = 0;

		return *this;
	}

	bool operator==(const DynArray& other) const
	{
		if (Size != other.Size) {
			return false;
		}

		return std::ranges::equal(*this, other);
	}

	bool operator!=(const DynArray& other) const
	{
		if (Size != other.Size) {
			return true;
		}

		return !std::ranges::equal(*this, other);
	}

	~DynArray()
	{
		Clear();
		std::free(static_cast<void*>(pData));
	}

private:
	/**
	 * @brief Ensure the array can hold at least `size` elements without reallocating.
	 */
	void EnsureCapacity(const uint32 size)
	{
		if (Capacity >= size) {
			return;
		}

		Reallocate(GrowCapacity(size));
	}

	uint32 GrowCapacity(const uint32 required) const
	{
		if (Capacity == 0) {
			return std::max(required, PageSize);
		}

		uint32 capacity = Capacity;

		while (capacity < required) {
			const uint32 next = TGrow(capacity, PageSize);
			AssertMsg(next > capacity, "DynArray growth function did not grow (zero page size or overflow)");
			capacity = next;
		}

		return capacity;
	}

	static TElementType* AllocateBuffer(const uint32 capacity)
	{
		void* memory = std::malloc(sizeof(TElementType) * static_cast<size_t>(capacity));

		if (memory == nullptr) {
			Panic("DynArray", "Out of memory allocating {} elements", capacity);
		}

		return static_cast<TElementType*>(memory);
	}

	/**
	 * @brief Moves the elements into a buffer of `new_capacity`. Trivially copyable elements are realloc'd, anything
	 * else is move-constructed into the new buffer and destroyed in the old one, so element addresses held by the
	 * elements themselves stay valid.
	 */
	void Reallocate(const uint32 new_capacity)
	{
		if constexpr (std::is_trivially_copyable_v<TElementType>) {
			void* memory = std::realloc(static_cast<void*>(pData), sizeof(TElementType) * static_cast<size_t>(new_capacity));

			if (memory == nullptr) {
				Panic("DynArray", "Out of memory allocating {} elements", new_capacity);
			}

			pData = static_cast<TElementType*>(memory);
		}
		else {
			TElementType* new_data = AllocateBuffer(new_capacity);
			RelocateInto(new_data);
			pData = new_data;
		}

		Capacity = new_capacity;
	}

	void RelocateInto(TElementType* new_data)
	{
		if (pData == nullptr) {
			return;
		}

		std::uninitialized_move_n(pData, Size, new_data);
		std::destroy_n(pData, Size);
		std::free(static_cast<void*>(pData));
	}

	/**
	 * @brief Emplace into a full array. The new element is built before the old buffer is released, so arguments that
	 * refer to elements of this array (`array.Insert(array[0])`) are still valid while it is constructed.
	 */
	template <typename... TArgs>
	TElementType& EmplaceGrow(TArgs&&... args)
	{
		const uint32 new_capacity = GrowCapacity(Size + 1);
		TElementType* element = nullptr;

		if constexpr (std::is_trivially_copyable_v<TElementType>) {
			TElementType value(std::forward<TArgs>(args)...);

			Reallocate(new_capacity);
			element = ::new (static_cast<void*>(pData + Size)) TElementType(value);
		}
		else {
			TElementType* new_data = AllocateBuffer(new_capacity);
			element = ::new (static_cast<void*>(new_data + Size)) TElementType(std::forward<TArgs>(args)...);

			RelocateInto(new_data);
			pData = new_data;
			Capacity = new_capacity;
		}

		++Size;

		return *element;
	}

public:
	uint32 Size = 0;
	uint32 Capacity = 0;

	uint32 PageSize = scInitialSize;

	TElementType* pData = nullptr;
};

} // namespace fx
