#pragma once

#include <Core/MemPool/FxMemPool.hpp>
#include <FxEngine.hpp>
#define FX_PAGED_ARRAY_ALLOC(type_, size_) reinterpret_cast<type_*>(std::malloc(size_))
#define FX_PAGED_ARRAY_FREE(type_, ptr_)   std::free(reinterpret_cast<void*>(ptr_))

// #define FX_PAGED_ARRAY_ALLOC(type_, size_) gEnginePool->Alloc<type_>(size_)
// #define FX_PAGED_ARRAY_FREE(type_, ptr_)   gEnginePool->Free<type_>(ptr_)

#include <Core/FxPagedArrayImpl.hpp>
