#include "RefCountedBase.hpp"

#include <Core/Allocator.hpp>
#include <Engine.hpp>

namespace fx {

RefCountedBase::RefCountedBase() { mpRefCnt = StdAllocator::Alloc<RefCount>(sizeof(RefCount)); }

void RefCountedBase::InheritRef(const RefCountedBase& other)
{
    // Remove the current reference
    if (mpRefCnt) {
        ReleaseRef();
    }

    // Set the ref count to the other reference
    mpRefCnt = other.mpRefCnt;

    // If that reference does not exist, create a new one.
    if (!mpRefCnt) {
        mpRefCnt = StdAllocator::Alloc<RefCount>(sizeof(RefCount));
    }
    else {
        mpRefCnt->Inc();
    }
}


void RefCountedBase::ReleaseRef()
{
    if (!mpRefCnt) {
        return;
    }

    // There are still references to the pointer, return without destroying object.
    if (mpRefCnt->Dec() > 0) {
        return;
    }

    StdAllocator::Free(mpRefCnt);
    mpRefCnt = nullptr;

    // Destroy the object if it has no remaining refs.
    DestroyObject();
}


} // namespace fx
