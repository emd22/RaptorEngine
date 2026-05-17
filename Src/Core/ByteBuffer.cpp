#include "ByteBuffer.hpp"

#include <Core/MemPool/MemPool.hpp>
#include <Engine.hpp>

namespace fx {


void ByteBuffer::Create(uint32 size)
{
    Capacity = size;
    Offset = 0;

    pData = malloc(size);
}

ByteBuffer::ByteBuffer(ByteBuffer&& other) { (*this) = std::move(other); }

ByteBuffer& ByteBuffer::operator=(ByteBuffer&& other) noexcept
{
    pData = other.pData;
    Offset = other.Offset;
    bIsExternalPtr = other.bIsExternalPtr;
    Capacity = other.Capacity;

    other.pData = nullptr;
    other.bIsExternalPtr = false;
    other.Offset = 0;
    other.Capacity = 0;

    return *this;
}

void ByteBuffer::Free()
{
    if (pData && !bIsExternalPtr) {
        free(pData);
    }

    Capacity = 0;
    pData = nullptr;
}

void ByteBuffer::InsertRaw(const void* value, uint32 value_size)
{
    if (value == nullptr || value_size == 0) {
        return;
    }

    memcpy(reinterpret_cast<uint8*>(pData) + Offset, value, value_size);
    Offset += value_size;
}

const void* ByteBuffer::GetRaw(uint32 index) const
{
    Assert(index < Offset);

    return reinterpret_cast<void*>(reinterpret_cast<uint8*>(pData) + (index));
}

void* ByteBuffer::GetRaw(uint32 index)
{
    Assert(index < Offset);

    return reinterpret_cast<void*>(reinterpret_cast<uint8*>(pData) + (index));
}

} // namespace fx
