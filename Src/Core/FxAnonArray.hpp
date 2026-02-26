#pragma once

#include <Core/FxPanic.hpp>
#include <Core/FxSlice.hpp>
#include <Core/FxTypes.hpp>
#include <cstring>

class FxAnonArray
{
public:
    FxAnonArray() = default;

    FxAnonArray(uint32 object_size, uint32 size) { Create(object_size, size); }

    void Create(uint32 object_size, uint32 size);

    template <typename T>
    void InitAsCopyOf(const FxSlice<T>& data)
    {
        FxAssert(pBuffer == nullptr);

        Create(sizeof(T), data.Size);
        memcpy(pBuffer, data.pData);
    }

    template <typename T>
    void Insert(const T& object)
    {
        FxAssert(sizeof(T) == ObjectSize);
        memcpy(&reinterpret_cast<T*>(pBuffer)[Size++], &object, sizeof(T));
    }

    FX_FORCE_INLINE bool IsEmpty() const { return Size == 0; }
    FX_FORCE_INLINE bool IsNotEmpty() const { return !IsEmpty(); }


    template <typename T>
    T& Get(uint32 index)
    {
        FxAssert(sizeof(T) == ObjectSize);
        FxAssert(index < Size);
        return *(reinterpret_cast<T*>(pBuffer)[index]);
    }

    void Free();

    ~FxAnonArray() { Free(); }

public:
    void* pBuffer = nullptr;

    /// The size of each object to store
    uint32 ObjectSize = 0;

    uint32 Size = 0;
    uint32 Capacity = 0;
};
