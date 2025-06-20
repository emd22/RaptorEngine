#pragma once

#include "../Types.hpp"

#include "../FxLinkedList.hpp"

#include <vector>

namespace experimental {

class FxMemPoolPage
{
private:
    struct MemBlock
    {
        uint64 Size = 0;
        uint8* Start = nullptr;
    };
    friend class FxMemPool;

public:
    FxMemPoolPage() = default;

    FxMemPoolPage(FxMemPoolPage&& other) noexcept
    {
        mSize = other.mSize;
        mMem = other.mMem;
        mMemBlocks = std::move(other.mMemBlocks);

        other.mMem = nullptr;
        other.mSize = 0;
    }

    void Create(uint64 size);

    /** Allocates a block of memory */
    template <typename PtrType>
    PtrType* Alloc(uint32 size)
    {
        MemBlock& block = AllocateMemory(size)->Data;

        return static_cast<PtrType*>(block.Start);
    }


    void* Realloc(void* ptr, uint32 new_size);

    template <typename Type>
    Type* Realloc(Type* ptr, uint32 new_size)
    {
        return static_cast<Type*>(Realloc(static_cast<void*>(ptr), new_size));
    }

    /** Frees an allocated pointer */
    template <typename ElementType>
    void Free(ElementType* ptr)
    {
        if (ptr == nullptr) {
            return;
        }
        //std::lock_guard guard(mMemMutex);

        auto* node = GetNodeFromPtr(static_cast<void*>(ptr));
        mMemBlocks.DeleteNode(node);
    }

    void PrintAllocations() const;

    void Destroy();
    ~FxMemPoolPage()
    {
        Destroy();
    }

    inline bool IsInited() const
    {
        return (mMem != nullptr && mSize > 0);
    }

private:
    auto AllocateMemory(uint64 requested_size) -> FxLinkedList<FxMemPoolPage::MemBlock>::Node*;
    auto GetNodeFromPtr(void* ptr) const->FxLinkedList<FxMemPoolPage::MemBlock>::Node*;

    inline void CheckInited()
    {
        if (!IsInited()) {
            Create(64);
        }
    }

private:

    uint64 mSize = 0;
    uint8* mMem = nullptr;

    FxLinkedList<MemBlock> mMemBlocks;
};

class FxMemPool
{
public:
    FxMemPool() = default;

    static FxMemPool& GetGlobalPool();

    void Create(uint32 page_size_kb)
    {
        mPageSize = static_cast<uint64>(page_size_kb) * page_size_kb;

        AllocateNewPage();
    }

    /** Allocates memory on either this memory pool or the global pool if `pool` is null. */
    static void* Alloc(uint32 size, FxMemPool* pool = nullptr);

    template <typename Type>
    static Type* Alloc(uint32 size, FxMemPool* pool = nullptr)
    {
        return static_cast<Type*>(Alloc(size, pool));
    }

    /** Frees an allocated pointer on the global memory pool */
    static void Free(void* ptr, FxMemPool* pool = nullptr);

    template <typename Type>
    static void Free(Type* ptr, FxMemPool* pool = nullptr)
    {
        Free(static_cast<void*>(ptr), pool);
    }

private:

    auto AllocateMemory(uint64 requested_size) -> FxLinkedList<FxMemPoolPage::MemBlock>::Node*;

    FxMemPoolPage* FindPtrInPage(void* ptr);
    inline bool IsPtrInPage(void* ptr, FxMemPoolPage* page) const;

    void AllocateNewPage();

private:
    FxMemPoolPage* mCurrentPage = nullptr;
    uint64 mPageSize = 0;

    std::vector<FxMemPoolPage> mPoolPages;

};

} // namespace experimental
