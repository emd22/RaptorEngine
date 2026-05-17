#include "Allocator.hpp"

#include <cstdlib>

namespace fx {


/////////////////////////////////////
// Standard Library allocator
/////////////////////////////////////

void* StdAllocator::AllocRaw(uint32 size) { return std::malloc(size); }
void StdAllocator::FreeRaw(void* ptr) { return std::free(ptr); }

} // namespace fx
