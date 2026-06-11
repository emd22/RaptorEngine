#pragma once

#include <Core/Assert.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Slice.hpp>
#include <Core/Types.hpp>
#include <cstring>

namespace fx {


class AnonArray
{
public:
    AnonArray() = default;

    AnonArray(uint32 object_size, uint32 size) { Create(object_size, size); }
    AnonArray(AnonArray&& other);

    AnonArray& operator=(AnonArray&& other) noexcept;

    template <typename T>
    AnonArray(SizedArray<T>&& other)
    {
        (*this) = std::move(other);
    }

    AnonArray(const AnonArray& other) = delete;

    template <typename T>
    AnonArray& operator=(SizedArray<T>&& other) noexcept
    {
        pData = other.pData;
        Size = other.Size;
        Capacity = other.Capacity;
        ObjectSize = sizeof(T);

        other.pData = nullptr;
        other.Size = 0;
        other.Capacity = 0;

        return *this;
    }

    AnonArray& operator=(const AnonArray& other) = delete;

    void Create(uint32 object_size, uint32 size);

    template <typename T>
    void InitAsCopyOf(const SizedArray<T>& data)
    {
        Assert(pData == nullptr);

        Create(sizeof(T), data.Capacity);
        memcpy(pData, data.pData, data.GetSizeInBytes());
        Size = data.Size;
    }

    template <typename T>
    void Insert(const T& object)
    {
        Assert(sizeof(T) == ObjectSize);
        memcpy(&reinterpret_cast<T*>(pData)[Size], &object, sizeof(T));
        ++Size;
    }

    void InsertRaw(const void* value);

    FX_FORCE_INLINE bool IsEmpty() const { return pData == nullptr || Size == 0; }
    FX_FORCE_INLINE bool IsNotEmpty() const { return !IsEmpty(); }

    template <typename T>
    const T& Get(uint32 index) const
    {
        Assert(sizeof(T) == ObjectSize);
        Assert(index < Size);
        return reinterpret_cast<T*>(pData)[index];
    }

    template <typename T>
    T& Get(uint32 index)
    {
        Assert(sizeof(T) == ObjectSize);
        Assert(index < Size);
        return reinterpret_cast<T*>(pData)[index];
    }

    void* GetRaw(uint32 index);
    const void* GetRaw(uint32 index) const;

    template <typename T>
    T* GetPtr(uint32 index)
    {
        Assert(sizeof(T) == ObjectSize);
        Assert(index < Size);

        return reinterpret_cast<T*>(pData) + index;
    }

    void Free();

    ~AnonArray() { Free(); }

public:
    void* pData = nullptr;

    /// The size of each object to store
    uint32 ObjectSize = 0;

    uint32 Size = 0;
    uint32 Capacity = 0;
};

} // namespace fx
