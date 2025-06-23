#pragma once

#include <stddef.h>
#include <stdexcept>
#include <stdio.h>
#include <cstdlib>
#include <initializer_list>

#include "FxAllocator.hpp"

#include "FxMemPool.hpp"

// #define FX_STATIC_ARRAY_DEBUG 1
// #define FX_STATIC_ARRAY_NO_MEMPOOL 1

static inline void NoMemError()
{
    puts("FxSizedArray: out of memory");
    std::terminate();
}

template <
    typename ElementType,
    class Allocator = FxGlobalPoolAllocator<ElementType>
> requires C_IsAllocator< Allocator >

class FxSizedArray
{
public:
    struct Iterator
    {
        Iterator(ElementType* ptr, size_t index)
            : mPtr(ptr), mIndex(index)
        {
        }

        Iterator& operator++()
        {
            mIndex++;
            return *this;
        }

        Iterator& operator++(int value) {
            Iterator before = *this;
            mIndex++;
            return before;
        }

        ElementType& operator * () const
        {
            return mPtr[mIndex];
        }

        bool operator==(const Iterator& b) const
        {
            return mPtr == b.mPtr && mIndex == b.mIndex;
        }

    private:
        ElementType* mPtr;
        size_t mIndex;
    };

    FxSizedArray(ElementType* ptr, size_t size)
        : Data(ptr), Size(size), Capacity(size)
    {
    }

    FxSizedArray(size_t element_count)
        : Capacity(element_count)
    {
    #ifdef FX_STATIC_ARRAY_DEBUG
        Log::Debug("Allocating FxSizedArray of capacity %zu (type: %s)", Capacity, typeid(ElementType).name());
    #endif

    #if !defined(FX_STATIC_ARRAY_NO_MEMPOOL)
        Data = FxMemPool::Alloc<ElementType>(static_cast<uint32>(sizeof(ElementType)) * element_count);
        if (Data == nullptr) {
            NoMemError();
        }
    #else
        try {
            ElementType* allocated_ptr = new ElementType[element_count];
        } catch (const std::bad_alloc& e) {
            NoMemError();
        }

        Data = static_cast<ElementType*>(allocated_ptr);
    #endif

        Size = 0;

        // for (size_t i = 0; i < element_count; i++) {
        //     new(Data + i) ElementType;
        // }
    }

    FxSizedArray(std::initializer_list<ElementType> list)
    {
        InitCapacity(list.size());

        for (const ElementType& obj : list) {
            Insert(obj);
        }
    }

    FxSizedArray(FxSizedArray<ElementType>&& other)
    {
        Data = std::move(other.Data);
        other.Data = nullptr;

        Capacity = other.Capacity;
        Size = other.Size;

        other.Size = 0;
        other.Capacity = 0;
    }

    FxSizedArray() = default;

    ~FxSizedArray()
    {
        FxSizedArray::Free();
    }

    virtual void Free()
    {
        if (Data == nullptr) {
            return;
        }

    #if !defined(FX_STATIC_ARRAY_NO_MEMPOOL)
        for (size_t i = 0; i < Size; i++) {
            ElementType& element = Data[i];
            element.~ElementType();
        }

        FxMemPool::Free(static_cast<void*>(Data));
    #else
        delete[] Data;
    #endif


    #ifdef FX_STATIC_ARRAY_DEBUG
        Log::Debug("Freeing FxSizedArray of size %zu (type: %s)", Size, typeid(ElementType).name());
    #endif

        //
        // delete[] Data;
        //
        // std::free(Data);


        Data = nullptr;
        Capacity = 0;
        Size = 0;
    }

    Iterator begin()
    {
        return Iterator(Data, 0);
    }

    Iterator end()
    {
        return Iterator(Data, Size);
    }

    operator ElementType* () const
    {
        return Data;
    }

    operator ElementType& () const
    {
        return *Data;
    }

    const ElementType& operator[] (size_t index) const
    {
        if (index >= Size) {
            throw std::out_of_range("FxSizedArray access out of range");
        }
        return Data[index];
    }

    ElementType& operator[] (size_t index)
    {
        if (index >= Size) {
            throw std::out_of_range("FxSizedArray access out of range");
        }
        return Data[index];
    }

    FxSizedArray<ElementType>& operator = (std::initializer_list<ElementType>& list)
    {
        const size_t list_size = list.size();
        if (list_size > Capacity) {
            Free();
            InitCapacity(list.size());
        }

        Clear();

        for (const ElementType& obj : list) {
            Insert(obj);
        }

        return *this;
    }

    FxSizedArray<ElementType>& operator = (FxSizedArray<ElementType>&& other)
    {
        if (Data) {
            Free();
        }

        Data = other.Data;
        other.Data = nullptr;

        Size = other.Size;
        Capacity = other.Capacity;

        other.Size = 0;
        other.Capacity = 0;

        return *this;
    }

    void Clear() { Size = 0; }
    bool IsEmpty() { return Size == 0; }
    bool IsNotEmpty() { return !IsEmpty(); }

    void Insert(const ElementType& object)
    {
        if (Size > Capacity) {
            printf("New Size(%zu) > Capacity(%zu)!\n", Size, Capacity);
            throw std::out_of_range("FxSizedArray insert is larger than the capacity!");
        }

        ElementType* element = &Data[Size++];

    // #if !defined(FX_STATIC_ARRAY_NO_MEMPOOL)
        new (element) ElementType (object);
    // #else
    //     (void)element;
    // #endif
    }

    /** Inserts a new empty element into the array and returns a pointer to the element */
    ElementType* Insert()
    {
        if (Size > Capacity) {
            printf("New Size(%zu) > Capacity(%zu)!\n", Size, Capacity);
            throw std::out_of_range("FxSizedArray insert is larger than the capacity!");
        }

        ElementType* element = &Data[Size++];

        new (element) ElementType;

        return element;
    }

    void InitCapacity(size_t element_count)
    {
        if (Data != nullptr) {
            throw std::runtime_error("FxSizedArray has already been previously initialized, cannot InitCapacity!");
        }

        InternalAllocateArray(element_count);

        Capacity = element_count;
    }

    /**
     *   Initializes an array to contain `element_count` elements, which can be modified externally.
     *
     *   Example:
     *   ```cpp
     *      FxSizedArray<int32> int_array(10);
     *      int_array.InitSize();
     *
     *      FxSizedArray<int32> other_array;
     *      other_array.InitSize(15);
     *   ```
     */
    void InitSize(size_t element_count)
    {
        if (Capacity == 0) {
            InitCapacity(element_count);
        }

        Size = Capacity;

    #if !defined(FX_STATIC_ARRAY_NO_MEMPOOL)
        for (uint64 i = 0; i < Size; i++) {
            new (&Data[i]) ElementType;
        }
    #endif
    }

    // void InitSize()
    // {
    //     if (Capacity == 0) {
    //         throw std::runtime_error("Static array capacity has not been set!");
    //     }

    //     Size = Capacity;
    // }

    size_t GetSizeInBytes() const
    {
        return Size * sizeof(ElementType);
    }

    size_t GetCapacityInBytes() const
    {
        return Capacity * sizeof(ElementType);
    }

protected:
    virtual void InternalAllocateArray(size_t element_count)
    {
    #ifdef FX_DEBUG_STATIC_ARRAY
        Log::Debug("Allocating FxSizedArray of capacity %zu (type: %s)", element_count, typeid(ElementType).name());
    #endif
    #if !defined(FX_STATIC_ARRAY_NO_MEMPOOL)
        Data = FxMemPool::Alloc<ElementType>(sizeof(ElementType) * element_count);
        if (Data == nullptr) {
            NoMemError();
        }
    #else
        try {
            Data = new ElementType[element_count];
        }
        catch (const std::bad_alloc& e) {
            NoMemError();
        }

    #endif

        // Data = new ElementType[element_count];

        Capacity = element_count;
    }

public:
    size_t Size = 0;
    size_t Capacity = 0;

    ElementType* Data = nullptr;
};
