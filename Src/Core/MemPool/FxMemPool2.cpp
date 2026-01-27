#include "FxMemPool2.hpp"

void FxMemPool2::Create(void* buffer)
{
    mpBuffer = buffer;
    AllocationHeader* leader = reinterpret_cast<AllocationHeader*>(mpBuffer);

    leader->pPrevious = nullptr;
    leader->Size = 0;
    leader->DistToNext = 0;
}


void* FxMemPool2::Allocate(uint32 size) { return nullptr; }
