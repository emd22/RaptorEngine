#include "FxAnonArray.hpp"

#include <Core/MemPool/FxMemPool.hpp>

void FxAnonArray::Create(uint32 object_size, uint32 size)
{
    ObjectSize = object_size;
    Capacity = size;

    pData = FxMemPool::AllocRaw(object_size * size);
}

FxAnonArray::FxAnonArray(FxAnonArray&& other) { (*this) = std::move(other); }

FxAnonArray& FxAnonArray::operator=(FxAnonArray&& other) noexcept
{
    pData = other.pData;
    Size = other.Size;
    Capacity = other.Capacity;
    ObjectSize = other.ObjectSize;

    other.pData = nullptr;
    other.Size = 0;
    other.Capacity = 0;

    return *this;
}

void FxAnonArray::Free()
{
    if (pData == nullptr) {
        return;
    }

    FxMemPool::Free(pData);

    Capacity = 0;
    Size = 0;
    ObjectSize = 0;
    pData = nullptr;
}
