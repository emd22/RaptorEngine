#pragma once

// #include "Allocator.hpp"
#include "Defines.hpp"
#include "Panic.hpp"

#include <stddef.h>
#include <stdio.h>

#include <Core/Allocator.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Engine.hpp>
#include <cstdlib>
#include <initializer_list>

#ifdef FX_SIZED_ARRAY_DEBUG
#include "Log.hpp"
#endif

namespace fx {

#define FX_SIZED_ARRAY_NO_MEMPOOL 1

static inline void NoMemError()
{
    puts("SizedArray: out of memory");
    Terminate();
}

template <typename TElementType, typename TAllocator = StdAllocator>
    requires C_IsAllocator<TAllocator>
class SizedArray
{
public:
    static constexpr int64 scItemNotFound = -1;

    using SizeType = size_t;

public:
    struct Iterator
    {
        Iterator(TElementType* ptr, size_t index) : mPtr(ptr), mIndex(index) {}

        Iterator& operator++()
        {
            mIndex++;
            return *this;
        }

        Iterator& operator++(int value)
        {
            Iterator before = *this;
            mIndex++;
            return before;
        }

        TElementType& operator*() const { return mPtr[mIndex]; }

        bool operator==(const Iterator& b) const { return mPtr == b.mPtr && mIndex == b.mIndex; }


    private:
        TElementType* mPtr;
        size_t mIndex;
    };


public:
    static SizedArray<TElementType> CreateCopyOf(const TElementType* ptr, SizeType size)
    {
        SizedArray<TElementType> arr(size);
        arr.MarkFull();

        memcpy(arr.pData, ptr, arr.GetSizeInBytes());

        return std::move(arr);
    }

    static SizedArray<TElementType> CreateAsSize(SizeType size)
    {
        SizedArray<TElementType> arr;
        arr.InitSize(size);

        return std::move(arr);
    }

    static SizedArray<TElementType> CreateEmpty() { return SizedArray<TElementType>(nullptr, 0); }

public:
    SizedArray(TElementType* ptr, size_t size) : pData(ptr), Size(size), Capacity(size) {}

    SizedArray(size_t element_count) : Capacity(element_count)
    {
#ifdef FX_SIZED_ARRAY_DEBUG
        LogDebug("Allocating SizedArray of capacity {:d} (type: {:s})", Capacity, typeid(TElementType).name());
#endif
        InternalAllocateArray(element_count);
        Size = 0;
    }

    SizedArray(std::initializer_list<TElementType> list)
    {
        if (list.size() == 0) {
            return;
        }

        InitCapacity(list.size());

        for (const TElementType& obj : list) {
            Insert(obj);
        }
    };

    SizedArray(SizedArray<TElementType>&& other)
    {
        pData = std::move(other.pData);
        other.pData = nullptr;

        Capacity = other.Capacity;
        Size = other.Size;

        other.Size = 0;
        other.Capacity = 0;
    }

    SizedArray() = default;

    ~SizedArray() { Free(); }


    void Free()
    {
        if (pData == nullptr || bDoNotDestroy) {
            return;
        }

        for (size_t i = 0; i < Size; i++) {
            TElementType& element = pData[i];
            element.~TElementType();
        }

#if !defined(FX_SIZED_ARRAY_NO_MEMPOOL)
        if (gEnginePool) {
            gEnginePool->FreeRaw(static_cast<void*>(pData));
        }
#else
        std::free(reinterpret_cast<void*>(pData));
#endif

#ifdef FX_SIZED_ARRAY_DEBUG
        LogDebug("Freeing SizedArray of size {:d} (type: {:s})", Size, typeid(TElementType).name());
#endif

        pData = nullptr;
        Capacity = 0;
        Size = 0;
    }

    Iterator begin() const { return Iterator(pData, 0); }

    Iterator end() const { return Iterator(pData, Size); }

    operator TElementType*() const { return pData; }

    operator TElementType&() const { return *pData; }

    const TElementType& operator[](size_t index) const
    {
        AssertMsg(index < Capacity, "Access out of range");
        return pData[index];
    }

    TElementType& operator[](size_t index)
    {
        AssertMsg(index < Capacity, "Access out of range");
        return pData[index];
    }

    SizedArray<TElementType>& operator=(std::initializer_list<TElementType>& list)
    {
        const size_t list_size = list.size();
        if (list_size > Capacity) {
            Free();
            InitCapacity(list.size());
        }

        Clear();

        for (const TElementType& obj : list) {
            Insert(obj);
        }

        return *this;
    }

    SizedArray<TElementType>& operator=(SizedArray<TElementType>&& other)
    {
        if (pData) {
            Free();
        }

        pData = other.pData;
        other.pData = nullptr;

        Size = other.Size;
        Capacity = other.Capacity;

        other.Size = 0;
        other.Capacity = 0;

        return *this;
    }

    template <typename T>
        requires C_IsSameOrConst<T, TElementType>
    void InitAsCopyOf(const SizedArray<T>& other)
    {
        InitCapacity(other.Capacity);
        Size = other.Size;

        if (std::is_copy_constructible_v<TElementType>) {
            for (SizeType i = 0; i < Size; i++) {
                new (other.pData + i) TElementType(other.pData[i]);
            }
        }
        else {
            std::memcpy(reinterpret_cast<void*>(pData), reinterpret_cast<const void*>(other.pData),
                        other.GetSizeInBytes());
        }
    }

    template <typename T>
        requires std::is_same_v<typename std::remove_const<T>::type, TElementType>
    void InitAsCopyOf(const SizedArray<T>& other, SizeType copy_capacity)
    {
        InitCapacity(max(other.Capacity, copy_capacity));
        Size = other.Size;

        memcpy(pData, other.pData, other.GetSizeInBytes());
    }

    void InitAsCopyOf(const TElementType* ptr, SizeType size)
    {
        InitSize(size);
        memcpy(pData, ptr, GetSizeInBytes());
    }

    void CloneFrom(const SizedArray<TElementType>& other)
    {
        InitCapacity(other.Capacity);
        Size = other.Size;
        memcpy(pData, other.pData, other.GetSizeInBytes());
    }

    void Clear()
    {
        for (size_t i = 0; i < Size; i++) {
            TElementType& element = pData[i];
            element.~TElementType();
        }

        Size = 0;
    }

    FX_FORCE_INLINE bool IsEmpty() const { return Size == 0; }
    FX_FORCE_INLINE bool IsNotEmpty() const { return !IsEmpty(); }

    TElementType& Insert(const TElementType& object)
    {
        AssertMsg(Size < Capacity, "Insert will exceed capacity!");

        TElementType* element = &pData[Size++];
        new (element) TElementType(object);

        return *element;
    }

    void Insert(TElementType&& object)
    {
        AssertMsg(Size < Capacity, "Insert will exceed capacity!");

        TElementType* element = &pData[Size++];

        new (element) TElementType(std::move(object));
    }

    /** Inserts a new empty element into the array and returns a pointer to the element */
    TElementType* Insert()
    {
        AssertMsg(Size < Capacity, "Insert will exceed capacity!");

        TElementType* element = &pData[Size++];

        new (element) TElementType;

        return element;
    }

    void RemoveLast()
    {
        AssertMsg(Size > 0, "No elements remaining!");
        TElementType* element = &pData[Size--];

        if (std::is_destructible_v<TElementType>) {
            element->~TElementType();
        }
    }

    void InitCapacity(size_t element_count)
    {
        AssertMsg(pData == nullptr, "SizedArray has already been initialized!");

        InternalAllocateArray(element_count);

        Capacity = element_count;
    }

    void MarkFull() { Size = Capacity; }

    bool ContainsItem(TElementType* ptr) const
    {
        const TElementType* end_ptr = (pData + Size);

        if (ptr >= pData && ptr <= end_ptr) {
            return true;
        }

        return false;
    }

    int64 GetItemIndex(TElementType* ptr) const
    {
        const TElementType* end_ptr = (pData + Size);

        if (ptr < pData || ptr > end_ptr) {
            return scItemNotFound;
        }

        Assert(ptr >= pData);

        return ptr - pData;
    }

    /**
     *   Initializes an array to contain `element_count` elements, which can be modified externally.
     *
     *   Example:
     *   ```cpp
     *      SizedArray<int32> int_array(10);
     *      int_array.InitSize();
     *
     *      SizedArray<int32> other_array;
     *      other_array.InitSize(15);
     *   ```
     */
    void InitSize(size_t element_count)
    {
        if (Capacity == 0) {
            InitCapacity(element_count);
        }

        Size = Capacity;

        for (uint64 i = 0; i < Size; i++) {
            new (&pData[i]) TElementType;
        }
    }

    size_t GetSizeInBytes() const { return Size * sizeof(TElementType); }

    size_t GetCapacityInBytes() const { return Capacity * sizeof(TElementType); }

    void Print()
    {
        LogSeverityText<eLogSeverity::Info>();

        LogDirect("Array: {{ ");

        for (size_t i = 0; i < Size; i++) {
            LogDirect("{}", (*this)[i]);

            if (i < Size - 1) {
                LogDirect(", ");
            }
        }

        LogDirect(" }}\n");
    }

    FX_FORCE_INLINE bool IsInited() const { return (pData != nullptr && Capacity > 0); }


protected:
    void InternalAllocateArray(size_t element_count)
    {
#ifdef FX_SIZED_ARRAY_DEBUG
        LogDebug("Allocating SizedArray of capacity {:d} (type: {:s})", element_count, typeid(TElementType).name());
#endif
        if (element_count == 0) {
            return;
        }

#if !defined(FX_SIZED_ARRAY_NO_MEMPOOL)
        pData = static_cast<TElementType*>(gEnginePool->AllocRaw(sizeof(TElementType) * element_count));
#else
        pData = reinterpret_cast<TElementType*>(std::malloc(sizeof(TElementType) * element_count));
#endif

        if (pData == nullptr) {
            NoMemError();
        }

        Capacity = element_count;
    }

public:
    TElementType* pData = nullptr;
    SizeType Size = 0;
    SizeType Capacity = 0;


    bool bDoNotDestroy = false;
};

} // namespace fx
