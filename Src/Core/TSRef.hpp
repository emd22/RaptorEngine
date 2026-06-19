#pragma once


#include "Assert.hpp"
#include "Defines.hpp"
#include "Types.hpp"

#include <Core/MemPool/MemPool.hpp>
#include <Engine.hpp>

#ifdef FX_DEBUG_REF
#include <Core/Log.hpp>
#endif

#include <Math/MathUtil.hpp>
#include <cstddef>


namespace fx {

/** The internal reference count for `TSRef`. */
struct TSRefCount
{
    using IntType = int32;
    using RefCountType = std::atomic<IntType>;

    /** Increments the reference count */
    void Inc() { ++Count; }

    /** Decrements the reference count */
    IntType Dec() { return (--Count); }

public:
    RefCountType Count = 1;
};

template <typename T>
class TSRef
{
public:
    using UserType = T;

    TSRef() : mpRefCnt(nullptr), mpPtr(nullptr) {}
    TSRef(nullptr_t np) : mpRefCnt(nullptr), mpPtr(nullptr) {}

    /**
     * Constructs a new TSRef from a pointer.
     */
    TSRef(T* ptr) : TSRef(ptr, gEnginePool->Alloc<TSRefCount>(sizeof(TSRefCount))) {}

    /**
     * Constructs a new TSRef from a pointer and a pre-allocated ref count.
     */
    TSRef(T* RESTRICT ptr, TSRefCount* RESTRICT cnt, bool is_combined_allocation = false)
        : mpRefCnt(cnt), mpPtr(ptr), mbIsCombinedAllocation(is_combined_allocation), mbIsExternalPtr(true)
    {
#ifdef FX_DEBUG_REF
        LogDebug("Created TSRef {:p} (RefCnt: {:p})", reinterpret_cast<void*>(mpPtr),
                 reinterpret_cast<void*>(mpRefCnt));
#endif
    }

    /**
     * @brief Inherits from another thread safe pointer that has a type that is convertible to T.
     *
     * @tparam U The type of the other TSRef
     */
    template <typename DerivedType>
        requires std::is_base_of_v<T, DerivedType>
    TSRef(const TSRef<DerivedType>& other)
    {
        mpRefCnt = other.mpRefCnt;
        mpPtr = other.mpPtr;
        mbIsCombinedAllocation = other.mbIsCombinedAllocation;
        mbIsExternalPtr = other.mbIsExternalPtr;

        if (mpRefCnt) {
            mpRefCnt->Inc();
        }
    }

    /**
     * @brief Converts from a base type TSRef to a derived type TSRef.
     */
    template <typename BaseType>
        requires std::is_base_of_v<BaseType, T>
    TSRef(const TSRef<BaseType>& other)
    {
        mpRefCnt = other.mpRefCnt;
        mpPtr = static_cast<T*>(other.mpPtr);
        mbIsCombinedAllocation = other.mbIsCombinedAllocation;
        mbIsExternalPtr = other.mbIsExternalPtr;

        if (mpRefCnt) {
            mpRefCnt->Inc();
        }
    }


    /**
     * Inherits from another TSRef that contains the same type.
     */
    TSRef(const TSRef& other)
    {
        mpRefCnt = other.mpRefCnt;
        mpPtr = other.mpPtr;
        mbIsCombinedAllocation = other.mbIsCombinedAllocation;
        mbIsExternalPtr = other.mbIsExternalPtr;

        if (mpRefCnt) {
            mpRefCnt->Inc();
        }
    }


    TSRef(TSRef&& other)
    {
        mpRefCnt = (other.mpRefCnt);
        mpPtr = (other.mpPtr);
        mbIsCombinedAllocation = other.mbIsCombinedAllocation;
        mbIsExternalPtr = other.mbIsExternalPtr;

        other.mpPtr = nullptr;
        other.mpRefCnt = nullptr;
    }


    ~TSRef() { DecRef(); }

    /**
     * Constructs a new TSRef for the given type using the provided arguments.
     *
     * @param args The arguments to be forwarded to the constructor of T.
     */
    template <typename... Args>
    static TSRef<T> New(Args... args)
    {
        // Since we want to ensure aligned laod/store on the ref count object as well, we should ensure that the T
        // object is aligned on a 16 byte boundary.
        uint8* raw_ptr = gEnginePool->Alloc<uint8>(MathUtil::AlignValue<16>(sizeof(T)) + sizeof(TSRefCount));

        T* obj_ptr = reinterpret_cast<T*>(raw_ptr);
        TSRefCount* count_ptr = reinterpret_cast<TSRefCount*>(raw_ptr + sizeof(T));

        Assert(reinterpret_cast<uint8*>(count_ptr) - sizeof(T) == raw_ptr);

        // Construct the ref count
        ::new (count_ptr) TSRefCount();

        // Construct the Ref object
        if constexpr (std::is_constructible_v<T, Args...>) {
            ::new (obj_ptr) T(std::forward<Args>(args)...);
        }

#ifdef FX_DEBUG_REF
        LogDebug("Creating TSRef {:p} (RefCnt: {:p}) as combined allocation", reinterpret_cast<void*>(obj_ptr),
                 reinterpret_cast<void*>(count_ptr));
#endif

        return TSRef<T>(obj_ptr, count_ptr, true);
    }

    /** Retrieves the internal usage count for the reference. */
    TSRefCount::IntType GetRefCount()
    {
        if (mpRefCnt) {
            return mpRefCnt->Count;
        }

        return 0;
    }

    ///////////////////////////////
    // Operator overloads
    ///////////////////////////////

    TSRef& operator=(const TSRef& other)
    {
        // If there is already a pointer in this reference, decrement or destroy it
        // This will be an infinite memory printing machine if this is not here
        if (mpPtr && mpRefCnt) {
            DecRef();
        }

        mpPtr = other.mpPtr;
        mpRefCnt = other.mpRefCnt;
        mbIsCombinedAllocation = other.mbIsCombinedAllocation;
        mbIsExternalPtr = other.mbIsExternalPtr;

        // Since `mRefCnt` now points to the old ref's count, Inc'ing this
        // marks that the object is in use.
        IncRef();

        return *this;
    }

    T* operator->() const noexcept
    {
        Assert(mpRefCnt != nullptr && mpPtr != nullptr);

        return mpPtr;
    }

    T& operator*() const noexcept
    {
        Assert((mpRefCnt != nullptr && mpPtr != nullptr));

        return *mpPtr;
    }

    bool operator==(nullptr_t np) const noexcept { return mpPtr == nullptr; }

    explicit operator bool() const noexcept { return mpPtr != nullptr; }

    bool IsValid() const noexcept { return mpPtr != nullptr && mpRefCnt != nullptr; }

    void SetNull()
    {
        mpPtr = nullptr;
        mpRefCnt = nullptr;
    }

    void DestroyRef()
    {
        if (mpPtr) {
            // The pointer if the ref count and ptr are allocated together.
            // +=========+==========+======
            // | Obj Ptr | RefCnt   | . . .
            // +=========+==========+======
            // NOTE: Unless we want to store the size of the original object, using the casting constructor messes up
            // using mpPtr + sizeof(T) == mpRefCnt to check if the allocation is combined.

            if (mbIsCombinedAllocation) {
                // Call the destructor manually (we remove the type info so the MemPool::Free call won't call it)
                if constexpr (std::is_destructible_v<T>) {
                    mpPtr->~T();
                }

                if (mpRefCnt) {
                    // Call the destructor on the ref count
                    mpRefCnt->~TSRefCount();
                }

                // Free the bundled memory
                gEnginePool->Free(reinterpret_cast<uint8*>(mpPtr));

                mpRefCnt = nullptr;
                mpPtr = nullptr;

                return;
            }

            // If the ptr is external, we can assume that it will be freed by the owner.
            if (!mbIsExternalPtr) {
                // The pointer exists but the ref count ptr != the bundled ptr, so we will free the
                // ptr normally.
                gEnginePool->Free<T>(mpPtr);
            }

            mpPtr = nullptr;
        }

        if (mpRefCnt) {
            // Free the ref count
            gEnginePool->Free<TSRefCount>(mpRefCnt);
            mpRefCnt = nullptr;
        }
    }

private:
    /**
     * Decrements the reference count and destroys the object if there are no other references to it.
     */
    void DecRef()
    {
        // Reference count does not exist, we can assume that the object is corrupt or no longer exists.
        if (!mpRefCnt) {
            return;
        }

        // If the reference count is zero, we can free the ptr as it is not being used by any other objects.
        if (mpRefCnt->Dec() == 0) {
#ifdef FX_DEBUG_REF
            LogDebug("Destroying TSRef {:p} (RefCnt: {:p})", reinterpret_cast<void*>(mpPtr),
                     reinterpret_cast<void*>(mpRefCnt));
#endif
            // Destroy the reference
            DestroyRef();
        }
    }

    /**
     * Increments the reference count if the reference exists.
     */
    void IncRef()
    {
        if (!mpRefCnt) {
            return;
        }

        mpRefCnt->Inc();
    }


public:
    /** The usage count for the reference */
    TSRefCount* mpRefCnt = nullptr;
    T* mpPtr = nullptr;

    /// Was the memory allocated as one buffer?
    bool mbIsCombinedAllocation : 1 = false;
    bool mbIsExternalPtr : 1 = false;
};

} // namespace fx
