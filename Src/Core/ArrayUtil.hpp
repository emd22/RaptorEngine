#pragma once

#include <Core/PagedArray.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Slice.hpp>
#include <cstdlib>

namespace fx::ArrayUtil {

template <typename T>
SizedArray<T> LinearizePagedArray(PagedArray<T>& src)
{
    SizedArray<T> dst;
    dst.InitSize(src.Size());

    auto* page = src.pFirstPage;

    uint64 offset = 0;

    while (page != nullptr) {
        memcpy(dst.pData + offset, page->pData, page->Size);

        offset += page->Size;

        // Continue to next page
        page = page->pNext;
    }

    return dst;
}

template <typename T>
Slice<T> SliceDupe(const Slice<T>& value)
{
    const uint32 total_size = value.GetSizeInBytes();
    Slice<T> copy { reinterpret_cast<T*>(malloc(total_size)), value.Size };

    memcpy(const_cast<void*>(reinterpret_cast<const void*>(copy.pData)), reinterpret_cast<const void*>(value.pData),
           total_size);

    return copy;
}

} // namespace fx::ArrayUtil
