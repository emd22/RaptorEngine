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

    FX_FORCE_INLINE bool IsInited() const { return pPtr != nullptr; }

    TItemType* NewItem(uint32* out_index = nullptr)
    {
        uint32 index = SlotsInUse.FindNextFreeBit();
        if (index == Bitset::scNoFreeBits) {
            return nullptr;
        }

        TItemType* ptr = &pPtr[index];
        new (ptr) TItemType;

        SlotsInUse.Set(index);

        if (out_index != nullptr) {
            (*out_index) = index;
        }

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

        MarkItemFree(index);
    }

    FX_FORCE_INLINE void MarkItemFree(uint32 index) { SlotsInUse.Unset(index); }

    FX_FORCE_INLINE uint32 GetIndexForItem(TItemType* ptr) const
    {
        return reinterpret_cast<uintptr_t>(ptr - pPtr) / sizeof(TItemType);
    }

    void Free()
    {
        if (pPtr) {
            for (uint32 i = 0; i < Capacity; i++) {
                FreeItem(i);
            }

            free(pPtr);
            pPtr = nullptr;

            SlotsInUse.ClearAll();
        }

        Capacity = 0;
    }

    ~FreeArray() { Free(); }

public:
    TItemType* pPtr = nullptr;
    uint32 Capacity = 0;
    Bitset SlotsInUse;
};
} // namespace fx
