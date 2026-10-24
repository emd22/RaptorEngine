#include "ByteBuffer.hpp"

namespace fx {


void ByteBuffer::Create(uint32 size)
{
    Free();

    if (size == 0) {
        return;
    }

    pData = StdAllocator::AllocRaw(size);

    if (pData == nullptr) {
        Panic("ByteBuffer", "Out of memory allocating {} bytes", size);
    }

    Capacity = size;
}

ByteBuffer::ByteBuffer(ByteBuffer&& other) noexcept { (*this) = std::move(other); }

ByteBuffer& ByteBuffer::operator=(ByteBuffer&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    Free();

    pData = std::exchange(other.pData, nullptr);
    Size = std::exchange(other.Size, 0);
    Capacity = std::exchange(other.Capacity, 0);
    ReadOffset = std::exchange(other.ReadOffset, 0);
    bIsExternalPtr = std::exchange(other.bIsExternalPtr, false);
    bIsReadOnly = std::exchange(other.bIsReadOnly, false);

    return *this;
}

void ByteBuffer::Free()
{
    if (pData != nullptr && !bIsExternalPtr) {
        StdAllocator::FreeRaw(pData);
    }

    pData = nullptr;
    Size = 0;
    Capacity = 0;
    ReadOffset = 0;
    bIsExternalPtr = false;
    bIsReadOnly = false;
}

void ByteBuffer::InsertRaw(const void* value, uint32 value_size)
{
    if (value_size == 0) {
        return;
    }

    Assert(value != nullptr);
    Assert(!bIsReadOnly);
    Assert(value_size <= Capacity - Size);

    memcpy(static_cast<uint8*>(pData) + Size, value, value_size);
    Size += value_size;
}

const void* ByteBuffer::GetRaw(uint32 index) const
{
    Assert(index <= Size);

    return static_cast<const uint8*>(pData) + index;
}

void* ByteBuffer::GetRaw(uint32 index)
{
    Assert(index <= Size);

    return static_cast<uint8*>(pData) + index;
}

} // namespace fx
