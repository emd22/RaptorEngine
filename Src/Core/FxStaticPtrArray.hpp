#pragma once

#include "FxSizedArray.hpp"

#include <Core/Log.hpp>

template <typename ElementType>
class FxStaticPtrArray : public FxSizedArray<ElementType*>
{
protected:
    void InternalAllocateArray(size_t element_count) override
    {
#ifdef FX_DEBUG_STATIC_ARRAY
        FxLogDebug("Allocating FxSizedArray of capacity {:d} (type: {:s})", element_count, typeid(ElementType).name());
#endif
        // this->Data = new ElementType *[element_count];
        void* allocated_ptr = std::malloc(sizeof(ElementType*) * element_count);
        if (allocated_ptr == nullptr) {
            NoMemError();
        }

        this->Data = static_cast<ElementType**>(allocated_ptr);

        for (size_t i = 0; i < element_count; i++) {
            this->Data[i] = new ElementType;
        }
    }

public:
    void Free() override
    {
        if (this->Data != nullptr) {
#ifdef FX_DEBUG_STATIC_ARRAY
            FxLogDebug("Freeing FxSizedArray of size {:d} (type: {:s})", this->Size, typeid(ElementType).name());
#endif
            for (int i = 0; i < this->Capacity; i++) {
                delete this->Data[i];
            }

            std::free(this->Data);
            this->Data = nullptr;
        }

        this->Size = 0;
        this->Capacity = 0;
    }

    ~FxStaticPtrArray() { FxStaticPtrArray::Free(); }
};
