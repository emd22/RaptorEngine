#include "FxMemPool2.hpp"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Core/FxBitUtil.hpp>
#include <Core/FxPanic.hpp>

#if defined(__cplusplus)
#define tlsf_decl inline
#else
#define tlsf_decl static
#endif

static constexpr uint32 scAlignment = 16;


// /*
// ** Architecture-specific bit manipulation routines.
// **
// ** TLSF achieves O(1) cost for malloc and free operations by limiting
// ** the search for a free block to a free list of guaranteed size
// ** adequate to fulfill the request, combined with efficient free list
// ** queries using bitmasks and architecture-specific bit-manipulation
// ** routines.
// **
// ** Most modern processors provide instructions to count leading zeroes
// ** in a word, find the lowest and highest set bit, etc. These
// ** specific implementations will be used when available, falling back
// ** to a reasonably efficient generic implementation.
// **
// ** NOTE: TLSF spec relies on ffs/fls returning value 0..31.
// ** ffs/fls return 1-32 by default, returning 0 for error.
// */

// /*
// ** gcc 3.4 and above have builtin support, specialized for architecture.
// ** Some compilers masquerade as gcc; patchlevel test filters them out.
// */
// #if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)) && defined(__GNUC_PATCHLEVEL__)

// #if defined(__SNC__)
// /* SNC for Playstation 3. */

// tlsf_decl int tlsf_ffs(unsigned int word)
// {
//     const unsigned int reverse = word & (~word + 1);
//     const int bit = 32 - __builtin_clz(reverse);
//     return bit - 1;
// }

// #else

// tlsf_decl int tlsf_ffs(unsigned int word) { return __builtin_ffs(word) - 1; }

// #endif

// tlsf_decl int tlsf_fls(unsigned int word)
// {
//     const int bit = word ? 32 - __builtin_clz(word) : 0;
//     return bit - 1;
// }

// #elif defined(_MSC_VER) && (_MSC_VER >= 1400) && (defined(_M_IX86) || defined(_M_X64))
// /* Microsoft Visual C++ support on x86/X64 architectures. */

// #include <intrin.h>


// tlsf_decl int tlsf_fls(unsigned int word)
// {
//     unsigned long index;
//     return _BitScanReverse(&index, word) ? index : -1;
// }

// tlsf_decl int tlsf_ffs(unsigned int word)
// {
//     unsigned long index;
//     return _BitScanForward(&index, word) ? index : -1;
// }

// #elif defined(_MSC_VER) && defined(_M_PPC)
// /* Microsoft Visual C++ support on PowerPC architectures. */

// #include <ppcintrinsics.h>

// tlsf_decl int tlsf_fls(unsigned int word)
// {
//     const int bit = 32 - _CountLeadingZeros(word);
//     return bit - 1;
// }

// tlsf_decl int tlsf_ffs(unsigned int word)
// {
//     const unsigned int reverse = word & (~word + 1);
//     const int bit = 32 - _CountLeadingZeros(reverse);
//     return bit - 1;
// }

// #elif defined(__ARMCC_VERSION)
// /* RealView Compilation Tools for ARM */

// tlsf_decl int tlsf_ffs(unsigned int word)
// {
//     const unsigned int reverse = word & (~word + 1);
//     const int bit = 32 - __clz(reverse);
//     return bit - 1;
// }

// tlsf_decl int tlsf_fls(unsigned int word)
// {
//     const int bit = word ? 32 - __clz(word) : 0;
//     return bit - 1;
// }

// #elif defined(__ghs__)
// /* Green Hills support for PowerPC */

// #include <ppc_ghs.h>

// tlsf_decl int tlsf_ffs(unsigned int word)
// {
//     const unsigned int reverse = word & (~word + 1);
//     const int bit = 32 - __CLZ32(reverse);
//     return bit - 1;
// }

// tlsf_decl int tlsf_fls(unsigned int word)
// {
//     const int bit = word ? 32 - __CLZ32(word) : 0;
//     return bit - 1;
// }

// #else
// /* Fall back to generic implementation. */

// tlsf_decl int tlsf_fls_generic(unsigned int word)
// {
//     int bit = 32;

//     if (!word)
//         bit -= 1;
//     if (!(word & 0xffff0000)) {
//         word <<= 16;
//         bit -= 16;
//     }
//     if (!(word & 0xff000000)) {
//         word <<= 8;
//         bit -= 8;
//     }
//     if (!(word & 0xf0000000)) {
//         word <<= 4;
//         bit -= 4;
//     }
//     if (!(word & 0xc0000000)) {
//         word <<= 2;
//         bit -= 2;
//     }
//     if (!(word & 0x80000000)) {
//         word <<= 1;
//         bit -= 1;
//     }

//     return bit;
// }

// /* Implement ffs in terms of fls. */
// tlsf_decl int tlsf_ffs(unsigned int word) { return tlsf_fls_generic(word & (~word + 1)) - 1; }

// tlsf_decl int tlsf_fls(unsigned int word) { return tlsf_fls_generic(word) - 1; }

// #endif

// /* Possibly 64-bit version of tlsf_fls. */
// tlsf_decl int tlsf_fls_sizet(size_t size)
// {
//     int high = (int)(size >> 32);
//     int bits = 0;
//     if (high) {
//         bits = 32 + tlsf_fls(high);
//     }
//     else {
//         bits = tlsf_fls((int)size & 0xffffffff);
//     }
//     return bits;
// }

#undef tlsf_decl

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
    FL_INDEX_SHIFT = (SL_INDEX_COUNT_LOG2 + 4),
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
** Block header structure.
**
** There are several implementation subtleties involved:
** - The prev_phys_block field is only valid if the previous block is free.
** - The prev_phys_block field is actually stored at the end of the
**   previous block. It appears at the beginning of this structure only to
**   simplify the implementation.
** - The next_free / prev_free fields are only valid if the block is free.
*/
struct BlockHeader
{
    /* Points to the previous physical block. */
    BlockHeader* pPrevPhysBlock;

    /* The size of this block, excluding the block header. */
    size_t Size;

    /* Next and previous free blocks. */
    BlockHeader* pNextFree;
    BlockHeader* pPrevFree;
};

/*
** Since block sizes are always at least a multiple of 4, the two least
** significant bits of the size field are used to store the block status:
** - bit 0: whether block is busy or free
** - bit 1: whether previous block is busy or free
*/
static constexpr size_t scBlockFreeBit = 1 << 0;
static constexpr size_t scBlockPrevFreeBit = 1 << 1;

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


struct alignas(16) ControlBlock
{
    /* Empty lists point at this block to indicate they are free. */
    BlockHeader NullBlock;

    /* Bitmaps for free lists. */
    unsigned int FlBitmap;
    unsigned int SlBitmap[FL_INDEX_COUNT];

    /* Head of free lists. */
    BlockHeader* ppBlocks[FL_INDEX_COUNT][SL_INDEX_COUNT];
};

/* A type used for casting when doing pointer arithmetic. */
typedef ptrdiff_t tlsfptr_t;

/*
** block_header_t member functions.
*/

static size_t block_size(const BlockHeader* block) { return block->Size & ~(scBlockFreeBit | scBlockPrevFreeBit); }

static void block_set_size(BlockHeader* block, size_t size)
{
    const size_t oldsize = block->Size;
    block->Size = size | (oldsize & (scBlockFreeBit | scBlockPrevFreeBit));
}

static int block_is_last(const BlockHeader* block) { return block_size(block) == 0; }

static int block_is_free(const BlockHeader* block) { return static_cast<int32>(block->Size & scBlockFreeBit); }

static void block_set_free(BlockHeader* block) { block->Size |= scBlockFreeBit; }

static void block_set_used(BlockHeader* block) { block->Size &= ~scBlockFreeBit; }

static int block_is_prev_free(const BlockHeader* block) { return static_cast<int32>(block->Size & scBlockPrevFreeBit); }

static void block_set_prev_free(BlockHeader* block) { block->Size |= scBlockPrevFreeBit; }

static void block_set_prev_used(BlockHeader* block) { block->Size &= ~scBlockPrevFreeBit; }

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

/* Return location of previous block. */
static BlockHeader* block_prev(const BlockHeader* block)
{
    tlsf_assert(block_is_prev_free(block) && "previous block must be free");
    return block->pPrevPhysBlock;
}

/* Return location of next existing block. */
static BlockHeader* block_next(const BlockHeader* block)
{
    BlockHeader* next = GetBlock(BlockToPtr(block), block_size(block) - block_header_overhead);
    tlsf_assert(!block_is_last(block));
    return next;
}

/* Link a new block with its physical neighbor, return the neighbor. */
static BlockHeader* block_link_next(BlockHeader* block)
{
    BlockHeader* next = block_next(block);
    next->pPrevPhysBlock = block;
    return next;
}

static void block_mark_as_free(BlockHeader* block)
{
    /* Link the block to the next block, first. */
    BlockHeader* next = block_link_next(block);
    block_set_prev_free(next);
    block_set_free(block);
}

static void block_mark_as_used(BlockHeader* block)
{
    BlockHeader* next = block_next(block);
    block_set_prev_used(next);
    block_set_used(block);
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

/* Remove a free block from the free list.*/
static void remove_free_block(ControlBlock* control, BlockHeader* block, int fl, int sl)
{
    BlockHeader* prev = block->pPrevFree;
    BlockHeader* next = block->pNextFree;
    tlsf_assert(prev && "prev_free field can not be null");
    tlsf_assert(next && "next_free field can not be null");
    next->pPrevFree = prev;
    prev->pNextFree = next;

    /* If this block is the head of the free list, set new head. */
    if (control->ppBlocks[fl][sl] == block) {
        control->ppBlocks[fl][sl] = next;

        /* If the new head is null, clear the bitmap. */
        if (next == &control->NullBlock) {
            control->SlBitmap[fl] &= ~(1U << sl);

            /* If the second bitmap is now empty, clear the fl bitmap. */
            if (!control->SlBitmap[fl]) {
                control->FlBitmap &= ~(1U << fl);
            }
        }
    }
}

/* Insert a free block into the free block list. */
static void insert_free_block(ControlBlock* control, BlockHeader* block, int fl, int sl)
{
    BlockHeader* current = control->ppBlocks[fl][sl];
    tlsf_assert(current && "free list cannot have a null entry");
    tlsf_assert(block && "cannot insert a null entry into the free list");
    block->pNextFree = current;
    block->pPrevFree = &control->NullBlock;
    current->pPrevFree = block;

    tlsf_assert(block_to_ptr(block) == align_ptr(block_to_ptr(block), scAlignment) && "block not aligned properly");
    /*
    ** Insert the new block at the head of the list, and mark the first-
    ** and second-level bitmaps appropriately.
    */
    control->ppBlocks[fl][sl] = block;
    control->FlBitmap |= (1U << fl);
    control->SlBitmap[fl] |= (1U << sl);
}

/* Remove a given block from the free list. */
static void block_remove(ControlBlock* control, BlockHeader* block)
{
    int fl, sl;
    mapping_insert(block_size(block), &fl, &sl);
    remove_free_block(control, block, fl, sl);
}

/* Insert a given block into the free list. */
static void block_insert(ControlBlock* control, BlockHeader* block)
{
    int fl, sl;
    mapping_insert(block_size(block), &fl, &sl);
    insert_free_block(control, block, fl, sl);
}

static int block_can_split(BlockHeader* block, size_t size) { return block_size(block) >= sizeof(BlockHeader) + size; }

/* Split a block into two, the second of which is free. */
static BlockHeader* block_split(BlockHeader* block, size_t size)
{
    /* Calculate the amount of space left in the remaining block. */
    BlockHeader* remaining = GetBlock(BlockToPtr(block), size - block_header_overhead);

    const size_t remain_size = block_size(block) - (size + block_header_overhead);

    tlsf_assert(block_to_ptr(remaining) == align_ptr(block_to_ptr(remaining), scAlignment) &&
                "remaining block not aligned properly");

    tlsf_assert(block_size(block) == remain_size + size + block_header_overhead);
    block_set_size(remaining, remain_size);
    tlsf_assert(block_size(remaining) >= block_size_min && "block split with invalid size");

    block_set_size(block, size);
    block_mark_as_free(remaining);

    return remaining;
}

/* Absorb a free block's storage into an adjacent previous free block. */
static BlockHeader* block_absorb(BlockHeader* prev, BlockHeader* block)
{
    tlsf_assert(!block_is_last(prev) && "previous block can't be last");
    /* Note: Leaves flags untouched. */
    prev->Size += block_size(block) + block_header_overhead;
    block_link_next(prev);
    return prev;
}

/* Merge a just-freed block with an adjacent previous free block. */
static BlockHeader* block_merge_prev(ControlBlock* control, BlockHeader* block)
{
    if (block_is_prev_free(block)) {
        BlockHeader* prev = block_prev(block);
        tlsf_assert(prev && "prev physical block can't be null");
        tlsf_assert(block_is_free(prev) && "prev block is not free though marked as such");
        block_remove(control, prev);
        block = block_absorb(prev, block);
    }

    return block;
}

/* Merge a just-freed block with an adjacent free block. */
static BlockHeader* block_merge_next(ControlBlock* control, BlockHeader* block)
{
    BlockHeader* next = block_next(block);
    tlsf_assert(next && "next physical block can't be null");

    if (block_is_free(next)) {
        tlsf_assert(!block_is_last(block) && "previous block can't be last");
        block_remove(control, next);
        block = block_absorb(block, next);
    }

    return block;
}

/* Trim any trailing block space off the end of a block, return to pool. */
static void block_trim_free(ControlBlock* control, BlockHeader* block, size_t size)
{
    tlsf_assert(block_is_free(block) && "block must be free");
    if (block_can_split(block, size)) {
        BlockHeader* remaining_block = block_split(block, size);
        block_link_next(block);
        block_set_prev_free(remaining_block);
        block_insert(control, remaining_block);
    }
}

/* Trim any trailing block space off the end of a used block, return to pool. */
static void block_trim_used(ControlBlock* control, BlockHeader* block, size_t size)
{
    tlsf_assert(!block_is_free(block) && "block must be used");
    if (block_can_split(block, size)) {
        /* If the next block is free, we must coalesce. */
        BlockHeader* remaining_block = block_split(block, size);
        block_set_prev_used(remaining_block);

        remaining_block = block_merge_next(control, remaining_block);
        block_insert(control, remaining_block);
    }
}

static BlockHeader* block_trim_free_leading(ControlBlock* control, BlockHeader* block, size_t size)
{
    BlockHeader* remaining_block = block;
    if (block_can_split(block, size)) {
        /* We want the 2nd block. */
        remaining_block = block_split(block, size - block_header_overhead);
        block_set_prev_free(remaining_block);

        block_link_next(block);
        block_insert(control, block);
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
        tlsf_assert(block_size(block) >= size);
        remove_free_block(control, block, fl, sl);
    }

    return block;
}

static void* block_prepare_used(ControlBlock* control, BlockHeader* block, size_t size)
{
    void* p = 0;
    if (block) {
        tlsf_assert(size && "size must be non-zero");
        block_trim_free(control, block, size);
        block_mark_as_used(block);
        p = BlockToPtr(block);
    }
    return p;
}

/* Clear structure and point all empty lists at the null block. */
static void control_construct(ControlBlock* control)
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
    const int this_prev_status = block_is_prev_free(block) ? 1 : 0;
    const int this_status = block_is_free(block) ? 1 : 0;
    const size_t this_block_size = block_size(block);

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
                tlsf_insist(block_is_free(block) && "block should be free");
                tlsf_insist(!block_is_prev_free(block) && "blocks should have coalesced");
                tlsf_insist(!block_is_free(block_next(block)) && "blocks should have coalesced");
                tlsf_insist(block_is_prev_free(block_next(block)) && "block should be free");
                tlsf_insist(block_size(block) >= block_size_min && "block not minimum size");

                mapping_insert(block_size(block), &fli, &sli);
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

    while (block && !block_is_last(block)) {
        pool_walker(BlockToPtr(block), block_size(block), !block_is_free(block), user);
        block = block_next(block);
    }
}

size_t tlsf_block_size(void* ptr)
{
    size_t size = 0;
    if (ptr) {
        const BlockHeader* block = BlockFromPtr(ptr);
        size = block_size(block);
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
    block_set_size(block, pool_bytes);
    block_set_free(block);
    block_set_prev_used(block);
    block_insert(reinterpret_cast<ControlBlock*>(pTlsf), block);

    /* Split the block to create a zero-size sentinel block. */
    next = block_link_next(block);
    block_set_size(next, 0);
    block_set_used(next);
    block_set_prev_free(next);

    return mem;
}

void tlsf_remove_pool(tlsf_t tlsf, pool_t pool)
{
    ControlBlock* control = reinterpret_cast<ControlBlock*>(tlsf);
    BlockHeader* block = GetBlock(pool, -(int)block_header_overhead);

    int fl = 0, sl = 0;

    tlsf_assert(block_is_free(block) && "block should be free");
    tlsf_assert(!block_is_free(block_next(block)) && "next block should not be free");
    tlsf_assert(block_size(block_next(block)) == 0 && "next block size should be zero");

    mapping_insert(block_size(block), &fl, &sl);
    remove_free_block(control, block, fl, sl);
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

    pTlsf = ptr;
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

    control_construct(reinterpret_cast<ControlBlock*>(allocated_buffer));

    pTlsf = reinterpret_cast<tlsf_t>(allocated_buffer);

    return pTlsf;
}

tlsf_t FxMemPool2::CreateWithPool(void* mem, size_t bytes)
{
    CreateFromPtr(mem);
    AddPool((char*)mem + sizeof(ControlBlock), bytes - sizeof(ControlBlock));
    return pTlsf;
}


pool_t tlsf_get_pool(tlsf_t tlsf)
{
    return reinterpret_cast<pool_t>(reinterpret_cast<uint8*>(tlsf) + sizeof(ControlBlock));
}

void* FxMemPool2::Alloc(size_t size)
{
    ControlBlock* control = reinterpret_cast<ControlBlock*>(pTlsf);
    const size_t adjust = adjust_request_size(size, scAlignment);
    BlockHeader* block = block_locate_free(control, adjust);
    return block_prepare_used(control, block, adjust);
}

void* FxMemPool2::AlignedAlloc(tlsf_t tlsf, size_t align, size_t size)
{
    ControlBlock* control = reinterpret_cast<ControlBlock*>(tlsf);
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
    tlsf_assert(sizeof(block_header_t) == block_size_min + block_header_overhead);

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

    ControlBlock* control = reinterpret_cast<ControlBlock*>(pTlsf);

    BlockHeader* block = BlockFromPtr(ptr);
    tlsf_assert(!block_is_free(block) && "block already marked as free");
    block_mark_as_free(block);
    block = block_merge_prev(control, block);
    block = block_merge_next(control, block);
    block_insert(control, block);
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
    ControlBlock* control = reinterpret_cast<ControlBlock*>(pTlsf);
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
        BlockHeader* next = block_next(block);

        const size_t cursize = block_size(block);
        const size_t combined = cursize + block_size(next) + block_header_overhead;
        const size_t adjust = adjust_request_size(size, scAlignment);

        tlsf_assert(!block_is_free(block) && "block already marked as free");

        /*
        ** If the next block is used, or when combined with the current
        ** block, does not offer enough space, we must reallocate and copy.
        */
        if (adjust > cursize && (!block_is_free(next) || adjust > combined)) {
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
                block_mark_as_used(block);
            }

            /* Trim the resulting block and return the original pointer. */
            block_trim_used(control, block, adjust);
            p = ptr;
        }
    }

    return p;
}
