#pragma once

#include "../Types.hpp"

#include "FxMPLinkedList.hpp"

#include <type_traits>
// #include <vector>

#define FX_MEMPOOL_USE_ATOMIC_LOCKING 1

#ifdef FX_MEMPOOL_USE_ATOMIC_LOCKING
#include <atomic>
#endif

#if !defined(FX_MEMPOOL_USE_ATOMIC_LOCKING) && !defined(FX_MEMPOOL_DEBUG_CHECK_THREAD_OWNERSHIP)
#define FX_MEMPOOL_DEBUG_CHECK_THREAD_OWNERSHIP
#endif

#ifdef FX_MEMPOOL_DEBUG_CHECK_THREAD_OWNERSHIP
#include <thread>
#include <iostream>
#endif

constexpr uint64 FxUnitByte = 1;
constexpr uint64 FxUnitKibibyte = 1024;
constexpr uint64 FxUnitMebibyte = FxUnitKibibyte * 1024;
constexpr uint64 FxUnitGibibyte = FxUnitMebibyte * 1024;

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
    #ifdef FX_MEMPOOL_USE_ATOMIC_LOCKING
        FxSpinThreadGuard guard(&mInUse);
    #endif

        MemBlock& block = AllocateMemory(size)->Data;

        return static_cast<ElementType*>(block.Start);
    }

    void* Realloc(void* ptr, uint32 new_size);

    template <typename Type>
    Type* Realloc(Type* ptr, uint32 new_size)
    {
    #ifdef FX_MEMPOOL_USE_ATOMIC_LOCKING
        FxSpinThreadGuard guard(&mInUse);
    #endif
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
        
    #ifdef FX_MEMPOOL_USE_ATOMIC_LOCKING
        FxSpinThreadGuard guard(&mInUse);
    #endif

        // Do not mark as a freed block when we are at the end of the page as it is ambiguous
        // on whether the end is a previously freed block or has not been previously allocated.
        // We don't want to waste cycles on pages that have nothing to give us!

        // There is a check in `HasFreeSpace()` that covers the case of freed space at the end.

        if (node != mMemBlocks.Tail) {
            mFreedTotalSize += node->Data.Size;
        }

        mMemBlocks.DeleteNode(node);
    }

    template <bool OnlyCheckFreedBlocks = false>
    bool HasFreeSpace() const
    {
        if (mFreedTotalSize > 0) {
            return true;
        }
        // For `AllocateMemory()` where it already tests for free memory at the end. We can
        // skip the extraneous checks
        if constexpr (OnlyCheckFreedBlocks) {
            return false;
        }

        const auto& final_block = mMemBlocks.Tail->Data;
        uint64 current_page_size = static_cast<uint64>((final_block.Start + final_block.Size) - mMem);

        if (current_page_size < mSize) {
            return true;
        }

        return false;
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
            FxPanic("FxMemoryPage", "Page has not been initialized!", 0);
        }
    }

private:
    /**
     * @brief The allocated capacity of the page
     */
    uint64 mSize = 0;

    /**
     * @brief The total amount of bytes that have been freed in the page
     */
    int64 mFreedTotalSize = 0;

    /**
     * @brief The page's allocated memory
     */
    uint8* mMem = nullptr;

    FxMPLinkedList<MemBlock> mMemBlocks;

    mutable FxAtomicFlag mInUse{ };
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
    void Create(uint64 page_size, uint64 size_unit)
    {
        FxSpinThreadGuard guard(&mInUse);
        {
            
            mPageSize = page_size * size_unit;
            
            mPoolPages.Create(8);
    #ifdef FX_MEMPOOL_DEBUG_CHECK_THREAD_OWNERSHIP
            mCreatedThreadId = std::this_thread::get_id();
    #endif
        }
        
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
        if (pool == nullptr) {
            pool = &GetGlobalPool();
        }
            
#ifdef FX_MEMPOOL_DEBUG_CHECK_THREAD_OWNERSHIP
        const auto& this_id = std::this_thread::get_id();
        
        if (this_id != pool->mCreatedThreadId) {
            Log::Warning("Attempting to free memory from a different thread!");
            std::cout << "Thread ids: " << pool->mCreatedThreadId << ", " << std::this_thread::get_id() << "\n";
        }
#endif
        if constexpr (std::is_destructible_v<Type>) {
            ptr->~Type();
        }
        FxSpinThreadGuard guard(&pool->mInUse);
        FreeRaw(static_cast<void*>(ptr), pool);
    }

    static void PrintAllocations();

private:
    auto AllocateMemory(uint64 requested_size) -> FxMPLinkedList<FxMemPoolPage::MemBlock>::Node*;

    FxMemPoolPage* FindPtrInPage(void* ptr);
    inline bool IsPtrInPage(void* ptr, FxMemPoolPage* page) const;

    void AllocateNewPage();

private:
    FxMemPoolPage* mCurrentPage = nullptr;
    uint64 mPageSize = 0;

    FxMPPagedArray<FxMemPoolPage> mPoolPages;
    
    FxAtomicFlag mInUse{};

#ifdef FX_MEMPOOL_DEBUG_CHECK_THREAD_OWNERSHIP
    std::thread::id mCreatedThreadId;
#endif
};
