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
    static FxSizedArray<TElementType> CreateCopyOf(const TElementType* ptr, SizeType size)
    {
        FxSizedArray<TElementType> arr(size);
        arr.MarkFull();

        memcpy(arr.pData, ptr, arr.GetSizeInBytes());

        return std::move(arr);
    }

    static FxSizedArray<TElementType> CreateAsSize(SizeType size)
    {
        FxSizedArray<TElementType> arr;
        arr.InitSize(size);

        return std::move(arr);
    }

    static FxSizedArray<TElementType> CreateEmpty() { return FxSizedArray<TElementType>(nullptr, 0); }

public:
    FxSizedArray(TElementType* ptr, size_t size) : pData(ptr), Size(size), Capacity(size) {}

    FxSizedArray(size_t element_count) : Capacity(element_count)
    {
#ifdef FX_SIZED_ARRAY_DEBUG
        FxLogDebug("Allocating FxSizedArray of capacity {:d} (type: {:s})", Capacity, typeid(TElementType).name());
#endif

#if !defined(FX_SIZED_ARRAY_NO_MEMPOOL)
        pData = FxMemPool::Alloc<TElementType>(static_cast<uint32>(sizeof(TElementType)) * element_count);
        if (pData == nullptr) {
            NoMemError();
        }
#else
        TElementType* allocated_ptr;
        try {
            allocated_ptr = new TElementType[element_count];
        }
        catch (const std::bad_alloc& e) {
            NoMemError();
        }

        pData = static_cast<TElementType*>(allocated_ptr);
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
        pData = std::move(other.pData);
        other.pData = nullptr;

        Capacity = other.Capacity;
        Size = other.Size;

        other.Size = 0;
        other.Capacity = 0;
    }

    FxSizedArray() = default;

    ~FxSizedArray() { FxSizedArray::Free(); }


    void Free()
    {
        if (pData == nullptr || DoNotDestroy) {
            return;
        }

#if !defined(FX_SIZED_ARRAY_NO_MEMPOOL)
        for (size_t i = 0; i < Size; i++) {
            TElementType& element = pData[i];
            element.~TElementType();
        }

        FxMemPool::Free(static_cast<void*>(pData));
#else
        delete[] pData;
#endif


        // if (Size == 2012) {
        //     FX_BREAKPOINT;
        // }

#ifdef FX_SIZED_ARRAY_DEBUG
        FxLogDebug("Freeing FxSizedArray of size {:d} (type: {:s})", Size, typeid(TElementType).name());
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
        if (index >= Size) {
            throw std::out_of_range("FxSizedArray access out of range");
        }
        return pData[index];
    }

    TElementType& operator[](size_t index)
    {
        if (index >= Size) {
            throw std::out_of_range("FxSizedArray access out of range");
        }
        return pData[index];
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
    void InitAsCopyOf(const FxSizedArray<T>& other)
    {
        InitCapacity(other.Capacity);
        Size = other.Size;

        memcpy(pData, other.pData, other.GetSizeInBytes());
    }

    template <typename T>
        requires std::is_same_v<typename std::remove_const<T>::type, TElementType>
    void InitAsCopyOf(const FxSizedArray<T>& other, SizeType copy_capacity)
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

    void Clear()
    {
        for (size_t i = 0; i < Size; i++) {
            TElementType& element = pData[i];
            element.~TElementType();
        }

        Size = 0;
    }

    inline bool IsEmpty() const { return Size == 0; }
    inline bool IsNotEmpty() const { return !IsEmpty(); }

    TElementType& Insert(const TElementType& object)
    {
        if (Size > Capacity) {
            printf("New Size(%zu) > Capacity(%zu)!\n", Size, Capacity);
            throw std::out_of_range("FxSizedArray insert is larger than the capacity!");
        }

        TElementType* element = &pData[Size++];

        new (element) TElementType(object);

        return *element;
    }

    void Insert(TElementType&& object)
    {
        if (Size > Capacity) {
            printf("New Size(%zu) > Capacity(%zu)!\n", Size, Capacity);
            throw std::out_of_range("FxSizedArray insert is larger than the capacity!");
        }

        TElementType* element = &pData[Size++];

        new (element) TElementType(std::move(object));
    }

    /** Inserts a new empty element into the array and returns a pointer to the element */
    TElementType* Insert()
    {
        if (Size > Capacity) {
            printf("New Size(%zu) > Capacity(%zu)!\n", Size, Capacity);
            throw std::out_of_range("FxSizedArray insert is larger than the capacity!");
        }

        TElementType* element = &pData[Size++];

        new (element) TElementType;

        return element;
    }

    void InitCapacity(size_t element_count)
    {
        if (pData != nullptr) {
            throw std::runtime_error("FxSizedArray has already been previously initialized, cannot InitCapacity!");
        }

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

        FxAssert(ptr >= pData);

        return ptr - pData;
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
            new (&pData[i]) TElementType;
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

    FX_FORCE_INLINE bool IsInited() const { return (pData != nullptr && Capacity > 0); }


protected:
    void InternalAllocateArray(size_t element_count)
    {
#ifdef FX_SIZED_ARRAY_DEBUG
        FxLogDebug("Allocating FxSizedArray of capacity {:d} (type: {:s})", element_count, typeid(TElementType).name());
#endif
#if !defined(FX_SIZED_ARRAY_NO_MEMPOOL)
        pData = FxMemPool::Alloc<TElementType>(sizeof(TElementType) * element_count);
        if (pData == nullptr) {
            NoMemError();
        }
#else
        pData = new TElementType[element_count];

        if (pData == nullptr) {
            NoMemError();
        }

#endif

        // Data = new ElementType[element_count];

        Capacity = element_count;
    }

public:
    TElementType* pData = nullptr;
    SizeType Size = 0;
    SizeType Capacity = 0;


    bool DoNotDestroy = false;
};
