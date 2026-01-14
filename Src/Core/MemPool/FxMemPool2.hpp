#pragma once

#include <Core/FxTypes.hpp>

class FxMemPool2
{
    struct AllocationHeader
    {
        /// The location of the previous allocation
        AllocationHeader* pPrevious = nullptr;
        /// The size of this allocation
        uint32 Size = 0;

        uint32 DistToNext = 0;
    };

public:
    FxMemPool2() = default;

    void Create(void* buffer);

    void* Allocate(uint32 size);


private:
    void* mpBuffer = nullptr;

    AllocationHeader* LastAllocation = nullptr;
};
