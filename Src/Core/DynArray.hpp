#pragma once

#include <Core/Assert.hpp>
#include <Core/Types.hpp>
#include <algorithm>
#include <cstdlib>

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

	DynArray(DynArray&& other) { (*this) = std::move(other); }

	TElementType& operator[](uint32 index) noexcept { return pData[index]; }
	const TElementType& operator[](uint32 index) const noexcept { return pData[index]; }

	TElementType* Get(uint32 index)
	{
		if (index > Size) {
			return nullptr;
		}

		return &pData[index];
	}


	const TElementType* Get(uint32 index) const
	{
		if (index > Size) {
			return nullptr;
		}

		return &pData[index];
	}

	template <typename... Args>
	TElementType& Emplace(Args&&... args)
	{
		EnsureCapacity(Size + 1);

		TElementType* element = &pData[Size++];
		new (element) TElementType(std::forward<Args>(args)...);

		return *element;
	}

	TElementType* GetLast()
	{
		if (Size == 0) {
			return nullptr;
		}

		return &pData[Size - 1];
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

	void Insert(const TElementType& object)
	{
		EnsureCapacity(Size + 1);

		TElementType* dst = &pData[Size++];
		if constexpr (std::is_copy_constructible_v<TElementType>) {
			new (dst) TElementType(object);
		}
		else {
			memcpy(dst, &object, sizeof(TElementType));
		}
	}

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
		std::free(reinterpret_cast<void*>(pData));

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

	~DynArray()
	{
		// Destroy all objects

		if constexpr (std::is_destructible_v<TElementType>) {
			for (uint32 i = 0; i < Size; i++) {
				pData[i].~TElementType();
			}
		}


		if (pData) {
			std::free(reinterpret_cast<void*>(pData));
		}
	}

private:
	/**
	 * @brief Ensure there is enough capacity in the array for `size` more elements to be stored
	 */
	FX_FORCE_INLINE void EnsureCapacity(const uint32 size)
	{
		const uint32 new_size = size;

		// If we already have enough capacity, nothing more to do
		if (Capacity >= new_size) {
			return;
		}

		// If the data has not been allocated yet, allocate it with a minimum page size or the required size
		if (pData == nullptr) {
			Capacity = (new_size < PageSize) ? PageSize : new_size;
			pData = reinterpret_cast<TElementType*>(std::malloc(sizeof(TElementType) * Capacity));
			return;
		}

		// Keep growing the capacity until it can accommodate the requested new size
		while (Capacity < new_size) {
			Capacity = TGrow(Capacity, PageSize);
		}

		// Resize our buffer to the new capacity
		pData = reinterpret_cast<TElementType*>(std::realloc(pData, sizeof(TElementType) * Capacity));
	}

public:
	uint32 Size = 0;
	uint32 Capacity = 0;

	uint32 PageSize = scInitialSize;

	TElementType* pData = nullptr;
};

} // namespace fx
