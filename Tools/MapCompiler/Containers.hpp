/*
 * File:        Containers.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: The engine's arrays as the map compiler uses them, and the few operations on them that it needs
 */

#pragma once

#include <Core/DynArray.hpp>
#include <Core/SizedArray.hpp>
#include <cstring>
#include <new>
#include <utility>

namespace fx::mapc {

/// A list that grows. It can be moved but not copied, so copies are made with CopyArray() or a Clone() function.
template <typename TElementType>
using Array = DynArray<TElementType, GrowthFunctions::Double>;

template <typename TElementType>
Array<TElementType> CopyArray(const Array<TElementType>& source)
{
	Array<TElementType> copy;

	if (source.Size > 0) {
		copy.SetPageSize(source.Size + 1);
	}

	for (const TElementType& element : source) {
		copy.Emplace(element);
	}

	return copy;
}

template <typename TElementType>
SizedArray<TElementType> CopyToSizedArray(const Array<TElementType>& source)
{
	SizedArray<TElementType> copy;
	copy.InitCapacity(source.Size);

	for (const TElementType& element : source) {
		copy.Insert(element);
	}

	return copy;
}

template <typename TElementType>
void RemoveAt(Array<TElementType>& array, uint32 index)
{
	for (uint32 i = index; i + 1 < array.Size; i++) {
		array[i] = std::move(array[i + 1]);
	}

	array.RemoveLast();
}

/**
 * @brief Empties the array and fills it with `count` copies of `value`
 */
template <typename TElementType>
void Fill(Array<TElementType>& array, uint32 count, const TElementType& value)
{
	array.Clear();

	for (uint32 i = 0; i < count; i++) {
		array.Emplace(value);
	}
}

template <typename TElementType>
TElementType& GetLast(Array<TElementType>& array)
{
	return array[array.Size - 1];
}

} // namespace fx::mapc
