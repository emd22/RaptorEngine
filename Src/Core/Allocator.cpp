#include "Allocator.hpp"

#include <cstdlib>

namespace fx {


/////////////////////////////////////
// Standard Library allocator
/////////////////////////////////////

void* StdAllocator::AllocRaw(size_t size) { return std::malloc(size); }
void* StdAllocator::ReallocRaw(void* ptr, size_t size) { return std::realloc(ptr, size); }
void StdAllocator::FreeRaw(void* ptr) { std::free(ptr); }

} // namespace fx
