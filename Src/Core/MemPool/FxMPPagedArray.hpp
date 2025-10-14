#pragma once

#include <cstdlib>

#define FX_PAGED_ARRAY_ALLOC(type_, size_) reinterpret_cast<type_*>(std::malloc(size_))
#define FX_PAGED_ARRAY_FREE(type_, ptr_)   std::free(reinterpret_cast<void*>(ptr_))

#include <Core/FxPagedArrayImpl.hpp>

#undef FX_PAGED_ARRAY_ALLOC
#undef FX_PAGED_ARRAY_FREE
