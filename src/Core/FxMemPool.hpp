#pragma once

#include "Types.hpp"
#include "FxLinkedList.hpp"

class FxMemPool
{
private:
    struct MemBlock
    {
        uint64 Size = 0;
        uint8* Start = nullptr;
    };

public:
    FxMemPool() = default;

    void Create(uint32 size_kb);

    template <typename ElementType>
    ElementType* Alloc(uint32 size)
    {
        MemBlock& block = AllocateMemory(size)->Data;

        return reinterpret_cast<ElementType*>(block.Start);
    }

    template <typename ElementType>
    void Free(ElementType* ptr)
    {
        auto* node = GetNodeFromPtr(reinterpret_cast<void*>(ptr));
        mMemBlocks.DeleteNode(node);
    }

    //void Destroy();

private:
    auto AllocateMemory(uint64 requested_size) -> FxLinkedList<FxMemPool::MemBlock>::Node*;
    auto GetNodeFromPtr(void* ptr) -> FxLinkedList<FxMemPool::MemBlock>::Node*;

private:

    uint64 mSize = 0;
    uint8* mMem = nullptr;

    FxLinkedList<MemBlock> mMemBlocks;
};
