#include "FxMemPool.hpp"
#include "FxPanic.hpp"
#include <mutex>

static FxMemPool GlobalPool;

void FxMemPool::Create(uint32 size_kb)
{
	mSize = static_cast<uint64>(size_kb) * 1024;

	void* allocated_ptr = std::malloc(mSize);
	if (allocated_ptr == nullptr) {
		FxPanic("FxMemPool", "Error allocating buffer for memory pool!", 0);
	}

	mMem = static_cast<uint8 *>(allocated_ptr);

	mMemBlocks.Create(64);
}

FxMemPool& FxMemPool::GetGlobalPool()
{
	return GlobalPool;
}

static inline uint64 GetAlignedValue(uint64 value)
{
	// Get if the value is aligned per 8 bytes
	uint64 align_value = (value & 0x07);

	// If the value is not aligned, offset our value
	if (align_value) {
		value += (8 - align_value);
	}

	return value;
}
static inline uint8* GetAlignedPtr(uint8* ptr)
{
	return reinterpret_cast<uint8*>(GetAlignedValue(reinterpret_cast<uint64>(ptr)));
}


auto FxMemPool::AllocateMemory(uint64 requested_size) -> FxLinkedList<FxMemPool::MemBlock>::Node*
{

	auto* node = mMemBlocks.Head;

	// Check to see if there is space at the start of the list
	if (node && node == mMemBlocks.Head && node->Data.Start != mMem) {
		uint64 gap_size = (node->Data.Start) - mMem;

		if (gap_size >= GetAlignedValue(requested_size)) {
			FxMemPool::MemBlock new_block{
				.Size = GetAlignedValue(requested_size),
				.Start = mMem
			};

			return mMemBlocks.InsertBeforeNode(new_block, node);
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
		new_block_ptr = last_block.Start + last_block.Size;

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
	GlobalPool.CheckInited();

	std::lock_guard<std::mutex> guard(GlobalPool.mMemMutex);

	MemBlock& block = GlobalPool.AllocateMemory(size)->Data;

	return static_cast<void*>(block.Start);
}

void FxMemPool::Free(void* ptr)
{
    if (ptr == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> guard(GlobalPool.mMemMutex);

	auto* node = GlobalPool.GetNodeFromPtr(ptr);
	GlobalPool.mMemBlocks.DeleteNode(node);
}


void FxMemPool::PrintAllocations() const
{
	auto* node = mMemBlocks.Head;

	Log::Debug("");
	Log::Debug("=== Memory Pool [size=%llu, start=%p] ===", mSize, mMemBlocks.Head);

	while (node != nullptr) {
		FxMemPool::MemBlock& block = node->Data;

		if (node->Prev) {
			FxMemPool::MemBlock& prev_block = node->Prev->Data;

			printf("prevblk=%p, sz: %llu\n", prev_block.Start, prev_block.Size);
			printf("curblk=%p, sz: %llu\n", block.Start, block.Size);

			uint64 gap_size = block.Start - (prev_block.Start + prev_block.Size);
			if (gap_size) {
				Log::Debug("MemGap  [size=%llu, ptr=%p]", gap_size, prev_block.Start + prev_block.Size);
			}
		}
		else if (node == mMemBlocks.Head && node->Data.Start != mMem) {
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
    std::lock_guard<std::mutex> guard(mMemMutex);
	std::free(mMem);
	mSize = 0;
}
