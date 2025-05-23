#include "FxMemPool.hpp"

#include <cstdlib>

// void FxMemPool::Init(uint32 page_size, uint32 num_pages)
// {
//     mPageSize = page_size;

//     uint64 mem_size = (uint64)mPageSize * num_pages;
//     mMem = (uint8 *)std::malloc(mem_size);

//     mMemPages.InitSize(num_pages);
//     InitPages();
// }

// void *FxMemPool::Alloc(uint64 size)
// {
//     BlockEntry *entry = nullptr;
//     int page_index = 0;

//     StaticArray<BlockEntry *> entries;
//     entries.InitCapacity((size / mPageSize) + 1);

//     BlockEntry *prev_entry = nullptr;

//     while (size > 0) {
//         MemPage &page = mMemPages[page_index++];

//         // For an allocation, find the minimum between the page size and the size of the buffer
//         const uint32 page_size = (size > mPageSize) ? mPageSize : size;

//         entry = FindFreeBlockInPage(page, page_size);

//         if (!entry) {
//             continue;
//         }

//         if (prev_entry) {
//             prev_entry->NextBlock = entry;
//         }

//         // There is a free (section) of a block, save it in our blocks array
//         entries.Insert(entry);
//         size -= page_size;

//         prev_entry = entry;

//         page.Used = true;
//     }

// }

// // void *FxMemPool::RequestBlock(uint32 size)
// // {
// //     FxMemPool::MemPage *page = nullptr;

// //     uint32 page_index = 0;
// //     do {
// //         page = &mMemPages[page_index++];
// //         FindFreeBlockInPage(page);
// //     } while (page_index < mMemPages.Size);

// //     return page;
// // }

// FxMemPool::BlockEntry *FxMemPool::FindFreeBlockInPage(MemPage &page, uint32 size)
// {
//     // The page is marked full, continue to next page
//     if (page.Full) {
//         return nullptr;
//     }

//     // Page has not been used before, allocate at start and skip checks
//     if (!page.Used) {
//         page.Used = true;
//         return page.PushEntry(BlockEntry{ .Ptr = page.Start, .Size = size });
//     }

//     uint8 *start = page.Start;
//     const int32 entry_count = page.BlockEntries.size();

//     uint32 current_offset = 0;

//     // Iterate through all entries that have been registered in our page
//     for (int32 i = 0; i < entry_count; i++) {
//         BlockEntry *this_block = &page.BlockEntries[i];

//         current_offset += this_block->Size;

//         // If there is no space left in the page, return null
//         if (current_offset > mPageSize) {
//             // Mark that the page is now full
//             page.Full = true;

//             return nullptr;
//         }

//         // Get the next block if it exists
//         BlockEntry *next_block = nullptr;
//         if (i + 1 < entry_count) {
//             next_block = &page.BlockEntries[i + 1];
//         }
//         // There is no other blocks, break as we have remaining free space
//         else {
//             break;
//         }

//         // There are other blocks, check if there is space between the current
//         // and the next block that can fit our block.
//         if ((uint64)next_block - ((uint64)this_block->Ptr + this_block->Size) >= size) {
//             break;
//         }

//         // There is not enough space, continue on to the next block.
//     }

//     // Push our new block entry to the block entry vector
//     return page.PushEntry(BlockEntry{ .Ptr = start + current_offset, .Size = size });
// }

// void FxMemPool::InitPages()
// {
//     for (int i = 0; i < mMemPages.Size; i++) {
//         MemPage &page = mMemPages[i];
//         page.Start = &mMem[(uint64)mPageSize * i];
//         page.BlockEntries.reserve(32);
//     }
// }


// void FxMemPool::Destroy()
// {
//     mPageSize = 0;
//     std::free(mMem);
// }


void FxMemPool::Create(uint64 size)
{
    mSize = size;

    mMem = (uint8 *)std::malloc(mSize);
    if (!mMem) {
        throw std::bad_alloc();
    }

    mMemBlocks.Create(128);
}

FxMemPool::MemBlock &FxMemPool::FindNextFreeBlock(uint64 requested_size)
{
    FxLinkedList<MemBlock>::Node *current_node = mMemBlocks.Head;

    while (current_node != nullptr) {
        if (current_node->Next == nullptr) {
            // Get the ptr for the start of the pool
            uint8 *start_ptr = mMemBlocks.Head->Data.Start;
            uint8 *end_ptr = current_node->Data.Start + current_node->Data.Size;

            uint64 bytes_used = static_cast<uint64>(end_ptr - start_ptr);

            if (bytes_used + requested_size >= mSize) {
                // We are over the memory size of the pool, resize the pool
                mSize *= 2;
                mMem = static_cast<uint8 *>(std::realloc(mMem, mSize));
            }
        }

        uint8 *current_block_end = current_node->Data.Start + current_node->Data.Size;

        int64 free_block_size = current_node->Next->Data.Start - current_block_end;

        if (free_block_size >= requested_size) {
            MemBlock block { .Size = requested_size, .Start = current_block_end };

            FxLinkedList<MemBlock>::Node *node = mMemBlocks.NewNode(block);
            node->InsertNodeAfter(current_node);

            return node->Data;
        }
    }
}

void *FxMemPool::Alloc(uint64 size)
{
    MemBlock &block = FindNextFreeBlock(size);

}

void FxMemPool::Destroy()
{
    std::free(mMem);
}
