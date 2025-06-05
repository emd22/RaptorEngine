#pragma once

#include <Core/Types.hpp>
#include <Core/FxLinkedList.hpp>

#include <mutex>

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

    static FxMemPool& GetGlobalPool();

    /** Allocates a block of memory */
    template <typename PtrType> requires std::is_pointer_v<PtrType>
    PtrType AllocInPool(uint32 size)
    {
        MemBlock& block = AllocateMemory(size)->Data;

        return static_cast<PtrType>(block.Start);
    }
    /** Allocates memory on the global memory pool */
    static void* Alloc(uint32 size);

    template <typename T>
    static T* Alloc(uint32 size)
    {
        return static_cast<T*>(Alloc(size));
    }

    /** Frees an allocated pointer */
    template <typename ElementType>
    void FreeInPool(ElementType* ptr)
    {
        if (ptr == nullptr) {
            return;
        }
        std::lock_guard guard(mMemMutex);

        auto* node = GetNodeFromPtr(static_cast<void*>(ptr));
        mMemBlocks.DeleteNode(node);
    }

    /** Frees an allocated pointer on the global memory pool */
    static void Free(void* ptr);

    template <typename T>
    static void Free(T* ptr)
    {
        Free(static_cast<T*>(ptr));
    }

    void PrintAllocations() const;

    void Destroy();
    ~FxMemPool()
    {
        Destroy();
    }

    inline bool IsInited() const
    {
        return (mMem != nullptr && mSize > 0);
    }

private:
    auto AllocateMemory(uint64 requested_size) -> FxLinkedList<FxMemPool::MemBlock>::Node*;
    auto GetNodeFromPtr(void* ptr) const -> FxLinkedList<FxMemPool::MemBlock>::Node*;

    inline void CheckInited()
    {
        if (!IsInited()) {
            Create(64);
        }
    }

private:

    uint64 mSize = 0;
    uint8* mMem = nullptr;

    std::mutex mMemMutex;

    FxLinkedList<MemBlock> mMemBlocks;
};
