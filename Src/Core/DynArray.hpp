#pragma once

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

	void Insert(const TElementType& object)
	{
		ResizeIfNeeded();

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
		for (size_t i = 0; i < Size; i++) {
			TElementType& element = pData[i];
			element.~TElementType();
		}

		Size = 0;
	}

	FX_FORCE_INLINE void SetPageSize(uint32 page_size) { PageSize = page_size; }


	DynArray& operator=(DynArray<TElementType>&& other) noexcept
	{
		Size = other.Size;
		Capacity = other.Capacity;
		PageSize = other.PageSize;

		pData = other.pData;

		other.pData = nullptr;

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
	void ResizeIfNeeded()
	{
		// If the data has not been allocated yet, allocate it with a minimum size.
		if (pData == nullptr) {
			Capacity = PageSize;
			pData = reinterpret_cast<TElementType*>(std::malloc(sizeof(TElementType) * Capacity));
		}

		// If there is no remaining space, use the growth function and resize our buffer.
		if (pData && Size + 1 >= Capacity) {
			Capacity = TGrow(Capacity, PageSize);
			pData = reinterpret_cast<TElementType*>(std::realloc(pData, sizeof(TElementType) * Capacity));
		}
	}

public:
	uint32 Size = 0;
	uint32 Capacity = 0;

	uint32 PageSize = scInitialSize;

	TElementType* pData = nullptr;
};

} // namespace fx
