#pragma once

#include <Core/Types.hpp>
#include <cstdlib>

namespace fx {

/**
 * @brief Raw memcpy with safeguards.
 */
template <typename T>
void MemoryCopyRaw(T* dst, const T* src, uint64 size)
{
	static_assert(std::is_trivially_copyable<T>());
	std::memcpy(static_cast<void*>(dst), static_cast<const void*>(src), size);
}


/**
 * @brief Raw memset with safeguards.
 */
template <typename T>
void MemorySetRaw(T* dst, int c, uint64 size)
{
	static_assert(std::is_trivially_copyable<T>());
	std::memset(static_cast<void*>(dst), c, size);
}

} // namespace fx
