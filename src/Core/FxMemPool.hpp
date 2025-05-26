// #pragma once

// #include "Types.hpp"
// #include "FxStaticArray.hpp"
// #include "FxLinkedList.hpp"

// #include <vector>


// // class FxMemPool
// // {
// // public:
// //     struct BlockEntry
// //     {
// //         uint8 *Ptr = nullptr;
// //         uint32 Size = 0;
// //         BlockEntry *NextBlock = nullptr;
// //     };

// //     struct MemPage
// //     {

// //         uint8 *Start = nullptr;
// //         std::vector<BlockEntry> BlockEntries;

// //         bool Used : 1 = false;
// //         bool Full : 1 = false;

// //         BlockEntry *PushEntry(const BlockEntry &entry)
// //         {
// //             if (!Used) {
// //                 Used = true;
// //             }

// //             BlockEntries.push_back(entry);
// //             // std::sort(BlockEntries.begin(), BlockEntries.end(), [](BlockEntry &a, BlockEntry &b){ return a.Ptr < b.Ptr; });

// //             return &BlockEntries[BlockEntries.size() - 1];
// //         }
// //     };

// // public:
// //     FxMemPool();
// //     void Init(uint32 page_size, uint32 num_pages);

// //     void *Alloc(uint64 size);

// //     void Destroy();
// //     ~FxMemPool()
// //     {
// //         Destroy();
// //     }

// // private:
// //     BlockEntry *FindFreeBlockInPage(MemPage &page, uint32 size);
// //     void InitPages();

// // private:
// //     uint32 mPageSize = 0;

// //     uint8 *mMem = nullptr;
// //     FxStaticArray<MemPage> mMemPages;
// // };


// class FxMemPool
// {
// private:
//     struct MemBlock
//     {
//         uint64 Size = 0;
//         uint8 *Start = nullptr;
//     };

// public:
//     FxMemPool() = default;

//     void Create(uint64 size);
//     void *Alloc(uint64 size);

//     void Free(void *ptr);

//     void Destroy();

// private:
//     MemBlock &FindNextFreeBlock(uint64 requested_size);

// private:
//     uint64 mSize = 0;
//     uint8 *mMem = nullptr;

//     FxLinkedList<MemBlock> mMemBlocks;
// };
