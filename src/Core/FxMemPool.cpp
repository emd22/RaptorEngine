#include "FxMemPool.hpp"
#include "FxPanic.hpp"
#include <mutex>


void FxMemPool::Create(uint32 size_kb)
{
    std::lock_guard guard(mMemMutex);

    if (mMem || mSize) {
        FxPanic("FxMemPool", "Mem pool has already been initialized!", 0);
    }

    mSize = static_cast<uint64>(size_kb) * 1024;

    void* allocated_ptr = std::malloc(mSize);
    if (allocated_ptr == nullptr) {
        FxPanic("FxMemPool", "Error allocating buffer for memory pool!", 0);
    }

    mMem = static_cast<uint8*>(allocated_ptr);

    mMemBlocks.Create(128);
}

FxMemPool& FxMemPool::GetGlobalPool()
{
    static FxMemPool GlobalPool;

    return GlobalPool;
}

static inline uint64 GetAlignedValue(uint64 value)
{
    // Get if the value is aligned per 8 bytes
    uint64 align_value = (value % 16);

    // If the value is not aligned, offset our value
    if (align_value) {
        value += (16 - align_value);
    }

    return value;
}

static inline uint8* GetAlignedPtr(uint8* ptr)
{
    return reinterpret_cast<uint8*>(GetAlignedValue(reinterpret_cast<uintptr_t>(ptr)));
    // return ptr;
}


auto FxMemPool::AllocateMemory(uint64 requested_size) -> FxLinkedList<FxMemPool::MemBlock>::Node*
{
    printf("*** requesting size %llu\n", requested_size);
    std::lock_guard guard(mMemMutex);

    auto* node = mMemBlocks.Head;

    // Check to see if there is space at the start of the list
    if (node && node == mMemBlocks.Head && node->Data.Start != mMem) {
        uint64 gap_size = (node->Data.Start) - mMem;

        if (gap_size >= GetAlignedValue(requested_size)) {
            FxMemPool::MemBlock new_block{
                .Size = GetAlignedValue(requested_size),
                .Start = mMem
            };

            printf("Allocating (reusing from start) memory block at %p (size: %llu)\n", new_block.Start, new_block.Size);

            return mMemBlocks.InsertFirst(new_block);
        }
    }

    // Walk through currently allocated blocks. If there is a gap, check to see if it is large enough.
    while (node != nullptr) {
        FxMemPool::MemBlock& block = node->Data;

        // If there is no node next, break as we won't find a gap. This means we will fallthrough
        // to below, adding a new block to the end of the list.
        if (node->Next == nullptr) {
            break;
        }

        FxMemPool::MemBlock& next_block = node->Next->Data;

        uint8* current_block_end = block.Start + block.Size;
        const uint64 gap_size = next_block.Start - current_block_end;

        // The gap is large enough for our buffer, take it
        if (gap_size >= requested_size) {
            FxMemPool::MemBlock new_block{
                .Size = GetAlignedValue(requested_size),
                .Start = GetAlignedPtr(current_block_end)
            };

            printf("Allocating (reusing) memory block at %p (size: %llu)\n", new_block.Start, new_block.Size);

            // Track that the block is now allocated
            return mMemBlocks.InsertAfterNode(new_block, node);
        }

        node = node->Next;
    }

    // There are no gaps betweem the allocated blocks, create a new block.
    uint8* new_block_ptr = mMem;

    // If there are previous allocations (there is a tail), then we can find the furthest pointer
    // along and allocate after that. If there have not been previous allocations, we will use the
    // start of the pool as defined above.
    if (mMemBlocks.Tail) {
        const FxMemPool::MemBlock& last_block = mMemBlocks.Tail->Data;
        new_block_ptr = last_block.Start + last_block.Size + 8;

        // Check to make sure there is space left in the pool
        if (static_cast<uint64>(new_block_ptr - mMem) > GetAlignedValue(mSize)) {
            FxPanic("FxMemPool", "Could not resize memory pool! (Not implemented)", 0);
            return nullptr;
        }
    }

    // Create a new block entry for the allocation
    FxMemPool::MemBlock new_block{
        .Size = GetAlignedValue(requested_size),
        .Start = GetAlignedPtr(new_block_ptr)
    };

    printf("Allocating end memory block at %p (size: %llu)\n", new_block.Start, new_block.Size);

    auto* new_node = mMemBlocks.InsertLast(new_block);

    return new_node;
}

auto FxMemPool::GetNodeFromPtr(void* ptr) const -> FxLinkedList<FxMemPool::MemBlock>::Node*
{
    auto* node = mMemBlocks.Head;

    while (node != nullptr) {
        if (node->Data.Start == ptr) {
            return node;
        }

        node = node->Next;
    }
    return nullptr;
}


void* FxMemPool::Alloc(uint32 size)
{
    FxMemPool& global_pool = GetGlobalPool();

    global_pool.CheckInited();
    MemBlock& block = global_pool.AllocateMemory(size)->Data;

    return static_cast<void*>(block.Start);
}

void FxMemPool::Free(void* ptr)
{
    if (ptr == nullptr) {
        return;
    }

    FxMemPool& global_pool = GetGlobalPool();


    std::lock_guard guard(global_pool.mMemMutex);

    auto* node = global_pool.GetNodeFromPtr(ptr);
    // printf("Freeing memory block at %p (size %llu)\n", node->Data.Start, node->Data.Size);
    global_pool.mMemBlocks.DeleteNode(node);
}


void FxMemPool::PrintAllocations() const
{
    auto* node = mMemBlocks.Head;

    Log::Debug("");
    Log::Debug("=== Memory Pool [size=%llu, start=%p] ===", mSize, mMemBlocks.Head);

    while (node != nullptr) {
        FxMemPool::MemBlock& block = node->Data;

        if (node->Next) {
            FxMemPool::MemBlock& next_block = node->Next->Data;

            uint8* current_block_end = block.Start + block.Size;
            const uint64 gap_size = next_block.Start - current_block_end;

            printf("current block start: %p, block size: %llu, next block start: %p\n", block.Start, block.Size, next_block.Start);

            if (gap_size) {
                Log::Debug("MemGap  [size=%llu, ptr=%p]", gap_size, current_block_end);
            }
        }

        if (node->Prev == nullptr && node == mMemBlocks.Head && node->Data.Start != mMem) {
            FxMemPool::MemBlock& block = node->Data;

            uint64 gap_size = block.Start - mMem;
            if (gap_size) {
                Log::Debug("MemGap  [size=%llu, ptr=%p]", gap_size, mMem);
            }
        }

        Log::Debug("MemAlloc[size=%llu, ptr=%p]", block.Size, block.Start);

        node = node->Next;
    }
    Log::Debug("");
}

void FxMemPool::Destroy()
{
    std::lock_guard guard(mMemMutex);

    std::free(mMem);
    mSize = 0;
}
