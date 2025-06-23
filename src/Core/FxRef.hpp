#pragma once

#include <atomic>
#include <cstddef>

#include "FxMemPool.hpp"

#include "FxPanic.hpp"

#include "Types.hpp"


struct FxRefCount
{
    std::atomic_uint32_t Count = 1;

    uint32 GetCount()
    {
        return Count.load();
    }

    void Inc() {
        ++Count;
    }

    uint32 Dec() {
        return (--Count);
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
        // If there is already a reference, decrement or destroy it
        if (mPtr || mRefCnt) {
            DecRef();
        }

        mPtr = other.mPtr;
        mRefCnt = other.mRefCnt;

        IncRef();

        return *this;
    }

    T* operator -> () const noexcept
    {
        FxAssert((mRefCnt != nullptr && mPtr != nullptr));

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
     * Decrement the reference count and destroy the object if there are no other references.
     */
    void DecRef()
    {
        if (!mRefCnt) {
            return;
        }


        if (mRefCnt->Dec() == 0) {
            FxMemPool::Free<T>(mPtr);
            FxMemPool::Free<FxRefCount>(mRefCnt);

            mPtr = nullptr;
            mRefCnt = nullptr;
        }
    }

    /**
     * Increment the reference count if the FxRef is created.
     */
    void IncRef()
    {
        if (!mRefCnt) {
            return;
        }

        mRefCnt->Inc();
    }


public:
    FxRefCount* mRefCnt = nullptr;
    T* mPtr = nullptr;

#ifdef FX_REF_USE_MUTEX
    std::mutex mMutex;
#endif
};
