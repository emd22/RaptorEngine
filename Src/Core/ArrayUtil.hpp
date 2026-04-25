#pragma once

#include <Core/PagedArray.hpp>
#include <Core/SizedArray.hpp>

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

} // namespace fx::ArrayUtil
