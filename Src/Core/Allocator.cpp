#include "Allocator.hpp"

#include <mimalloc.h>

#include <cstdlib>

namespace fx {


/////////////////////////////////////
// Standard Library allocator
/////////////////////////////////////

void* StdAllocator::AllocRaw(size_t size) { return std::malloc(size); }
void* StdAllocator::ReallocRaw(void* ptr, size_t size) { return std::realloc(ptr, size); }
void StdAllocator::FreeRaw(void* ptr) { std::free(ptr); }

/////////////////////////////////////
// mimalloc heaps
/////////////////////////////////////

static mi_heap_t* sHeaps[static_cast<size_t>(eMemHeap::Count)] = {};

static mi_heap_t* GetHeap(eMemHeap heap)
{
    mi_heap_t*& slot = sHeaps[static_cast<size_t>(heap)];
    if (slot == nullptr) {
        slot = mi_heap_new();
    }
    return slot;
}

void* MemHeaps::AllocRaw(eMemHeap heap, size_t size) { return mi_heap_malloc(GetHeap(heap), size); }
void* MemHeaps::ReallocRaw(eMemHeap heap, void* ptr, size_t size) { return mi_heap_realloc(GetHeap(heap), ptr, size); }
void MemHeaps::FreeRaw(void* ptr) { mi_free(ptr); }

void MemHeaps::Collect(eMemHeap heap, bool force) { mi_heap_collect(GetHeap(heap), force); }

void MemHeaps::Reset(eMemHeap heap)
{
    mi_heap_t*& slot = sHeaps[static_cast<size_t>(heap)];
    if (slot != nullptr) {
        mi_heap_destroy(slot);
        slot = nullptr;
    }
}

void MemHeaps::Destroy()
{
    for (size_t i = 0; i < static_cast<size_t>(eMemHeap::Count); i++) {
        Reset(static_cast<eMemHeap>(i));
    }
}

} // namespace fx
