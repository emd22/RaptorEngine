#pragma once

// #include "Allocator.hpp"
#include "Assert.hpp"
#include "Defines.hpp"

#include <stddef.h>
#include <stdio.h>

#include <Core/Allocator.hpp>
#include <Engine.hpp>
#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

#ifdef FX_SIZED_ARRAY_DEBUG
#include "Log.hpp"
#endif

namespace fx {

static inline void NoMemError()
{
	puts("SizedArray: out of memory");
	Terminate();
}

template <typename TElementType, typename TAllocator = StdAllocator>
	requires C_IsAllocator<TAllocator>
class SizedArray
{
public:
	static constexpr int64 scItemNotFound = -1;

	using SizeType = size_t;

	using Iterator = TElementType*;
	using ConstIterator = const TElementType*;


public:
	static SizedArray CreateCopyOf(const TElementType* ptr, SizeType size)
	{
		SizedArray arr;
		arr.InitAsCopyOf(ptr, size);

		return arr;
	}

	static SizedArray CreateAsSize(SizeType size)
	{
		SizedArray arr;
		arr.InitSize(size);

		return arr;
	}

	static SizedArray CreateEmpty() { return SizedArray(nullptr, 0); }

	static SizedArray Clone(const SizedArray& other)
	{
		SizedArray clone;
		clone.CloneFrom(other);

		return clone;
	}

public:
	SizedArray(TElementType* ptr, size_t size) : pData(ptr), Size(size), Capacity(size), bDoNotDestroy(true) {}

	SizedArray(size_t element_count) : Capacity(element_count)
	{
		InternalAllocateArray(element_count);
		Size = 0;
	}

	SizedArray(std::initializer_list<TElementType> list)
	{
		if (list.size() == 0) {
			return;
		}

		InitCapacity(list.size());

		for (const TElementType& obj : list) {
			Insert(obj);
		}
	};

	SizedArray(SizedArray&& other) noexcept { TakeFrom(other); }

	SizedArray() = default;

	~SizedArray() { Free(); }


	void FreeNoDestructor()
	{
		if (pData == nullptr || bDoNotDestroy) {
			return;
		}

		TAllocator::FreeRaw(static_cast<void*>(pData));

		pData = nullptr;
		Capacity = 0;
		Size = 0;
	}

	void Free()
	{
		if (bDoNotDestroy) {
			// A non-owning view only forgets the memory it points at.
			pData = nullptr;
			Size = 0;
			Capacity = 0;
			bDoNotDestroy = false;
			return;
		}

		if (pData == nullptr) {
			return;
		}

		if constexpr (!std::is_trivially_destructible<TElementType>::value) {
			for (size_t i = 0; i < Size; i++) {
				TElementType& element = pData[i];
				element.~TElementType();
			}
		}

		FreeNoDestructor();
	}

	Iterator begin() { return pData; }
	Iterator end() { return pData + Size; }

	ConstIterator begin() const { return pData; }
	ConstIterator end() const { return pData + Size; }

	operator TElementType*() const { return pData; }

	operator TElementType&() const { return *pData; }

	const TElementType& operator[](size_t index) const
	{
#ifdef FX_DEBUG_ARRAY_BOUNDS_CHECK
		AssertMsg(index < Capacity, "Access out of range");
#endif
		return pData[index];
	}

	TElementType& operator[](size_t index)
	{
		AssertMsg(index < Capacity, "Access out of range");
		return pData[index];
	}

	SizedArray& operator=(std::initializer_list<TElementType> list)
	{
		const size_t list_size = list.size();

		if (list_size > Capacity) {
			Free();
			InitCapacity(list.size());
		}

		Clear();

		for (const TElementType& obj : list) {
			Insert(obj);
		}

		return *this;
	}

	SizedArray& operator=(SizedArray&& other) noexcept
	{
		if (this != &other) {
			Free();
			TakeFrom(other);
		}

		return *this;
	}

	/** Copies `other`'s elements into a fresh allocation with the same capacity. */
	template <typename T, typename TOtherAllocator>
		requires C_IsSameOrConst<T, TElementType>
	void InitAsCopyOf(const SizedArray<T, TOtherAllocator>& other)
	{
		InitAsCopyOf(other.pData, other.Size, other.Capacity);
	}

	/** Copies `other`'s elements into a fresh allocation of at least `copy_capacity` elements. */
	template <typename T, typename TOtherAllocator>
		requires C_IsSameOrConst<T, TElementType>
	void InitAsCopyOf(const SizedArray<T, TOtherAllocator>& other, SizeType copy_capacity)
	{
		InitAsCopyOf(other.pData, other.Size, std::max(other.Capacity, copy_capacity));
	}

	/** Copies `size` elements from `ptr` into a fresh allocation of exactly `size` elements. */
	void InitAsCopyOf(const TElementType* ptr, SizeType size) { InitAsCopyOf(ptr, size, size); }

	void CloneFrom(const SizedArray& other) { InitAsCopyOf(other); }

	void Clear()
	{
		if constexpr (!std::is_trivially_destructible<TElementType>::value) {
			for (size_t i = 0; i < Size; i++) {
				TElementType& element = pData[i];
				element.~TElementType();
			}
		}

		Size = 0;
	}

	FX_FORCE_INLINE bool IsEmpty() const { return Size == 0; }
	FX_FORCE_INLINE bool IsNotEmpty() const { return !IsEmpty(); }

	TElementType& Insert(const TElementType& object)
	{
#ifdef FX_DEBUG_ARRAY_BOUNDS_CHECK
		AssertMsg(Size < Capacity, "Insert will exceed capacity!");
#endif

		TElementType* element = &pData[Size++];
		new (element) TElementType(object);

		return *element;
	}

	void Insert(TElementType&& object)
	{
#ifdef FX_DEBUG_ARRAY_BOUNDS_CHECK
		AssertMsg(Size < Capacity, "Insert will exceed capacity!");
#endif

		TElementType* element = &pData[Size++];
		new (element) TElementType(std::move(object));
	}

	/** Inserts a new empty element into the array and returns a pointer to the element */
	TElementType* Insert()
	{
#ifdef FX_DEBUG_ARRAY_BOUNDS_CHECK
		AssertMsg(Size < Capacity, "Insert will exceed capacity!");
#endif

		TElementType* element = &pData[Size++];

		new (element) TElementType;

		return element;
	}

	template <typename... Args>
	TElementType& Emplace(Args&&... args)
	{
#ifdef FX_DEBUG_ARRAY_BOUNDS_CHECK
		AssertMsg(Size < Capacity, "Insert will exceed capacity!");
#endif

		TElementType* element = &pData[Size++];
		new (element) TElementType(std::forward<Args>(args)...);

		return *element;
	}

	void RemoveLast()
	{
#ifdef FX_DEBUG_ARRAY_BOUNDS_CHECK
		AssertMsg(Size > 0, "No elements remaining!");
#endif

		--Size;
		TElementType* element = &pData[Size];

		if (std::is_destructible_v<TElementType>) {
			element->~TElementType();
		}
	}

	void InitCapacity(size_t element_count)
	{
		Free();

		InternalAllocateArray(element_count);

		Capacity = element_count;
	}

	void MarkFull() { Size = Capacity; }

	bool ContainsItem(TElementType* ptr) const
	{
		const TElementType* end_ptr = (pData + Size);

		if (ptr >= pData && ptr <= end_ptr) {
			return true;
		}

		return false;
	}

	int64 GetItemIndex(TElementType* ptr) const
	{
		const TElementType* end_ptr = (pData + Size);

		if (ptr < pData || ptr > end_ptr) {
			return scItemNotFound;
		}

		Assert(ptr >= pData);

		return ptr - pData;
	}

	/**
	 *   Initializes an array to contain `element_count` elements, which can be modified externally.
	 *
	 *   Example:
	 *   ```cpp
	 *      SizedArray<int32> int_array(10);
	 *      int_array.InitSize();
	 *
	 *      SizedArray<int32> other_array;
	 *      other_array.InitSize(15);
	 *   ```
	 */
	void InitSize(size_t element_count)
	{
		Clear();

		if (Capacity == 0) {
			InitCapacity(element_count);
		}

		Size = Capacity;

		for (uint64 i = 0; i < Size; i++) {
			new (&pData[i]) TElementType;
		}
	}

	/**
	 *   Initializes an array to contain `element_count` copies of `value`.
	 */
	void InitSize(size_t element_count, const TElementType& value)
	{
		Clear();

		if (Capacity == 0) {
			InitCapacity(element_count);
		}

		Size = Capacity;

		for (uint64 i = 0; i < Size; i++) {
			new (&pData[i]) TElementType(value);
		}
	}

	size_t GetSizeInBytes() const { return Size * sizeof(TElementType); }

	size_t GetCapacityInBytes() const { return Capacity * sizeof(TElementType); }

	void Print()
	{
		LogSeverityText<eLogSeverity::Info>();

		LogDirect("Array: {{ ");

		for (size_t i = 0; i < Size; i++) {
			LogDirect("{}", (*this)[i]);

			if (i < Size - 1) {
				LogDirect(", ");
			}
		}

		LogDirect(" }}\n");
	}

	FX_FORCE_INLINE bool IsInited() const { return (pData != nullptr && Capacity > 0); }


protected:
	void InitAsCopyOf(const TElementType* ptr, SizeType size, SizeType capacity)
	{
		static_assert(std::is_copy_constructible_v<TElementType>, "SizedArray element type is not copyable");
		AssertMsg(size <= capacity, "Copy size exceeds capacity");
		AssertMsg(ptr != nullptr || size == 0, "Copying from a null pointer");

		if (ptr != nullptr && pData != nullptr && !bDoNotDestroy) {
			AssertMsg(ptr + size <= pData || pData + Capacity <= ptr, "Copying a SizedArray into itself");
		}

		InitCapacity(capacity);

		// Lowers to memmove for trivially copyable types, copy-constructs each element otherwise.
		std::uninitialized_copy_n(ptr, size, pData);
		Size = size;
	}

	void TakeFrom(SizedArray& other)
	{
		pData = std::exchange(other.pData, nullptr);
		Size = std::exchange(other.Size, 0);
		Capacity = std::exchange(other.Capacity, 0);
		bDoNotDestroy = std::exchange(other.bDoNotDestroy, false);
	}

	void InternalAllocateArray(size_t element_count)
	{
#ifdef FX_SIZED_ARRAY_DEBUG
		LogDebug("Allocating SizedArray of capacity {:d} (type: {:s})", element_count, typeid(TElementType).name());
#endif
		if (element_count == 0) {
			return;
		}

		pData = static_cast<TElementType*>(TAllocator::AllocRaw(sizeof(TElementType) * element_count));

		if (pData == nullptr) {
			NoMemError();
		}

		Capacity = element_count;
	}

public:
	TElementType* pData = nullptr;
	SizeType Size = 0;
	SizeType Capacity = 0;

	bool bDoNotDestroy = false;
};

} // namespace fx
