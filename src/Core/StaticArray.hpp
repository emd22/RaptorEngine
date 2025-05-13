#pragma once

#include <stddef.h>
#include <stdexcept>
#include <stdio.h>
#include <initializer_list>

#include <Core/Log.hpp>

static inline void NoMemError()
{
    puts("StaticArray: out of memory");
    std::terminate();
}

template <typename ElementType>
class StaticArray
{
public:
    struct Iterator
    {
        Iterator(ElementType *ptr, size_t index)
            : mPtr(ptr), mIndex(index)
        {
        }

        Iterator &operator++()
        {
            mIndex++;
            return *this;
        }

        Iterator &operator++(int value) {
            Iterator before = *this;
            mIndex++;
            return before;
        }

        ElementType &operator * () const
        {
            return mPtr[mIndex];
        }

        bool operator==(const Iterator &b) const
        {
            return mPtr == b.mPtr && mIndex == b.mIndex;
        }

    private:
        ElementType *mPtr;
        size_t mIndex;
    };

    StaticArray(ElementType *ptr, size_t size)
        : Data(ptr), Size(size)
    {
    }

    StaticArray(size_t element_count)
        : Capacity(element_count)
    {
        try {
    #ifdef FX_STATIC_ARRAY_DEBUG
            Log::Debug("Allocating StaticArray of capacity %lu (type: %s)", Capacity, typeid(ElementType).name());
    #endif
            Data = new ElementType[element_count];
        }
        catch (std::bad_alloc &e) {
            NoMemError();
        }
    }

    StaticArray(std::initializer_list<ElementType> list)
    {
        InitCapacity(list.size());

        for (const ElementType &obj : list) {
            Insert(obj);
        }
    }



    StaticArray(StaticArray<ElementType> &&other)
    {
        Data = std::move(other.Data);
        other.Data = nullptr;

        Capacity = other.Capacity;
        Size = other.Size;
    }

    StaticArray() = default;

    ~StaticArray()
    {
        Free();
    }

    virtual void Free()
    {
        if (Data != nullptr) {
    #ifdef FX_DEBUG_STATIC_ARRAY
            Log::Debug("Freeing StaticArray of size %lu (type: %s)", Size, typeid(ElementType).name());
    #endif
            delete Data;

            Data = nullptr;
            Capacity = 0;
            Size = 0;
        }
    }

    Iterator begin()
    {
        return Iterator(Data, 0);
    }

    Iterator end()
    {
        return Iterator(Data, Size);
    }

    operator ElementType *() const
    {
        return Data;
    }

    operator ElementType &() const
    {
        return *Data;
    }

    const ElementType &operator[] (size_t index) const
    {
        if (index > Capacity) {
            throw std::out_of_range("StaticArray access out of range");
        }
        return Data[index];
    }

    ElementType &operator[] (size_t index)
    {
        if (index > Capacity) {
            throw std::out_of_range("StaticArray access out of range");
        }
        return Data[index];
    }

    StaticArray<ElementType> operator = (std::initializer_list<ElementType> &list)
    {
        const size_t list_size = list.size();
        if (list_size > Capacity) {
            Free();
            InitCapacity(list.size());
        }

        Clear();

        for (const ElementType &obj : list) {
            Insert(obj);
        }

        return *this;
    }

    StaticArray<ElementType> operator = (StaticArray<ElementType> &&other)
    {
        Data = std::move(other.Data);
        other.Data = nullptr;

        Size = other.Size;
        Capacity = other.Capacity;

        return *this;
    }

    void Clear() { Size = 0; }
    bool IsEmpty() { return Size == 0; }
    bool IsNotEmpty() { return !IsEmpty(); }

    void Insert(const ElementType &object)
    {
        if (Size > Capacity) {
            printf("New Size(%lu) > Capacity(%lu)!\n", Size, Capacity);
            throw std::out_of_range("StaticArray insert is larger than the capacity!");
        }

        memcpy(&Data[Size++], &object, sizeof(ElementType));
    }

    /** Inserts a new empty element into the array and returns a pointer to the element */
    ElementType *Insert()
    {
        if (Size > Capacity) {
            printf("New Size(%lu) > Capacity(%lu)!\n", Size, Capacity);
            throw std::out_of_range("StaticArray insert is larger than the capacity!");
        }

        ElementType *element = &Data[Size++];

        return element;
    }

    void InitCapacity(size_t element_count)
    {
        if (Data != nullptr) {
            throw std::runtime_error("StaticArray has already been previously initialized, cannot InitCapacity!");
        }

        InternalAllocateArray(element_count);

        Capacity = element_count;
    }

    /**
     *   Initializes an array to contain `element_count` elements, which can be modified externally.
     *
     *   Example:
     *
     *      StaticArray<int32> int_array(10);
     *      int_array.InitSize();
     *
     *      StaticArray<int32> other_array;
     *      other_array.InitSize(15);
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
        try {
    #ifdef FX_DEBUG_STATIC_ARRAY
            Log::Debug("Allocating StaticArray of capacity %lu (type: %s)", element_count, typeid(ElementType).name());
    #endif
            Data = new ElementType[element_count];
        }
        catch (std::bad_alloc &e) {
            NoMemError();
        }
    }

public:
    size_t Size = 0;
    size_t Capacity = 0;

    ElementType *Data = nullptr;
};
