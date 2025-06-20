#include "FxMemPool.hpp"

#include "../FxPanic.hpp"
#include "../Types.hpp"

namespace experimental {

void FxMemPoolPage::Create(uint64 size)
{
    if (mMem || mSize) {
        FxPanic("FxMemPool", "Mem pool has already been initialized!", 0);
    }

    mSize = size;

    void* allocated_ptr = std::malloc(mSize);
    if (allocated_ptr == nullptr) {
        FxPanic("FxMemPool", "Error allocating buffer for memory pool!", 0);
    }

    mMem = static_cast<uint8*>(allocated_ptr);

    mMemBlocks.Create(128);
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


auto FxMemPoolPage::AllocateMemory(uint64 requested_size) -> FxLinkedList<FxMemPoolPage::MemBlock>::Node*
{
    auto* node = mMemBlocks.Head;

    // Check to see if there is space at the start of the list
    if (node && node == mMemBlocks.Head && node->Data.Start != mMem) {
        uint64 gap_size = (node->Data.Start) - mMem;

        if (gap_size >= GetAlignedValue(requested_size)) {
            FxMemPoolPage::MemBlock new_block{
                .Size = GetAlignedValue(requested_size),
                .Start = mMem
            };

            return mMemBlocks.InsertFirst(new_block);
        }
    }

    // Walk through currently allocated blocks. If there is a gap, check to see if it is large enough.
    while (node != nullptr) {
        FxMemPoolPage::MemBlock& block = node->Data;

        // If there is no node next, break as we won't find a gap. This means we will fallthrough
        // to below, adding a new block to the end of the list.
        if (node->Next == nullptr) {
            break;
        }

        FxMemPoolPage::MemBlock& next_block = node->Next->Data;

        uint8* current_block_end = block.Start + block.Size;
        const uint64 gap_size = next_block.Start - current_block_end;

        // The gap is large enough for our buffer, take it
        if (gap_size >= requested_size) {
            FxMemPoolPage::MemBlock new_block{
                .Size = GetAlignedValue(requested_size),
                .Start = GetAlignedPtr(current_block_end)
            };

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
        const FxMemPoolPage::MemBlock& last_block = mMemBlocks.Tail->Data;
        new_block_ptr = last_block.Start + last_block.Size + 8;

        // Check to make sure there is space left in the pool
        if (static_cast<uint64>(new_block_ptr - mMem) > GetAlignedValue(mSize)) {
            //FxPanic("FxMemPool", "Could not resize memory pool! (Not implemented)", 0);
            return nullptr;
        }
    }

    // Create a new block entry for the allocation
    FxMemPoolPage::MemBlock new_block{
        .Size = GetAlignedValue(requested_size),
        .Start = GetAlignedPtr(new_block_ptr)
    };

    auto* new_node = mMemBlocks.InsertLast(std::move(new_block));

    return new_node;
}

auto FxMemPoolPage::GetNodeFromPtr(void* ptr) const -> FxLinkedList<FxMemPoolPage::MemBlock>::Node*
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

FxMemPool& FxMemPool::GetGlobalPool()
{
    static FxMemPool global_pool;
    return global_pool;
}

auto FxMemPool::AllocateMemory(uint64 requested_size) -> FxLinkedList<FxMemPoolPage::MemBlock>::Node*
{
    auto* node = mCurrentPage->AllocateMemory(requested_size);

    // No space remaining in the current page, try to allocate a new one
    if (node == nullptr) {
        AllocateNewPage();
        node = mCurrentPage->AllocateMemory(requested_size);
    }

    // If we have a fresh memory block and that is still too small, we can assume
    // the memory block is larger than page size, fail
    if (node == nullptr) {
        FxPanic("FxMemPool", "Allocation is too large for memory pool page size!", 0);
    }

    return node;
}



void* FxMemPoolPage::Realloc(void* ptr, uint32 new_size)
{
    auto* node = GetNodeFromPtr(ptr);

    FxMemPoolPage::MemBlock& block = node->Data;

    // Node is at end of block list, simply increase size
    if (node->Next == nullptr) {
        node->Data.Size = GetAlignedValue(new_size);
        return node->Data.Start;
    }
    // There is a node after this, check the gap

    FxMemPoolPage::MemBlock& next_block = node->Next->Data;
    intptr_t gap_size = next_block.Start - (block.Start + block.Size);

    // There is enough space between the nodes, reallocate in place
    if (gap_size >= new_size) {
        node->Data.Size = GetAlignedValue(new_size);
        return node->Data.Start;
    }

    // Worst case scenario, allocate a new block and copy to there.
    void* new_mem = Alloc<void>(new_size);
    std::memcpy(new_mem, block.Start, block.Size);

    // Remove the previous allocation from the memory pool
    mMemBlocks.DeleteNode(node);

    // Return the new block of memory
    return new_mem;
}



void FxMemPoolPage::PrintAllocations() const
{
    auto* node = mMemBlocks.Head;

    Log::Debug("");
    Log::Debug("=== Memory Pool [size=%llu, start=%p] ===", mSize, mMemBlocks.Head);

    while (node != nullptr) {
        FxMemPoolPage::MemBlock& block = node->Data;

        if (node->Next) {
            FxMemPoolPage::MemBlock& next_block = node->Next->Data;

            uint8* current_block_end = block.Start + block.Size;
            const uint64 gap_size = next_block.Start - current_block_end;

            if (gap_size) {
                Log::Debug("MemGap  [size=%llu, ptr=%p]", gap_size, current_block_end);
            }
        }

        if (node->Prev == nullptr && node == mMemBlocks.Head && node->Data.Start != mMem) {
            FxMemPoolPage::MemBlock& block = node->Data;

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

void FxMemPoolPage::Destroy()
{
    std::free(mMem);
    mSize = 0;
}



////////////////////////////
// FxMemPool Functions
////////////////////////////



void* FxMemPool::Alloc(uint32 size, FxMemPool* pool)
{
    if (pool == nullptr) {
        pool = &GetGlobalPool();
    }

    FxMemPoolPage::MemBlock& block = pool->AllocateMemory(size)->Data;

    return static_cast<void*>(block.Start);
}

bool FxMemPool::IsPtrInPage(void* ptr, FxMemPoolPage* page) const
{
    const uint8* start_ptr = page->mMem;
    const uint8* end_ptr = start_ptr + mPageSize;

    // If the ptr is within the bounds of the page, return the page
    if (ptr >= start_ptr && ptr <= end_ptr) {
        return true;
    }

    return false;
}

FxMemPoolPage* FxMemPool::FindPtrInPage(void* ptr)
{
    // Iterate in reverse as most allocations being freed are likely to be at the end

    size_t num_pages = mPoolPages.size() - 1;

    for (int32 i = num_pages; i >= 0; i--) {
        FxMemPoolPage* page = &mPoolPages[i];

        if (IsPtrInPage(ptr, page)) {
            return page;
        }
    }

    // The ptr was not found in any pages
    return nullptr;
}

void FxMemPool::AllocateNewPage()
{

    FxMemPoolPage page;

    mPoolPages.emplace_back(std::move(page));
    mCurrentPage = &mPoolPages.back();
    mCurrentPage->Create(mPageSize);

}

void FxMemPool::Free(void* ptr, FxMemPool* pool)
{
    if (pool == nullptr) {
        pool = &GetGlobalPool();
    }

    if (ptr == nullptr) {
        return;
    }

    // From the global pool,
    pool->FindPtrInPage(ptr)->Free(ptr);
}

} // namespace experimental
