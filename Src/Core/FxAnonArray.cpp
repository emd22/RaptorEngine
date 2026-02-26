#include "FxAnonArray.hpp"

#include <Core/MemPool/FxMemPool.hpp>

void FxAnonArray::Create(uint32 object_size, uint32 size)
{
    ObjectSize = object_size;
    Capacity = size;

    pBuffer = FxMemPool::AllocRaw(object_size * size);
}

void FxAnonArray::Free()
{
    if (pBuffer == nullptr) {
        return;
    }

    FxMemPool::Free(pBuffer);

    Capacity = 0;
    Size = 0;
    ObjectSize = 0;
    pBuffer = nullptr;
}
