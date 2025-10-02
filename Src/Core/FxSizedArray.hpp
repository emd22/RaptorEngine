#pragma once

#include "FxAllocator.hpp"
#include "FxDefines.hpp"
#include "FxLog.hpp"
#include "FxMemory.hpp"

#include <stddef.h>
#include <stdio.h>

#include <cstdlib>
#include <initializer_list>
#include <stdexcept>

static inline void NoMemError()
{
    puts("FxSizedArray: out of memory");
    std::terminate();
}

template <typename TElementType>
class FxSizedArray
{
public:
    static constexpr int64 scItemNotFound = -1;

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

    FxSizedArray(TElementType* ptr, size_t size) : Data(ptr), Size(size), Capacity(size) {}

    FxSizedArray(size_t element_count) : Capacity(element_count)
    {
#ifdef FX_SIZED_ARRAY_DEBUG
        FxLogDebug("Allocating FxSizedArray of capacity {:d} (type: {:s})", Capacity, typeid(ElementType).name());
#endif

#if !defined(FX_SIZED_ARRAY_NO_MEMPOOL)
        Data = FxMemPool::Alloc<TElementType>(static_cast<uint32>(sizeof(TElementType)) * element_count);
        if (Data == nullptr) {
            NoMemError();
        }
#else
        try {
            ElementType* allocated_ptr = new ElementType[element_count];
        }
        catch (const std::bad_alloc& e) {
            NoMemError();
        }

        Data = static_cast<ElementType*>(allocated_ptr);
#endif

        Size = 0;

        // for (size_t i = 0; i < element_count; i++) {
        //     new(Data + i) ElementType;
        // }
    }

    FxSizedArray(std::initializer_list<TElementType> list)
    {
        InitCapacity(list.size());

        for (const TElementType& obj : list) {
            Insert(obj);
        }
    }

    FxSizedArray(FxSizedArray<TElementType>&& other)
    {
        Data = std::move(other.Data);
        other.Data = nullptr;

        Capacity = other.Capacity;
        Size = other.Size;

        other.Size = 0;
        other.Capacity = 0;
    }

    FxSizedArray() = default;

    ~FxSizedArray() { FxSizedArray::Free(); }

    virtual void Free()
    {
        if (Data == nullptr || DoNotDestroy) {
            return;
        }

#if !defined(FX_SIZED_ARRAY_NO_MEMPOOL)
        for (size_t i = 0; i < Size; i++) {
            TElementType& element = Data[i];
            element.~TElementType();
        }

        FxMemPool::Free(static_cast<void*>(Data));
#else
        delete[] Data;
#endif


#ifdef FX_SIZED_ARRAY_DEBUG
        FxLogDebug("Freeing FxSizedArray of size {:d} (type: {:s})", Size, typeid(ElementType).name());
#endif

        //
        // delete[] Data;
        //
        // std::free(Data);


        Data = nullptr;
        Capacity = 0;
        Size = 0;
    }

    Iterator begin() { return Iterator(Data, 0); }

    Iterator end() { return Iterator(Data, Size); }

    operator TElementType*() const { return Data; }

    operator TElementType&() const { return *Data; }

    const TElementType& operator[](size_t index) const
    {
        if (index >= Size) {
            throw std::out_of_range("FxSizedArray access out of range");
        }
        return Data[index];
    }

    TElementType& operator[](size_t index)
    {
        if (index >= Size) {
            throw std::out_of_range("FxSizedArray access out of range");
        }
        return Data[index];
    }

    FxSizedArray<TElementType>& operator=(std::initializer_list<TElementType>& list)
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

    FxSizedArray<TElementType>& operator=(FxSizedArray<TElementType>&& other)
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

    void InitAsCopyOf(const FxSizedArray<TElementType>& other)
    {
        InitCapacity(other.Capacity);
        Size = other.Size;

        memcpy(Data, other.Data, other.GetSizeInBytes());
    }

    void Clear() { Size = 0; }

    inline bool IsEmpty() const { return Size == 0; }
    inline bool IsNotEmpty() const { return !IsEmpty(); }

    void Insert(const TElementType& object)
    {
        if (Size > Capacity) {
            printf("New Size(%zu) > Capacity(%zu)!\n", Size, Capacity);
            throw std::out_of_range("FxSizedArray insert is larger than the capacity!");
        }

        TElementType* element = &Data[Size++];

        new (element) TElementType(object);
    }

    void Insert(TElementType&& object)
    {
        if (Size > Capacity) {
            printf("New Size(%zu) > Capacity(%zu)!\n", Size, Capacity);
            throw std::out_of_range("FxSizedArray insert is larger than the capacity!");
        }

        TElementType* element = &Data[Size++];

        new (element) TElementType(std::move(object));
    }

    /** Inserts a new empty element into the array and returns a pointer to the element */
    TElementType* Insert()
    {
        if (Size > Capacity) {
            printf("New Size(%zu) > Capacity(%zu)!\n", Size, Capacity);
            throw std::out_of_range("FxSizedArray insert is larger than the capacity!");
        }

        TElementType* element = &Data[Size++];

        new (element) TElementType;

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

    bool ContainsItem(TElementType* ptr) const
    {
        const TElementType* end_ptr = (Data + Size);

        if (ptr >= Data && ptr <= end_ptr) {
            return true;
        }

        return false;
    }

    int64 GetItemIndex(TElementType* ptr) const
    {
        const TElementType* end_ptr = (Data + Size);

        if (ptr < Data || ptr > end_ptr) {
            return scItemNotFound;
        }

        FxAssert(ptr >= Data);

        return ptr - Data;
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

#if !defined(FX_SIZED_ARRAY_NO_MEMPOOL)
        for (uint64 i = 0; i < Size; i++) {
            new (&Data[i]) TElementType;
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

    size_t GetSizeInBytes() const { return Size * sizeof(TElementType); }

    size_t GetCapacityInBytes() const { return Capacity * sizeof(TElementType); }

    void Print()
    {
        FxLogChannelText<FxLogChannel::Info>();

        FxLogDirect("Array: {{ ");

        for (size_t i = 0; i < Size; i++) {
            FxLogDirect("{}", (*this)[i]);

            if (i < Size - 1) {
                FxLogDirect(", ");
            }
        }

        FxLogDirect(" }}\n");
    }


protected:
    virtual void InternalAllocateArray(size_t element_count)
    {
#ifdef FX_SIZED_ARRAY_DEBUG
        FxLogDebug("Allocating FxSizedArray of capacity {:d} (type: {:s})", element_count, typeid(ElementType).name());
#endif
#if !defined(FX_SIZED_ARRAY_NO_MEMPOOL)
        Data = FxMemPool::Alloc<TElementType>(sizeof(TElementType) * element_count);
        if (Data == nullptr) {
            NoMemError();
        }
#else
        try {
            Data = new TElementType[element_count];
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

    TElementType* Data = nullptr;

    bool DoNotDestroy = false;
};
