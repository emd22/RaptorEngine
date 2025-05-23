#pragma once

#include "StaticArray.hpp"
#include <Core/Log.hpp>

template <typename ElementType>
class StaticPtrArray : public StaticArray<ElementType *>
{
protected:
    void InternalAllocateArray(size_t element_count) override
    {
        try {
    #ifdef FX_DEBUG_STATIC_ARRAY
            Log::Debug("Allocating StaticArray of capacity %lu (type: %s)", element_count, typeid(ElementType).name());
    #endif
            this->Data = new ElementType *[element_count];
            for (int i = 0; i < element_count; i++) {
                this->Data[i] = new ElementType;
            }
        }
        catch (std::bad_alloc &e) {
            NoMemError();
        }
    }

public:
    void Free() override
    {
        if (this->Data != nullptr) {
    #ifdef FX_DEBUG_STATIC_ARRAY
            Log::Debug("Freeing StaticArray of size %lu (type: %s)", this->Size, typeid(ElementType).name());
    #endif
            for (int i = 0; i < this->Capacity; i++) {
                delete this->Data[i];
            }
            delete[] this->Data;

            this->Data = nullptr;
        }
        this->Size = 0;
        this->Capacity = 0;
    }

    ~StaticPtrArray()
    {
        StaticPtrArray::Free();
    }
};
