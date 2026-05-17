#pragma once

#include <Core/Panic.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Slice.hpp>
#include <Core/Types.hpp>
#include <cstring>

namespace fx {


class ByteBuffer
{
public:
    static ByteBuffer FromData(uint8* data, uint32 size)
    {
        ByteBuffer bb;
        bb.pData = data;
        bb.Capacity = size;
        bb.Offset = 0;
        bb.bIsExternalPtr = true;
        return std::move(bb);
    }

public:
    ByteBuffer() = default;

    ByteBuffer(uint32 size) { Create(size); }
    ByteBuffer(ByteBuffer&& other);

    ByteBuffer& operator=(ByteBuffer&& other) noexcept;

    template <typename T>
    ByteBuffer(SizedArray<T>&& other)
    {
        (*this) = std::move(other);
    }

    ByteBuffer(const ByteBuffer& other) = delete;

    template <typename T>
    ByteBuffer& operator=(SizedArray<T>&& other) noexcept
    {
        pData = other.pData;
        Offset = other.Offset;
        Capacity = other.Capacity;

        other.pData = nullptr;
        other.Offset = 0;
        other.Capacity = 0;

        return *this;
    }

    ByteBuffer& operator=(const ByteBuffer& other) = delete;

    void Create(uint32 size);

    template <typename T>
    void InitAsCopyOf(const SizedArray<T>& data)
    {
        Assert(pData == nullptr);

        Create(sizeof(T), data.Capacity);
        memcpy(pData, data.pData, data.GetSizeInBytes());
        Offset = data.Offset;
    }

    template <typename T>
    void Insert(const T& object)
    {
        memcpy(reinterpret_cast<uint8*>(pData) + Offset, &object, sizeof(T));
        Offset += sizeof(T);
    }

    void InsertRaw(const void* value, uint32 value_size);

    FX_FORCE_INLINE bool IsEmpty() const { return pData == nullptr || Offset == 0; }
    FX_FORCE_INLINE bool IsNotEmpty() const { return !IsEmpty(); }

    FX_FORCE_INLINE uint32 GetSize() const { return Offset; }
    FX_FORCE_INLINE uint32 GetRemainingSize() const { return Capacity - Offset; }

    FX_FORCE_INLINE void Rewind() { Offset = 0; }


    template <typename T>
    const T& Get(uint32 index) const
    {
        Assert(index < Capacity);
        return *reinterpret_cast<const T*>(reinterpret_cast<uint8*>(pData) + index);
    }

    /**
     * @brief Returns the value at the current `Offset` of the byte buffer and increments the offset by the size of T.
     */
    template <typename T>
    const T& Get()
    {
        const T& value = Get<T>(Offset);
        Offset += sizeof(T);
        return value;
    }

    template <typename T>
    T* GetPtr()
    {
        return GetPtr<T>(Offset);
    }

    template <typename T>
    T& Get(uint32 index)
    {
        Assert(index < Capacity);
        return *reinterpret_cast<T*>(reinterpret_cast<uint8*>(pData) + index);
    }


    void* GetRaw(uint32 index);
    const void* GetRaw(uint32 index) const;

    template <typename T>
    T* GetPtr(uint32 index)
    {
        Assert(index < Capacity);
        return reinterpret_cast<T*>(reinterpret_cast<uint8*>(pData) + index);
    }

    void Free();

    ~ByteBuffer() { Free(); }

public:
    void* pData = nullptr;

    bool bIsExternalPtr = false;

    uint32 Offset = 0;
    uint32 Capacity = 0;
};

} // namespace fx
