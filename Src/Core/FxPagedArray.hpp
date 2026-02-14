#pragma once

#include <Core/MemPool/FxMemPool.hpp>

#define FX_PAGED_ARRAY_ALLOC(type_, size_)                                                                             \
    FxLogInfo("YEAH");                                                                                                 \
    FxMemPool::Alloc<type_>(size_)


#define FX_PAGED_ARRAY_FREE(type_, ptr_) FxMemPool::Free<type_>(ptr_)

#include <Core/FxPagedArrayImpl.hpp>

#undef FX_PAGED_ARRAY_ALLOC
#undef FX_PAGED_ARRAY_FREE
