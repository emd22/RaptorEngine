#include "FxMemPool.hpp"
#include "FxPanic.hpp"

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
			auto* new_node = mMemBlocks.InsertAfterNode(new_block, node);

			return new_node;
		}

		node = node->Next;
	}

	// There are no gaps between the allocated blocks, create a new block.
	const FxMemPool::MemBlock& last_block = mMemBlocks.Tail->Data;
	uint8* last_allocated_ptr = last_block.Start + last_block.Size;

	// Check to make sure there is space left in the pool
	if ((last_allocated_ptr - mMem) > mSize) {
		FxPanic("FxMemPool", "Could not resize memory pool! (Not implemented)", 0);
		return nullptr;
	}

	// Create a new block entry
	FxMemPool::MemBlock new_block{
		.Size = GetAlignedValue(requested_size),
		.Start = GetAlignedPtr(last_allocated_ptr)
	};

	auto* new_node = mMemBlocks.InsertLast(new_block);

	return new_node;
}

auto FxMemPool::GetNodeFromPtr(void* ptr) -> FxLinkedList<FxMemPool::MemBlock>::Node*
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
