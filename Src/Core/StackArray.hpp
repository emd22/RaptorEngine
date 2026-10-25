#pragma once

#include "Assert.hpp"
#include "Types.hpp"


namespace fx {


template <typename T, unsigned int TCapacity>
class StackArray
{
public:
	using Iterator = T*;
	using ConstIterator = const T*;


public:
	StackArray() = default;

	StackArray(const std::initializer_list<T>& list)
	{
		const size_t list_size = list.size();

		AssertMsg(Size < Capacity, "StackArray cannot be resized");

		for (const T& obj : list) {
			Insert(obj);
		}
	}

	StackArray<T, TCapacity>& operator=(const StackArray<T, TCapacity>& other)
	{
		Clear();

		for (uint32 i = 0; i < other.Size; i++) {
			Insert(other.pData[i]);
		}

		return *this;
	}

	T* Insert(const T& value)
	{
		AssertMsg(Size < Capacity, "StackArray insert out of range");

		pData[Size++] = value;
		return &pData[Size - 1];
	}

	T* Insert()
	{
		AssertMsg(Size < Capacity, "StackArray insert out of range");

		++Size;
		return &pData[Size - 1];
	}

	const T& operator[](size_t index) const
	{
#ifdef FX_DEBUG_ARRAY_BOUNDS_CHECK
		if (index >= Capacity) {
			Panic("StackArray", "Access out of range");
		}
#endif
		return pData[index];
	}

	T& operator[](size_t index)
	{
#ifdef FX_DEBUG_ARRAY_BOUNDS_CHECK
		if (index >= Capacity) {
			Panic("StackArray", "Access out of range");
		}
#endif
		return pData[index];
	}

	bool operator==(const StackArray<T, TCapacity>& other) const
	{
		if (Size != other.Size) {
			return false;
		}

		for (uint32 i = 0; i < Size; i++) {
			if (!(pData[i] == other.pData[i])) {
				return false;
			}
		}

		return true;
	}

	Iterator begin() { return pData; }
	Iterator end() { return pData + Size; }

	ConstIterator begin() const { return pData; }
	ConstIterator end() const { return pData + Size; }

	void MarkFull() { Size = Capacity; };
	void Clear() { Size = 0; }

	uint32 GetSizeInBytes() const { return Size * sizeof(T); }

	T& Last() { return pData[Size]; }

	operator T*() noexcept { return pData; }

	operator const T*() const noexcept { return pData; }

public:
	uint32 Size = 0;
	const uint32 Capacity = TCapacity;

	T pData[TCapacity];
};

} // namespace fx
