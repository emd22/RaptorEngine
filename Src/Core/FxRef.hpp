#pragma once

#include "FxDefines.hpp"
#include "FxMemory.hpp"
#include "FxPanic.hpp"
#include "FxTypes.hpp"

#ifdef FX_DEBUG_REF
#include <Core/FxLog.hpp>
#endif

#include <atomic>
#include <cstddef>

/** The internal reference count for `FxRef`. */
struct FxRefCount
{
    using IntType = uint32;
    using RefCountType = std::atomic<IntType>;

    /** Increments the reference count */
    void Inc() { ++Count; }

    /** Decrements the reference count */
    IntType Dec() { return (--Count); }

public:
    RefCountType Count = 1;
};

template <typename T>
class FxRef
{
public:
    FxRef(nullptr_t np) : mRefCnt(nullptr), mPtr(nullptr) {}

    /**
     * Constructs a new FxRef from a pointer.
     */
    FxRef(T* ptr)
    {
        FxSpinThreadGuard guard(&IsBusy);

        mRefCnt = FxMemPool::Alloc<FxRefCount>(sizeof(FxRefCount));

        mPtr = ptr;

#ifdef FX_DEBUG_REF
        FxLogDebug("Created FxRef {:p} (RefCnt: {:p})", reinterpret_cast<void*>(mPtr),
                   reinterpret_cast<void*>(mRefCnt));
#endif
    }

    /**
     * Inherits from another FxRef that has a type that is convertible to T.
     *
     * @tparam U The type of the other FxRef
     */
    template <typename DerivedType>
        requires std::is_base_of_v<T, DerivedType>
    FxRef(const FxRef<DerivedType>& other)
    {
        FxSpinThreadGuard other_guard(&other.IsBusy);
        FxSpinThreadGuard guard(&IsBusy);

        mRefCnt = other.mRefCnt;
        mPtr = other.mPtr;

        if (mRefCnt) {
            mRefCnt->Inc();
        }
    }

    /**
     * @brief Converts from a base type FxRef to a derived type FxRef.
     */
    template <typename BaseType>
        requires std::is_base_of_v<BaseType, T>
    FxRef(const FxRef<BaseType>& other)
    {
        FxSpinThreadGuard other_guard(&other.IsBusy);
        FxSpinThreadGuard guard(&IsBusy);

        mRefCnt = other.mRefCnt;
        mPtr = static_cast<T*>(other.mPtr);

        if (mRefCnt) {
            mRefCnt->Inc();
        }
    }


    /**
     * Inherits from another FxRef that contains the same type.
     */
    FxRef(const FxRef& other)
    {
        FxSpinThreadGuard other_guard(&other.IsBusy);
        FxSpinThreadGuard guard(&IsBusy);

        mRefCnt = other.mRefCnt;
        mPtr = other.mPtr;

        if (mRefCnt) {
            mRefCnt->Inc();
        }
    }

    FxRef(FxRef&& other)
    {
        FxSpinThreadGuard other_guard(&other.IsBusy);
        FxSpinThreadGuard guard(&IsBusy);

        // No need to Inc or Dec the ref count here as we are moving the value.
        mRefCnt = std::move(other.mRefCnt);
        mPtr = std::move(other.mPtr);

        other.mPtr = nullptr;
        other.mRefCnt = nullptr;
    }


    ~FxRef()
    {
        FxSpinThreadGuard guard(&IsBusy);

        DecRef();
    }

    /**
     * Constructs a new FxRef for the given type using the provided arguments.
     *
     * @param args The arguments to be forwarded to the constructor of T.
     */
    template <typename... Args>
    static FxRef<T> New(Args... args)
    {
        T* ptr = FxMemPool::Alloc<T>(sizeof(T), std::forward<Args>(args)...);
        return FxRef<T>(ptr);
    }

    /** Retrieves the internal usage count for the reference. */
    uint32 GetRefCount()
    {
        FxSpinThreadGuard guard(&IsBusy);

        if (mRefCnt) {
            return mRefCnt->Count;
        }

        return 0;
    }

    ///////////////////////////////
    // Operator overloads
    ///////////////////////////////

    FxRef& operator=(const FxRef& other)
    {
        FxSpinThreadGuard guard(&IsBusy);
        FxSpinThreadGuard other_guard(&other.IsBusy);

        // If there is already a pointer in this reference, decrement or destroy it
        // This will be an infinite memory printing machine if this is not here
        if (mPtr && mRefCnt) {
            DecRef();
        }

        mPtr = other.mPtr;
        mRefCnt = other.mRefCnt;

        // Since `mRefCnt` now points to the old ref's count, Inc'ing this
        // marks that the object is in use.
        IncRef();

        return *this;
    }

    T* operator->() const noexcept
    {
        FxSpinThreadGuard guard(&IsBusy);

        FxAssert(mRefCnt != nullptr && mPtr != nullptr);

        return mPtr;
    }

    T& operator*() const noexcept
    {
        FxSpinThreadGuard guard(&IsBusy);

        FxAssert((mRefCnt != nullptr && mPtr != nullptr));

        return *mPtr;
    }

    bool operator==(nullptr_t np) const noexcept
    {
        FxSpinThreadGuard guard(&IsBusy);

        return mPtr == nullptr;
    }

    operator bool() const noexcept
    {
        FxSpinThreadGuard guard(&IsBusy);

        return mPtr != nullptr;
    }

private:
    /**
     * Decrements the reference count and destroys the object if there are no other references to it.
     */
    void DecRef()
    {
        // Note that since these are only called by other protected ref functions, these do not need a spinlock guard.

        // Reference count does not exist, we can assume that the object is corrupt or no longer exists.
        if (!mRefCnt) {
            return;
        }

        // If the reference count is zero, we can free the ptr as it is not being used by any other objects.
        if (mRefCnt->Dec() == 0) {
#ifdef FX_DEBUG_REF
            FxLogDebug("Destroying FxRef {:p} (RefCnt: {:p})", reinterpret_cast<void*>(mPtr),
                       reinterpret_cast<void*>(mRefCnt));
#endif

            // Free the ptr
            if (mPtr) {
                FxMemPool::Free<T>(mPtr);
                mPtr = nullptr;
            }

            if (mRefCnt) {
                // Free the ref count
                FxMemPool::Free<FxRefCount>(mRefCnt);
                mRefCnt = nullptr;
            }
        }
    }

    /**
     * Increments the reference count if the reference exists.
     */
    void IncRef()
    {
        // Note that since these are only called by other protected ref functions, these do not need a spinlock guard.

        if (!mRefCnt) {
            return;
        }

        mRefCnt->Inc();
    }


public:
    /** The usage count for the reference */
    FxRefCount* mRefCnt = nullptr;
    T* mPtr = nullptr;

    /** Denotes if the ref is currently in use from some thread */
    mutable FxAtomicFlag IsBusy {};
};

/**
 * Constructs a new `FxRef` for the type `T` using the arguments `args`.
 * @tparam T the undelying type of the reference.
 * @tparam Types the types of the arguments passed in.
 */
template <typename T, typename... Types>
FxRef<T> FxMakeRef(Types... args)
{
    return FxRef<T>::New(std::forward<Types>(args)...);
}
