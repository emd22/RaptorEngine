#include "FxMemPool2.hpp"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Core/FxBitUtil.hpp>
#include <Core/FxPanic.hpp>
#include <Math/FxMathUtil.hpp>

#if defined(__cplusplus)
#define tlsf_decl inline
#else
#define tlsf_decl static
#endif

static constexpr uint32 scAlignment = 8;

struct BlockHeader;

FX_FORCE_INLINE static void* BlockToPtr(BlockHeader* block);
FX_FORCE_INLINE static const void* BlockToPtr(const BlockHeader* block);

static void* align_ptr(const void* ptr, size_t align);


/*
** Constants.
*/

/* Public constants: may be modified. */
enum tlsf_public
{
    /* log2 of number of linear subdivisions of block sizes. Larger
    ** values require more memory in the control structure. Values of
    ** 4 or 5 are typical.
    */
    SL_INDEX_COUNT_LOG2 = 5,
};

/* Private constants: do not modify. */
enum tlsf_private
{
    /*
    ** TODO: We can increase this to support larger sizes, at the expense
    ** of more overhead in the TLSF structure.
    */
    FL_INDEX_MAX = 32,

    SL_INDEX_COUNT = (1 << SL_INDEX_COUNT_LOG2),
    FL_INDEX_SHIFT = (SL_INDEX_COUNT_LOG2 + 3),
    FL_INDEX_COUNT = (FL_INDEX_MAX - FL_INDEX_SHIFT + 1),

    SMALL_BLOCK_SIZE = (1 << FL_INDEX_SHIFT),
};

/*
** Cast and min/max macros.
*/

#define tlsf_min(a, b) ((a) < (b) ? (a) : (b))
#define tlsf_max(a, b) ((a) > (b) ? (a) : (b))

/*
** Set assert macro, if it has not been provided by the user.
*/
#if !defined(tlsf_assert)
#define tlsf_assert assert
#endif

/*
** Static assertion mechanism.
*/

#define _tlsf_glue2(x, y)       x##y
#define _tlsf_glue(x, y)        _tlsf_glue2(x, y)
#define tlsf_static_assert(exp) typedef char _tlsf_glue(static_assert, __LINE__)[(exp) ? 1 : -1]

/* This code has been tested on 32- and 64-bit (LP/LLP) architectures. */
tlsf_static_assert(sizeof(int) * CHAR_BIT == 32);
tlsf_static_assert(sizeof(size_t) * CHAR_BIT >= 32);
tlsf_static_assert(sizeof(size_t) * CHAR_BIT <= 64);

/* SL_INDEX_COUNT must be <= number of bits in sl_bitmap's storage type. */
tlsf_static_assert(sizeof(unsigned int) * CHAR_BIT >= SL_INDEX_COUNT);

/* Ensure we've properly tuned our sizes. */
tlsf_static_assert(scAlignment == SMALL_BLOCK_SIZE / SL_INDEX_COUNT);

/*
** Data structures and associated constants.
*/


/*
** Since block sizes are always at least a multiple of 4, the two least
** significant bits of the size field are used to store the block status:
** - bit 0: whether block is busy or free
** - bit 1: whether previous block is busy or free
*/
static constexpr size_t scBlockFreeBit = 1 << 0;
static constexpr size_t scBlockPrevFreeBit = 1 << 1;


/*
** Block header structure.
**
** There are several implementation subtleties involved:
** - The prev_phys_block field is only valid if the previous block is free.
** - The prev_phys_block field is actually stored at the end of the
**   previous block. It appears at the beginning of this structure only to
**   simplify the implementation.
** - The next_free / prev_free fields are only valid if the block is free.
*/
struct alignas(16) BlockHeader
{
public:
    size_t GetSize() const { return Size & ~(scBlockFreeBit | scBlockPrevFreeBit); }
    void SetSize(size_t size)
    {
        const uint32 flags = Size & (scBlockFreeBit | scBlockPrevFreeBit);
        Size = size | flags;
    }

    bool IsLast() const { return GetSize() == 0; }
    bool IsFree() const { return static_cast<bool>(Size & scBlockFreeBit); }
    void SetFree() { Size |= scBlockFreeBit; }
    void SetUsed() { Size &= ~scBlockFreeBit; }
    bool IsPrevFree() const { return static_cast<bool>(Size & scBlockPrevFreeBit); }
    void SetPrevFree() { Size |= scBlockPrevFreeBit; }
    void SetPrevUsed() { Size &= ~scBlockPrevFreeBit; }

    BlockHeader* GetPrev()
    {
        tlsf_assert(IsPrevFree() && "previous block must be free");
        return pPrevPhysBlock;
    }

    /* Return location of next existing block. */
    BlockHeader* GetNext() const;

    /* Link a new block with its physical neighbor, return the neighbor. */
    BlockHeader* LinkToNextBlock()
    {
        BlockHeader* next = GetNext();
        next->pPrevPhysBlock = this;
        return next;
    }

    void MarkFree()
    {
        /* Link the block to the next block, first. */
        BlockHeader* next = LinkToNextBlock();

        next->SetPrevFree();
        SetFree();
    }

    void MarkUsed()
    {
        BlockHeader* next = GetNext();
        next->SetPrevUsed();
        SetUsed();
    }

    bool CanSplit(uint64 size) const { return GetSize() >= sizeof(BlockHeader) + size; }

    /* Split a block into two, the second of which is free. */
    BlockHeader* Split(uint64 size);

public:
    /* Points to the previous physical block. */
    BlockHeader* pPrevPhysBlock;

    /* The size of this block, excluding the block header. */
    size_t Size;

    /* Next and previous free blocks. */
    BlockHeader* pNextFree;
    BlockHeader* pPrevFree;
};


/*
** TLSF utility functions. In most cases, these are direct translations of
** the documentation found in the white paper.
*/

static void mapping_insert(size_t size, int* fli, int* sli)
{
    int fl, sl;
    if (size < SMALL_BLOCK_SIZE) {
        /* Store small blocks in first list. */
        fl = 0;
        sl = static_cast<int>(size) / (SMALL_BLOCK_SIZE / SL_INDEX_COUNT);
    }
    else {
        fl = FxBit::FindLastOne64(size);
        sl = static_cast<int32>(size >> (fl - SL_INDEX_COUNT_LOG2)) ^ (1 << SL_INDEX_COUNT_LOG2);
        fl -= (FL_INDEX_SHIFT - 1);
    }
    *fli = fl;
    *sli = sl;
}

/* This version rounds up to the next block size (for allocations) */
static void mapping_search(size_t size, int* fli, int* sli)
{
    if (size >= SMALL_BLOCK_SIZE) {
        const size_t round = (1 << (FxBit::FindLastOne64(size) - SL_INDEX_COUNT_LOG2)) - 1;
        size += round;
    }
    mapping_insert(size, fli, sli);
}

struct alignas(16) ControlBlock
{
public:
    /* Remove a given block from the free list. */
    void RemoveBlockFromFreeList(BlockHeader* block)
    {
        int fl, sl;
        mapping_insert(block->GetSize(), &fl, &sl);
        RemoveFreeBlock(block, fl, sl);
    }

    /* Insert a given block into the free list. */
    void AddBlockToFreeList(BlockHeader* block)
    {
        int fl, sl;
        mapping_insert(block->GetSize(), &fl, &sl);
        InsertFreeBlock(block, fl, sl);
    }

    /* Remove a free block from the free list.*/
    void RemoveFreeBlock(BlockHeader* block, int fl, int sl)
    {
        BlockHeader* prev = block->pPrevFree;
        BlockHeader* next = block->pNextFree;
        tlsf_assert(prev && "prev_free field can not be null");
        tlsf_assert(next && "next_free field can not be null");
        next->pPrevFree = prev;
        prev->pNextFree = next;

        /* If this block is the head of the free list, set new head. */
        if (ppBlocks[fl][sl] == block) {
            ppBlocks[fl][sl] = next;

            /* If the new head is null, clear the bitmap. */
            if (next == &NullBlock) {
                SlBitmap[fl] &= ~(1U << sl);

                /* If the second bitmap is now empty, clear the fl bitmap. */
                if (!SlBitmap[fl]) {
                    FlBitmap &= ~(1U << fl);
                }
            }
        }
    }

    /* Insert a free block into the free block list. */
    void InsertFreeBlock(BlockHeader* block, int fl, int sl)
    {
        BlockHeader* current = ppBlocks[fl][sl];
        tlsf_assert(current && "free list cannot have a null entry");
        tlsf_assert(block && "cannot insert a null entry into the free list");
        block->pNextFree = current;
        block->pPrevFree = &NullBlock;
        current->pPrevFree = block;

        void* btp = BlockToPtr(block);
        void* align_btp = FxMath::AlignPtr<void*, scAlignment>(BlockToPtr(block));

        tlsf_assert(btp == align_btp && "block not aligned properly");
        /*
        ** Insert the new block at the head of the list, and mark the first-
        ** and second-level bitmaps appropriately.
        */
        ppBlocks[fl][sl] = block;
        FlBitmap |= (1U << fl);
        SlBitmap[fl] |= (1U << sl);
    }

public:
    /* Empty lists point at this block to indicate they are free. */
    BlockHeader NullBlock;

    /* Bitmaps for free lists. */
    unsigned int FlBitmap;
    unsigned int SlBitmap[FL_INDEX_COUNT];

    /* Head of free lists. */
    BlockHeader* ppBlocks[FL_INDEX_COUNT][SL_INDEX_COUNT];
};
/*
** The size of the block header exposed to used blocks is the size field.
** The prev_phys_block field is stored *inside* the previous free block.
*/
static const size_t block_header_overhead = sizeof(size_t);

/* User data starts directly after the size field in a used block. */
static constexpr size_t scBlockStartOffset = offsetof(BlockHeader, Size) + sizeof(size_t);

/*
** A free block must be large enough to store its header minus the size of
** the prev_phys_block field, and no larger than the number of addressable
** bits for FL_INDEX.
*/
static const size_t block_size_min = sizeof(BlockHeader) - sizeof(BlockHeader*);
static const size_t block_size_max = 1llu << FL_INDEX_MAX;


/* A type used for casting when doing pointer arithmetic. */
typedef ptrdiff_t tlsfptr_t;

/*
** block_header_t member functions.
*/

FX_FORCE_INLINE static BlockHeader* BlockFromPtr(void* ptr)
{
    return reinterpret_cast<BlockHeader*>(reinterpret_cast<uint8*>(ptr) - scBlockStartOffset);
}

FX_FORCE_INLINE static const BlockHeader* BlockFromPtr(const void* ptr)
{
    return reinterpret_cast<const BlockHeader*>(reinterpret_cast<const uint8*>(ptr) - scBlockStartOffset);
}

FX_FORCE_INLINE static void* BlockToPtr(BlockHeader* block)
{
    return reinterpret_cast<void*>(reinterpret_cast<uint8*>(block) + scBlockStartOffset);
}

FX_FORCE_INLINE static const void* BlockToPtr(const BlockHeader* block)
{
    return reinterpret_cast<const void*>(reinterpret_cast<const uint8*>(block) + scBlockStartOffset);
}

/* Return location of next block after block of given size. */
static BlockHeader* GetBlock(const void* ptr, size_t size)
{
    return reinterpret_cast<BlockHeader*>(reinterpret_cast<uintptr_t>(ptr) + size);
}


static size_t align_up(size_t x, size_t align)
{
    tlsf_assert(0 == (align & (align - 1)) && "must align to a power of two");
    return (x + (align - 1)) & ~(align - 1);
}

static size_t align_down(size_t x, size_t align)
{
    tlsf_assert(0 == (align & (align - 1)) && "must align to a power of two");
    return x - (x & (align - 1));
}

static void* align_ptr(const void* ptr, size_t align)
{
    const uintptr_t aligned = (reinterpret_cast<uintptr_t>(ptr) + (align - 1)) & ~(align - 1);
    tlsf_assert(0 == (align & (align - 1)) && "must align to a power of two");
    return reinterpret_cast<void*>(aligned);
}

BlockHeader* BlockHeader::GetNext() const
{
    BlockHeader* next = GetBlock(BlockToPtr(this), GetSize() - block_header_overhead);
    tlsf_assert(!IsLast());
    return next;
}

/*
** Adjust an allocation size to be aligned to word size, and no smaller
** than internal minimum.
*/
static size_t adjust_request_size(size_t size, size_t align)
{
    size_t adjust = 0;
    if (size) {
        const size_t aligned = align_up(size, align);

        /* aligned sized must not exceed block_size_max or we'll go out of bounds on sl_bitmap */
        if (aligned < block_size_max) {
            adjust = tlsf_max(aligned, block_size_min);
        }
    }
    return adjust;
}


static BlockHeader* search_suitable_block(ControlBlock* control, int* fli, int* sli)
{
    int fl = *fli;
    int sl = *sli;

    /*
    ** First, search for a block in the list associated with the given
    ** fl/sl index.
    */
    uint32 sl_map = control->SlBitmap[fl] & (~0U << sl);

    if (!sl_map) {
        /* No block exists. Search in the next largest first-level list. */
        const uint32 fl_map = control->FlBitmap & (~0U << (fl + 1));

        if (!fl_map) {
            /* No free blocks available, memory has been exhausted. */
            return 0;
        }

        fl = FxBit::FindFirstOne32(fl_map);
        *fli = fl;
        sl_map = control->SlBitmap[fl];
    }

    tlsf_assert(sl_map && "internal error - second level bitmap is null");
    sl = FxBit::FindFirstOne32(sl_map);
    *sli = sl;

    /* Return the first block in the free list. */
    return control->ppBlocks[fl][sl];
}


BlockHeader* BlockHeader::Split(uint64 size)
{
    /* Calculate the amount of space left in the remaining block. */
    BlockHeader* remaining = GetBlock(BlockToPtr(this), size - block_header_overhead);

    const size_t remain_size = GetSize() - (size + block_header_overhead);

    tlsf_assert(BlockToPtr(remaining) == align_ptr(BlockToPtr(remaining), scAlignment) &&
                "remaining block not aligned properly");

    tlsf_assert(GetSize() == remain_size + size + block_header_overhead);
    remaining->SetSize(remain_size);
    tlsf_assert(remaining->GetSize() >= block_size_min && "block split with invalid size");

    SetSize(size);
    remaining->MarkFree();

    return remaining;
}


/* Absorb a free block's storage into an adjacent previous free block. */
static BlockHeader* block_absorb(BlockHeader* prev, BlockHeader* block)
{
    tlsf_assert(!prev->IsLast() && "previous block can't be last");
    /* Note: Leaves flags untouched. */
    prev->Size += block->GetSize() + block_header_overhead;
    prev->LinkToNextBlock();
    return prev;
}

/* Merge a just-freed block with an adjacent previous free block. */
static BlockHeader* block_merge_prev(ControlBlock* control, BlockHeader* block)
{
    if (block->IsPrevFree()) {
        BlockHeader* prev = block->GetPrev();
        tlsf_assert(prev && "prev physical block can't be null");
        tlsf_assert(prev->IsFree() && "prev block is not free though marked as such");
        control->RemoveBlockFromFreeList(prev);
        block = block_absorb(prev, block);
    }

    return block;
}

/* Merge a just-freed block with an adjacent free block. */
static BlockHeader* block_merge_next(ControlBlock* control, BlockHeader* block)
{
    BlockHeader* next = block->GetNext();
    tlsf_assert(next && "next physical block can't be null");

    if (next->IsFree()) {
        tlsf_assert(!(block->IsLast()) && "previous block can't be last");

        control->RemoveBlockFromFreeList(next);
        block = block_absorb(block, next);
    }

    return block;
}

/* Trim any trailing block space off the end of a block, return to pool. */
static void block_trim_free(ControlBlock* control, BlockHeader* block, size_t size)
{
    tlsf_assert((block->IsFree()) && "block must be free");
    if (block->CanSplit(size)) {
        BlockHeader* remaining_block = block->Split(size);
        block->LinkToNextBlock();

        remaining_block->SetPrevFree();
        control->AddBlockToFreeList(remaining_block);
    }
}

/* Trim any trailing block space off the end of a used block, return to pool. */
static void block_trim_used(ControlBlock* control, BlockHeader* block, size_t size)
{
    tlsf_assert(!(block->IsFree()) && "block must be used");
    if (block->CanSplit(size)) {
        /* If the next block is free, we must coalesce. */
        BlockHeader* remaining_block = block->Split(size);
        remaining_block->SetPrevUsed();

        remaining_block = block_merge_next(control, remaining_block);
        control->AddBlockToFreeList(remaining_block);
    }
}

static BlockHeader* block_trim_free_leading(ControlBlock* control, BlockHeader* block, size_t size)
{
    BlockHeader* remaining_block = block;
    if (block->CanSplit(size)) {
        /* We want the 2nd block. */
        remaining_block = block->Split(size - block_header_overhead);
        remaining_block->SetPrevFree();

        block->LinkToNextBlock();
        control->AddBlockToFreeList(block);
    }

    return remaining_block;
}

static BlockHeader* block_locate_free(ControlBlock* control, size_t size)
{
    int fl = 0, sl = 0;
    BlockHeader* block = 0;

    if (size) {
        mapping_search(size, &fl, &sl);

        /*
        ** mapping_search can futz with the size, so for excessively large sizes it can sometimes wind up
        ** with indices that are off the end of the block array.
        ** So, we protect against that here, since this is the only callsite of mapping_search.
        ** Note that we don't need to check sl, since it comes from a modulo operation that guarantees it's always in
        *range.
        */
        if (fl < FL_INDEX_COUNT) {
            block = search_suitable_block(control, &fl, &sl);
        }
    }

    if (block) {
        tlsf_assert((block->GetSize()) >= size);
        control->RemoveFreeBlock(block, fl, sl);
    }

    return block;
}

static void* block_prepare_used(ControlBlock* control, BlockHeader* block, size_t size)
{
    void* p = 0;
    if (block) {
        tlsf_assert(size && "size must be non-zero");
        block_trim_free(control, block, size);
        block->MarkUsed();
        p = BlockToPtr(block);
    }
    return p;
}

/* Clear structure and point all empty lists at the null block. */
static void MakeControlBlock(ControlBlock* control)
{
    control->NullBlock.pNextFree = &control->NullBlock;
    control->NullBlock.pPrevFree = &control->NullBlock;

    control->FlBitmap = 0;
    for (int i = 0; i < FL_INDEX_COUNT; ++i) {
        control->SlBitmap[i] = 0;
        for (int j = 0; j < SL_INDEX_COUNT; ++j) {
            control->ppBlocks[i][j] = &control->NullBlock;
        }
    }
}

/*
** Debugging utilities.
*/

typedef struct integrity_t
{
    int prev_status;
    int status;
} integrity_t;

#define tlsf_insist(x)                                                                                                 \
    {                                                                                                                  \
        tlsf_assert(x);                                                                                                \
        if (!(x)) {                                                                                                    \
            status--;                                                                                                  \
        }                                                                                                              \
    }

static void integrity_walker(void* ptr, size_t size, int used, void* user)
{
    BlockHeader* block = BlockFromPtr(ptr);
    integrity_t* integ = reinterpret_cast<integrity_t*>(user);
    const bool this_prev_status = block->IsPrevFree();
    const bool this_status = block->IsFree();

    const size_t this_block_size = block->GetSize();

    int status = 0;
    (void)used;
    tlsf_insist(integ->prev_status == this_prev_status && "prev status incorrect");
    tlsf_insist(size == this_block_size && "block size incorrect");

    integ->prev_status = this_status;
    integ->status += status;
}

int tlsf_check(tlsf_t tlsf)
{
    int i, j;

    ControlBlock* control = reinterpret_cast<ControlBlock*>(tlsf);
    int status = 0;

    /* Check that the free lists and bitmaps are accurate. */
    for (i = 0; i < FL_INDEX_COUNT; ++i) {
        for (j = 0; j < SL_INDEX_COUNT; ++j) {
            const int fl_map = control->FlBitmap & (1U << i);
            const int sl_list = control->SlBitmap[i];
            const int sl_map = sl_list & (1U << j);
            const BlockHeader* block = control->ppBlocks[i][j];

            /* Check that first- and second-level lists agree. */
            if (!fl_map) {
                tlsf_insist(!sl_map && "second-level map must be null");
            }

            if (!sl_map) {
                tlsf_insist(block == &control->NullBlock && "block list must be null");
                continue;
            }

            /* Check that there is at least one free block. */
            tlsf_insist(sl_list && "no free blocks in second-level map");
            tlsf_insist(block != &control->NullBlock && "block should not be null");

            while (block != &control->NullBlock) {
                int fli, sli;
                tlsf_insist(block->IsFree() && "block should be free");
                tlsf_insist(!block->IsPrevFree() && "blocks should have coalesced");
                tlsf_insist(!block->GetNext()->IsFree() && "blocks should have coalesced");
                tlsf_insist(block->GetNext()->IsPrevFree() && "block should be free");
                tlsf_insist(block->GetSize() >= block_size_min && "block not minimum size");

                mapping_insert(block->GetSize(), &fli, &sli);
                tlsf_insist(fli == i && sli == j && "block size indexed in wrong list");
                block = block->pNextFree;
            }
        }
    }

    return status;
}

#undef tlsf_insist

static void default_walker(void* ptr, size_t size, int used, void* user)
{
    (void)user;
    printf("\t%p %s size: %x (%p)\n", ptr, used ? "used" : "free", (unsigned int)size, BlockFromPtr(ptr));
}

void tlsf_walk_pool(pool_t pool, tlsf_walker walker, void* user)
{
    tlsf_walker pool_walker = walker ? walker : default_walker;
    BlockHeader* block = GetBlock(pool, -(int)block_header_overhead);

    while (block && !block->IsLast()) {
        pool_walker(BlockToPtr(block), block->GetSize(), !block->IsFree(), user);
        block = block->GetNext();
    }
}

size_t tlsf_block_size(void* ptr)
{
    size_t size = 0;
    if (ptr) {
        const BlockHeader* block = BlockFromPtr(ptr);
        size = block->GetSize();
    }
    return size;
}

int tlsf_check_pool(pool_t pool)
{
    /* Check that the blocks are physically correct. */
    integrity_t integ = { 0, 0 };
    tlsf_walk_pool(pool, integrity_walker, &integ);

    return integ.status;
}


size_t tlsf_scAlignment(void) { return scAlignment; }

size_t tlsf_block_size_min(void) { return block_size_min; }

size_t tlsf_block_size_max(void) { return block_size_max; }

/*
** Overhead of the TLSF structures in a given memory block passed to
** tlsf_add_pool, equal to the overhead of a free block and the
** sentinel block.
*/
static constexpr size_t tlsf_pool_overhead(void) { return 4 * block_header_overhead; }

static constexpr size_t tlsf_alloc_overhead(void) { return block_header_overhead; }

pool_t FxMemPool2::AddPool(void* mem, size_t bytes)
{
    BlockHeader* block;
    BlockHeader* next;


    const size_t pool_overhead = tlsf_pool_overhead();
    const size_t pool_bytes = align_down(bytes - pool_overhead, scAlignment);

    if (((ptrdiff_t)mem % scAlignment) != 0) {
        printf("tlsf_add_pool: Memory must be aligned by %u bytes.\n", (unsigned int)scAlignment);
        return 0;
    }

    FxLogInfo("Pool bytes: {}, BSM: {}, BSMX: {}", pool_bytes, block_size_min, block_size_max);

    if (pool_bytes < block_size_min || pool_bytes > block_size_max) {
        printf("tlsf_add_pool: Memory size must be between 0x%x and 0x%x00 bytes.\n",
               (unsigned int)(pool_overhead + block_size_min), (unsigned int)((pool_overhead + block_size_max) / 256));

        return 0;
    }

    /*
    ** Create the main free block. Offset the start of the block slightly
    ** so that the prev_phys_block field falls outside of the pool -
    ** it will never be used.
    */
    block = GetBlock(mem, -block_header_overhead);

    block->SetSize(pool_bytes);
    block->SetFree();
    block->SetPrevUsed();

    GetControlBlock()->AddBlockToFreeList(block);

    /* Split the block to create a zero-size sentinel block. */
    next = block->LinkToNextBlock();
    next->SetSize(0);
    next->SetUsed();

    next->SetPrevFree();

    return mem;
}

void FxMemPool2::RemovePool(pool_t pool)
{
    ControlBlock* control = GetControlBlock();
    BlockHeader* block = GetBlock(pool, -(int)block_header_overhead);

    int fl = 0, sl = 0;

    tlsf_assert((block->IsFree()) && "block should be free");
    tlsf_assert(!block->GetNext()->IsFree() && "next block should not be free");
    tlsf_assert(block->GetNext()->GetSize() == 0 && "next block size should be zero");

    mapping_insert((block->GetSize()), &fl, &sl);
    control->RemoveFreeBlock(block, fl, sl);
}

/*
** TLSF main interface.
*/

#if _DEBUG
int test_ffs_fls()
{
    /* Verify ffs/fls work properly. */
    int rv = 0;
    rv += (tlsf_ffs(0) == -1) ? 0 : 0x1;
    rv += (tlsf_fls(0) == -1) ? 0 : 0x2;
    rv += (tlsf_ffs(1) == 0) ? 0 : 0x4;
    rv += (tlsf_fls(1) == 0) ? 0 : 0x8;
    rv += (tlsf_ffs(0x80000000) == 31) ? 0 : 0x10;
    rv += (tlsf_ffs(0x80008000) == 15) ? 0 : 0x20;
    rv += (tlsf_fls(0x80000008) == 31) ? 0 : 0x40;
    rv += (tlsf_fls(0x7FFFFFFF) == 30) ? 0 : 0x80;

    rv += (tlsf_fls_sizet(0x80000000) == 31) ? 0 : 0x100;
    rv += (tlsf_fls_sizet(0x100000000) == 32) ? 0 : 0x200;
    rv += (tlsf_fls_sizet(0xffffffffffffffff) == 63) ? 0 : 0x400;

    if (rv) {
        printf("test_ffs_fls: %x ffs/fls tests failed.\n", rv);
    }
    return rv;
}
#endif

void FxMemPool2::Create(uint64 size)
{
    void* ptr = std::malloc(size);
    FxAssertMsg(ptr != nullptr, "Could not allocate memory pool!");

    CreateFromPtr(ptr);
    AddPool(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) + sizeof(ControlBlock)),
            size - sizeof(ControlBlock));

    pMemory = ptr;
}

tlsf_t FxMemPool2::CreateFromPtr(void* allocated_buffer)
{
#if _DEBUG
    if (test_ffs_fls()) {
        return 0;
    }
#endif

    if ((reinterpret_cast<ptrdiff_t>(allocated_buffer) % scAlignment) != 0) {
        printf("tlsf_create: Memory must be aligned to %u bytes.\n", (unsigned int)scAlignment);
        return 0;
    }

    MakeControlBlock(reinterpret_cast<ControlBlock*>(allocated_buffer));
    pMemory = reinterpret_cast<tlsf_t>(allocated_buffer);

    return pMemory;
}

tlsf_t FxMemPool2::CreateWithPool(void* mem, size_t bytes)
{
    CreateFromPtr(mem);
    AddPool((char*)mem + sizeof(ControlBlock), bytes - sizeof(ControlBlock));
    return pMemory;
}

ControlBlock* FxMemPool2::GetControlBlock() { return reinterpret_cast<ControlBlock*>(pMemory); }


pool_t tlsf_get_pool(tlsf_t tlsf)
{
    return reinterpret_cast<pool_t>(reinterpret_cast<uint8*>(tlsf) + sizeof(ControlBlock));
}

void* FxMemPool2::Alloc(size_t size)
{
    ControlBlock* control = GetControlBlock();

    const size_t adjust = adjust_request_size(size, scAlignment);

    BlockHeader* block = block_locate_free(control, adjust);
    return block_prepare_used(control, block, adjust);
}

void* FxMemPool2::AlignedAlloc(size_t align, size_t size)
{
    ControlBlock* control = GetControlBlock();

    const size_t adjust = adjust_request_size(size, scAlignment);

    /*
    ** We must allocate an additional minimum block size bytes so that if
    ** our free block will leave an alignment gap which is smaller, we can
    ** trim a leading free block and release it back to the pool. We must
    ** do this because the previous physical block is in use, therefore
    ** the prev_phys_block field is not valid, and we can't simply adjust
    ** the size of that block.
    */
    const size_t gap_minimum = sizeof(BlockHeader);
    const size_t size_with_gap = adjust_request_size(adjust + align + gap_minimum, align);

    /*
    ** If alignment is less than or equals base alignment, we're done.
    ** If we requested 0 bytes, return null, as tlsf_malloc(0) does.
    */
    const size_t aligned_size = (adjust && align > scAlignment) ? size_with_gap : adjust;

    BlockHeader* block = block_locate_free(control, aligned_size);

    /* This can't be a static assert. */
    tlsf_assert(sizeof(BlockHeader) == block_size_min + block_header_overhead);

    if (block) {
        void* ptr = BlockToPtr(block);
        void* aligned = align_ptr(ptr, align);
        size_t gap = reinterpret_cast<size_t>(reinterpret_cast<uintptr_t>(aligned) - reinterpret_cast<uintptr_t>(ptr));

        /* If gap size is too small, offset to next aligned boundary. */
        if (gap && gap < gap_minimum) {
            const size_t gap_remain = gap_minimum - gap;
            const size_t offset = tlsf_max(gap_remain, align);
            const void* next_aligned = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(aligned) + offset);

            aligned = align_ptr(next_aligned, align);
            gap = reinterpret_cast<size_t>(reinterpret_cast<uintptr_t>(aligned) - reinterpret_cast<uintptr_t>(ptr));
        }

        if (gap) {
            tlsf_assert(gap >= gap_minimum && "gap size too small");
            block = block_trim_free_leading(control, block, gap);
        }
    }

    return block_prepare_used(control, block, adjust);
}

void FxMemPool2::Free(void* ptr)
{
    if (!ptr) {
        return;
    }

    ControlBlock* control = GetControlBlock();

    BlockHeader* block = BlockFromPtr(ptr);
    tlsf_assert(!(block->IsFree()) && "block already marked as free");
    block->MarkFree();
    block = block_merge_prev(control, block);
    block = block_merge_next(control, block);
    control->AddBlockToFreeList(block);
}

/*
** The TLSF block information provides us with enough information to
** provide a reasonably intelligent implementation of realloc, growing or
** shrinking the currently allocated block as required.
**
** This routine handles the somewhat esoteric edge cases of realloc:
** - a non-zero size with a null pointer will behave like malloc
** - a zero size with a non-null pointer will behave like free
** - a request that cannot be satisfied will leave the original buffer
**   untouched
** - an extended buffer size will leave the newly-allocated area with
**   contents undefined
*/
void* FxMemPool2::Realloc(void* ptr, size_t size)
{
    ControlBlock* control = GetControlBlock();
    void* p = 0;

    /* Zero-size requests are treated as free. */
    if (ptr && size == 0) {
        Free(ptr);
    }
    /* Requests with NULL pointers are treated as malloc. */
    else if (!ptr) {
        p = Alloc(size);
    }
    else {
        BlockHeader* block = BlockFromPtr(ptr);
        BlockHeader* next = block->GetNext();

        const size_t cursize = block->GetSize();
        const size_t combined = cursize + next->GetSize() + block_header_overhead;
        const size_t adjust = adjust_request_size(size, scAlignment);

        tlsf_assert(!(block->IsFree()) && "block already marked as free");

        /*
        ** If the next block is used, or when combined with the current
        ** block, does not offer enough space, we must reallocate and copy.
        */
        if (adjust > cursize && (!next->IsFree() || adjust > combined)) {
            p = Alloc(size);
            if (p) {
                const size_t minsize = tlsf_min(cursize, size);
                memcpy(p, ptr, minsize);
                Free(ptr);
            }
        }
        else {
            /* Do we need to expand to the next block? */
            if (adjust > cursize) {
                block_merge_next(control, block);
                block->MarkUsed();
            }

            /* Trim the resulting block and return the original pointer. */
            block_trim_used(control, block, adjust);
            p = ptr;
        }
    }

    return p;
}
