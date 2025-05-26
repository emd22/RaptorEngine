#pragma once

#include <stddef.h>
#include <stdexcept>
#include <stdio.h>
#include <cstdlib>
#include <initializer_list>

#include <Core/Log.hpp>

static inline void NoMemError()
{
    puts("FxStaticArray: out of memory");
    std::terminate();
}

template <typename ElementType>
class FxStaticArray
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

    FxStaticArray(ElementType* ptr, size_t size)
        : Data(ptr), Size(size), Capacity(size)
    {
    }

    FxStaticArray(size_t element_count)
        : Capacity(element_count)
    {
#ifdef FX_STATIC_ARRAY_DEBUG
        Log::Debug("Allocating FxStaticArray of capacity %zu (type: %s)", Capacity, typeid(ElementType).name());
#endif
        void* allocated_ptr = std::malloc(sizeof(ElementType) * element_count);

        if (allocated_ptr == nullptr) {
            NoMemError();
        }

        Data = static_cast<ElementType*>(allocated_ptr);
        for (size_t i = 0; i < element_count; i++) {
            new(Data + i) ElementType;
        }
    }

    FxStaticArray(std::initializer_list<ElementType> list)
    {
        InitCapacity(list.size());

        for (const ElementType& obj : list) {
            Insert(obj);
        }
    }

    FxStaticArray(FxStaticArray<ElementType>&& other)
    {
        Data = std::move(other.Data);
        other.Data = nullptr;

        Capacity = other.Capacity;
        Size = other.Size;
    }

    FxStaticArray() = default;

    ~FxStaticArray()
    {
        FxStaticArray::Free();
    }

    virtual void Free()
    {
        if (Data == nullptr) {
            return;
        }

#ifdef FX_DEBUG_STATIC_ARRAY
        Log::Debug("Freeing FxStaticArray of size %zu (type: %s)", Size, typeid(ElementType).name());
#endif
        for (size_t i = 0; i < Size; i++) {
            ElementType &element = Data[i];
            element.~ElementType();
        }

        std::free(Data);

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
        if (index > Capacity) {
            throw std::out_of_range("FxStaticArray access out of range");
        }
        return Data[index];
    }

    ElementType& operator[] (size_t index)
    {
        if (index > Capacity) {
            throw std::out_of_range("FxStaticArray access out of range");
        }
        return Data[index];
    }

    FxStaticArray<ElementType> operator = (std::initializer_list<ElementType>& list)
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

    FxStaticArray<ElementType> operator = (FxStaticArray<ElementType>&& other)
    {
        Data = other.Data;
        other.Data = nullptr;

        Size = other.Size;
        Capacity = other.Capacity;

        return *this;
    }

    void Clear() { Size = 0; }
    bool IsEmpty() { return Size == 0; }
    bool IsNotEmpty() { return !IsEmpty(); }

    void Insert(const ElementType& object)
    {
        if (Size > Capacity) {
            printf("New Size(%zu) > Capacity(%zu)!\n", Size, Capacity);
            throw std::out_of_range("FxStaticArray insert is larger than the capacity!");
        }

        memcpy(&Data[Size++], &object, sizeof(ElementType));
    }

    /** Inserts a new empty element into the array and returns a pointer to the element */
    ElementType* Insert()
    {
        if (Size > Capacity) {
            printf("New Size(%zu) > Capacity(%zu)!\n", Size, Capacity);
            throw std::out_of_range("FxStaticArray insert is larger than the capacity!");
        }

        ElementType* element = &Data[Size++];

        return element;
    }

    void InitCapacity(size_t element_count)
    {
        if (Data != nullptr) {
            throw std::runtime_error("FxStaticArray has already been previously initialized, cannot InitCapacity!");
        }

        InternalAllocateArray(element_count);

        Capacity = element_count;
    }

    /**
     *   Initializes an array to contain `element_count` elements, which can be modified externally.
     *
     *   Example:
     *   ```cpp
     *      FxStaticArray<int32> int_array(10);
     *      int_array.InitSize();
     *
     *      FxStaticArray<int32> other_array;
     *      other_array.InitSize(15);
     *   ```
     */
    void InitSize(size_t element_count)
    {
        if (Capacity == 0) {
            InitCapacity(element_count);
        }

        Size = Capacity;
    }

    void InitSize()
    {
        if (Capacity == 0) {
            throw std::runtime_error("Static array capacity has not been set!");
        }

        Size = Capacity;
    }

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
        Log::Debug("Allocating FxStaticArray of capacity %zu (type: %s)", element_count, typeid(ElementType).name());
#endif
        void* allocated_ptr = std::malloc(sizeof(ElementType) * element_count);

        if (allocated_ptr == nullptr) {
            NoMemError();
        }

        Data = static_cast<ElementType*>(allocated_ptr);
        for (size_t i = 0; i < element_count; i++) {
            new(Data + i) ElementType;
        }


        Capacity = element_count;
    }

public:
    size_t Size = 0;
    size_t Capacity = 0;

    ElementType* Data = nullptr;
};
