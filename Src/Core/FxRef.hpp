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
    FxRef() : mRefCnt(nullptr), mPtr(nullptr) {}
    FxRef(nullptr_t np) : mRefCnt(nullptr), mPtr(nullptr) {}

    /**
     * Constructs a new FxRef from a pointer.
     */
    FxRef(T* ptr) : FxRef(ptr, FxMemPool::Alloc<FxRefCount>(sizeof(FxRefCount))) {}

    /**
     * Constructs a new FxRef from a pointer and a pre-allocated ref count.
     */
    FxRef(T* ptr, FxRefCount* cnt)
    {
        FxSpinThreadGuard guard(&IsBusy);

        mRefCnt = cnt;

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
        void* raw_ptr = FxMemPool::Alloc<void>(sizeof(FxRefCount) + sizeof(T));
        T* obj_ptr = reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(raw_ptr) + sizeof(FxRefCount));

        // Construct the ref count
        ::new (raw_ptr) FxRefCount();

        // Construct the Ref object
        if constexpr (std::is_constructible_v<T, Args...>) {
            ::new (obj_ptr) T(std::forward<Args>(args)...);
        }

        // T* ptr = FxMemPool::Alloc<T>(sizeof(T), std::forward<Args>(args)...);
        return FxRef<T>(obj_ptr, reinterpret_cast<FxRefCount*>(raw_ptr));
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
    FX_FORCE_INLINE void DestroyRef()
    {
        if (mPtr) {
            // The pointer if the ref count and ptr are allocated together.
            // +========+==========+======
            // | RefCnt | Obj Ptr  | . . .
            // +========+==========+======

            uint8* bundled_ptr = reinterpret_cast<uint8*>(mPtr) - sizeof(FxRefCount);

            // Check if they were allocated together
            if (reinterpret_cast<uint8*>(mRefCnt) == bundled_ptr) {
                // Call the destructor manually (we remove the type info so the MemPool::Free call won't call it)
                if constexpr (std::is_destructible_v<T>) {
                    mPtr->~T();
                }
                // Call the destructor on the ref count
                mRefCnt->~FxRefCount();

                // Free the bundled memory
                FxMemPool::Free(bundled_ptr);

                // Null our stuff out here
                mRefCnt = nullptr;
                mPtr = nullptr;

                return;
            }

            // The pointer exists but the ref count ptr != the bundled ptr, so we will free the
            // ptr normally.
            FxMemPool::Free<T>(mPtr);
            mPtr = nullptr;
        }


        if (mRefCnt) {
            // Free the ref count
            FxMemPool::Free<FxRefCount>(mRefCnt);
            mRefCnt = nullptr;
        }
    }

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
            // Destroy the reference
            DestroyRef();
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
