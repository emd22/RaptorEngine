#pragma once

#include "Bitset.hpp"

#include <cstdlib>

namespace fx {
template <typename TItemType>
class FreeArray
{
public:
    FreeArray() = default;
    FreeArray(uint32 capacity) { Init(capacity); }

    void Init(uint32 capacity)
    {
        Capacity = capacity;

        pPtr = reinterpret_cast<TItemType*>(malloc(sizeof(TItemType) * capacity));
        SlotsInUse.InitZero(capacity);
    }

    TItemType* NewItem()
    {
        uint32 index = SlotsInUse.FindNextFreeBit();
        if (index == Bitset::scNoFreeBits) {
            return nullptr;
        }

        TItemType* ptr = &pPtr[index];
        new (ptr) TItemType;

        SlotsInUse.Set(index);

        return ptr;
    }

    TItemType* GetItem(uint32 index)
    {
        if (!SlotsInUse.Get(index)) {
            return nullptr;
        }

        return &pPtr[index];
    }

    void FreeItem(uint32 index)
    {
        Assert(index < Capacity);

        TItemType* ptr = GetItem(index);
        if (ptr) {
            ptr->~TItemType();
        }

        SlotsInUse.Unset(index);
    }

    FX_FORCE_INLINE uint32 GetIndexForItem(TItemType* ptr) const
    {
        return reinterpret_cast<uintptr_t>(ptr - pPtr) / sizeof(TItemType);
    }

    // FX_FORCE_INLINE void FreeItem(const TItemType* ptr) { FreeItem(GetIndexForItem(ptr)); }

    ~FreeArray()
    {
        if (pPtr) {
            for (uint32 i = 0; i < Capacity; i++) {
                FreeItem(i);
            }

            free(pPtr);
            pPtr = nullptr;
        }
    }


public:
    TItemType* pPtr = nullptr;
    uint32 Capacity = 0;
    Bitset SlotsInUse;
};
} // namespace fx
