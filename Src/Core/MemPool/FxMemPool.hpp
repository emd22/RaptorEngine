#pragma once

#include "../Types.hpp"

#include "FxMPLinkedList.hpp"

#include <type_traits>
#include <vector>


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
    template <typename ElementType, typename... Args>
    ElementType* Alloc(uint32 size, Args... args)
    {
        MemBlock& block = AllocateMemory(size)->Data;

        return static_cast<ElementType*>(block.Start);
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

        auto* node = GetNodeFromPtr(static_cast<void*>(ptr));
        if (node == nullptr) {
            Log::Error("FxMemPoolPage::Free: Could not find ptr %p in memory page!", ptr);
            return;
        }
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
    auto AllocateMemory(uint64 requested_size) -> FxMPLinkedList<FxMemPoolPage::MemBlock>::Node*;
    auto GetNodeFromPtr(void* ptr) const->FxMPLinkedList<FxMemPoolPage::MemBlock>::Node*;

    inline void CheckInited()
    {
        if (!IsInited()) {
            Create(64);
        }
    }

private:

    uint64 mSize = 0;
    uint8* mMem = nullptr;

    FxMPLinkedList<MemBlock> mMemBlocks;
};

class FxMemPool
{
public:
    FxMemPool() = default;

    static FxMemPool& GetGlobalPool();

    /**
     * Creates a new memory pool with paging.
     * @param page_size_kb The size of each page in kilobytes.
     */
    void Create(uint32 page_size_kb)
    {
        mPageSize = static_cast<uint64>(page_size_kb) * page_size_kb;

        AllocateNewPage();
    }

    /**
     * Allocates a raw memory block on a memory pool.
     * @param pool The memory pool to allocate the memory block from. If nullptr, the global pool is used.
     */
    static void* AllocRaw(uint32 size, FxMemPool* pool = nullptr);

    /**
     * Allocates a memory block on the global memory pool.
     * @param args The arguments to construct the object with.
     */
    template <typename Type, typename... Args>
    static Type* Alloc(uint32 size, Args&&... args)
    {
        Type* ptr = static_cast<Type*>(AllocRaw(size, nullptr));

        // Use a placement new if the object is constructable with the provided arguments
        if constexpr (std::is_constructible_v<Type, Args...>) {
            new (ptr) Type(std::forward<Args>(args)...);
        }
        else {
            printf("Not constructable %s\n", typeid(Type).name());
        }

        return ptr;
    }

    /**
     * Allocates memory on a given memory pool.
     * @param pool The memory pool to allocate the memory block from.
     * @param args The arguments to construct the object with.
     */
    template <typename Type, typename... Args>
    static Type* AllocInPool(uint32 size, FxMemPool* pool, Args&&... args)
    {
        Type* ptr = static_cast<Type*>(AllocRaw(size, pool));

        // Use a placement new if the object is constructable with the provided arguments
        if constexpr (std::is_constructible_v<Type, Args...>) {
            new (ptr) Type(std::forward<Args>(args)...);
        }

        return ptr;
    }

    /** Frees an allocated pointer on the global memory pool */
    static void FreeRaw(void* ptr, FxMemPool* pool = nullptr);

    template <typename Type>
    static void Free(Type* ptr, FxMemPool* pool = nullptr)
    {
        if constexpr (std::is_destructible_v<Type>) {
            ptr->~Type();
        }

        FreeRaw(static_cast<void*>(ptr), pool);
    }

private:

    auto AllocateMemory(uint64 requested_size) -> FxMPLinkedList<FxMemPoolPage::MemBlock>::Node*;

    FxMemPoolPage* FindPtrInPage(void* ptr);
    inline bool IsPtrInPage(void* ptr, FxMemPoolPage* page) const;

    void AllocateNewPage();

private:
    FxMemPoolPage* mCurrentPage = nullptr;
    uint64 mPageSize = 0;

    std::vector<FxMemPoolPage> mPoolPages;

};
