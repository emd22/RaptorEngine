#include "AnonArray.hpp"

#include <Core/MemPool/MemPool.hpp>
#include <Engine.hpp>

namespace fx {


void AnonArray::Create(uint32 object_size, uint32 size)
{
    ObjectSize = object_size;
    Capacity = size;
    Size = 0;

    // pData = gEnginePool->AllocRaw(object_size * size);
    pData = malloc(object_size * size);
}

AnonArray::AnonArray(AnonArray&& other) { (*this) = std::move(other); }

AnonArray& AnonArray::operator=(AnonArray&& other) noexcept
{
    pData = other.pData;
    Size = other.Size;
    Capacity = other.Capacity;
    ObjectSize = other.ObjectSize;

    other.pData = nullptr;
    other.Size = 0;
    other.Capacity = 0;
    other.ObjectSize = 0;

    return *this;
}

void AnonArray::Free()
{
    if (pData == nullptr) {
        return;
    }

    // MemPool::Free(pData);
    // gEnginePool->FreeRaw(pData);
    free(pData);

    Capacity = 0;
    Size = 0;
    ObjectSize = 0;
    pData = nullptr;
}

void AnonArray::InsertRaw(const void* value)
{
    memcpy(reinterpret_cast<uint8*>(pData) + (ObjectSize * Size), value, ObjectSize);
    ++Size;
}

const void* AnonArray::GetRaw(uint32 index) const
{
    Assert(index < Size);
    Assert(ObjectSize > 0);

    return reinterpret_cast<void*>(reinterpret_cast<uint8*>(pData) + (index * ObjectSize));
}

void* AnonArray::GetRaw(uint32 index)
{
    Assert(index < Size);
    Assert(ObjectSize > 0);

    return reinterpret_cast<void*>(reinterpret_cast<uint8*>(pData) + (index * ObjectSize));
}

} // namespace fx
