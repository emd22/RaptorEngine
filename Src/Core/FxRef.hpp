#pragma once

#include <atomic>
#include <cstddef>


#include "FxPanic.hpp"

#include "Types.hpp"

#include "FxMemory.hpp"

/** The internal reference count for `FxRef`. */
struct FxRefCount
{
    std::atomic_uint32_t Count = 1;

    uint32 GetCount()
    {
        return Count.load();
    }

    /** Increments the reference count */
    void Inc() {
        ++Count;
    }

    /** Decrements the reference count */
    uint32 Dec() {
        uint32 new_count = Count.load() - 1;
        Count.store(new_count);
        return new_count;
    }
};

template <typename T>
class FxRef
{
public:
    FxRef(nullptr_t np)
        : mRefCnt(nullptr), mPtr(nullptr)
    {
    }

    /**
     * Constructs a new FxRef from a pointer.
     */
    FxRef(T* ptr)
    {
        // mRefCnt = new FxRefCount;
        mRefCnt = FxMemPool::Alloc<FxRefCount>(sizeof(FxRefCount));
        new (mRefCnt) FxRefCount();

        mPtr = ptr;
    }

    /**
     * Inherits from another FxRef that has a type that is convertible to T.
     *
     * @tparam U The type of the other FxRef
     */
    template <typename U> requires std::is_base_of_v<T, U>
    FxRef(const FxRef<U>& other)
    {

        mRefCnt = other.mRefCnt;
        mPtr = other.mPtr;

        if (mRefCnt) {
            mRefCnt->Inc();
        }
    }


    /**
     * Inherits from another FxRef that contains the same type.
     */
    FxRef(const FxRef& other)
        : mRefCnt(other.mRefCnt), mPtr(other.mPtr)
    {
        if (mRefCnt) {
            mRefCnt->Inc();
        }
    }

    FxRef(FxRef&& other)
    {
        // No need to Inc or Dec the ref count here as we are moving the value.
        mRefCnt = std::move(other.mRefCnt);
        mPtr = std::move(other.mPtr);

        other.mPtr = nullptr;
        other.mRefCnt = nullptr;
    }

    ~FxRef()
    {
        DecRef();
    }

    /**
     * Constructs a new FxRef for the given type using the provided arguments.
     *
     * @param args The arguments to be forwarded to the constructor of T.
     */
    template<typename... Args>
    static FxRef<T> New(Args... args)
    {
        T* ptr = FxMemPool::Alloc<T>(sizeof(T), std::forward<Args>(args)...);
        return FxRef<T>(ptr);
    }

    T* Get()
    {
        return mPtr;
    }

    /** Retrieves the internal usage count for the reference. */
    uint32 GetRefCount()
    {
        if (mRefCnt) {
            return mRefCnt->Count.load();
        }
        return 0;
    }


    ///////////////////////////////
    // Operator overloads
    ///////////////////////////////

    FxRef& operator = (const FxRef& other) noexcept
    {
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

    T* operator -> () const noexcept
    {
        FxAssert(mRefCnt != nullptr && mPtr != nullptr);

        return mPtr;
    }

    T& operator * () const noexcept
    {
        FxAssert((mRefCnt != nullptr && mPtr != nullptr));

        return *mPtr;
    }

    bool operator == (nullptr_t np) const noexcept
    {
        return mPtr == nullptr;
    }

    operator bool () const noexcept
    {
        return mPtr != nullptr;
    }

private:
    /**
     * Decrements the reference count and destroys the object if there are no other references to it.
     */
    void DecRef()
    {
        // Reference count does not exist, we can assume that the object is corrupt or no longer exists.
        if (!mRefCnt) {
            return;
        }

        // If the reference count is zero, we can free the ptr as it is not being used by any other objects.
        if (mRefCnt->Dec() == 0) {
            // Free the ptr
            if (mPtr) {
                FxMemPool::Free<T>(mPtr);
            }

            // Free the ref count
            FxMemPool::Free<FxRefCount>(mRefCnt);

            mPtr = nullptr;
            mRefCnt = nullptr;
        }
    }

    /**
     * Increments the reference count if the reference exists.
     */
    void IncRef()
    {
        if (!mRefCnt) {
            return;
        }

        mRefCnt->Inc();
    }


public:
    /** The usage count for the reference */
    FxRefCount* mRefCnt = nullptr;

    T* mPtr = nullptr;
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
