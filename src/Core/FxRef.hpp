#pragma once

#include <atomic>
#include <cstddef>

// #include <Asset/FxModel.hpp>

// #include "FxMemPool.hpp"
#include "FxPanic.hpp"

#include "Types.hpp"

#include <thread>
#include <iostream>

#include <mutex>
#include <map>


// #define FX_REF_USE_MUTEX 1

struct FxRefInfoBlock
{

    std::map<std::__thread_id, std::vector<uintptr_t>> IncDecMap;
    std::atomic_uint32_t Ref = 1;

    void IncRef() {
        // std::cout << "++ INC FxRef " << std::this_thread::get_id()
        //     << '(' << Ref + 1  << ')'
        //     << '{' << this  <<  "}"
        //     << std::endl;

        // fflush(stdout);

        ++Ref;
        // IncDecMap[std::this_thread::get_id()].push_back(reinterpret_cast<uintptr_t>(this));
    }

    uint32 DecRef() {
        // std::cout << "-- DEC FxRef " << std::this_thread::get_id()
        //     << '(' << Ref - 1  << ')'
        //     << '{' << this  <<  "}"
        //     << std::endl;

        // fflush(stdout);

        // uintptr_t ptr = reinterpret_cast<uintptr_t>(this);
        // auto& vec = IncDecMap[std::this_thread::get_id()];
        // for (int32 i = 0; i < vec.size(); i++) {
        //     if (vec[i] == ptr) {
        //         vec.erase(vec.begin() + i);
        //         break;
        //     }
        // }

        return (--Ref);
    }
};



template <typename T>
class FxRef
{
private:
    FxRef(FxRefInfoBlock* info_block, T* ptr)
    {
    #ifdef FX_REF_USE_MUTEX
        std::lock_guard<std::mutex> guard(mMutex);
    #endif

        mInfo = info_block;
        mPtr = ptr;
    }

public:
    FxRef(nullptr_t np)
        : FxRef(nullptr, nullptr)
    {
    }

    FxRef(T* ptr)
    {
        this->FromPtr(ptr);
    }

    // void PrintMap()
    // {
    //     for (auto v : mInfo->IncDecMap) {
    //         std::cout << "Thread: " << v.first << " ; ";
    //         for (uintptr_t value : v.second) {
    //             std::cout << value << " ";
    //         }
    //         std::cout << std::endl;
    //     }
    // }

    FxRef(FxRef& other)
    {
    #ifdef FX_REF_USE_MUTEX
        std::lock_guard guard(other.mMutex);
    #endif

        mInfo = other.mInfo;
        mPtr = other.mPtr;

        if (mInfo) {
            mInfo->IncRef();
        }
    }

    template <typename U>
    FxRef(FxRef<U>& other)
    {
    #ifdef FX_REF_USE_MUTEX
        std::lock_guard<std::mutex> guard(other.mMutex);
    #endif

        mInfo = other.mInfo;
        mPtr = other.mPtr;

        if (mInfo) {
            mInfo->IncRef();
        }
    }

    FxRef(FxRef&& other)
    {
    #ifdef FX_REF_USE_MUTEX
        std::lock_guard<std::mutex> guard(other.mMutex);
    #endif

        mInfo = other.mInfo;
        mPtr = other.mPtr;

        other.mPtr = nullptr;
        other.mInfo = nullptr;
    }

    ~FxRef()
    {
    #ifdef FX_REF_USE_MUTEX
        std::lock_guard<std::mutex> guard(mMutex);
    #endif

        if (!mInfo) {
            return;
        }

        printf("Clearing ref to %s\n", typeid(T).name());

        if (mInfo->DecRef() == 0) {

//            FX_BREAKPOINT;
            std::cout << "DIS THREAD {" << std::this_thread::get_id() << "}" << std::endl;

            std::cout << "DEL FxRef " << this << " (" << typeid(T).name() << ")" << std::endl;

            delete mPtr;
            delete mInfo;
        }
    }


    void FromPtr(T* ptr)
    {
    #ifdef FX_REF_USE_MUTEX
        std::lock_guard<std::mutex> guard(mMutex);
    #endif

        // mInfo = FxMemPool::Alloc<FxRefInfoBlock>(sizeof(FxRefInfoBlock));
        std::cout << "CR new FxRef " << this << std::endl;

        mInfo = new FxRefInfoBlock;
        mPtr = ptr;
    }

    T* Get()
    {
    #ifdef FX_REF_USE_MUTEX
        std::lock_guard<std::mutex> guard(mMutex);
    #endif

        return mPtr;
    }

    uint32 GetRefCount()
    {
        if (mInfo) {
            return mInfo->Ref.load();
        }
        return 0;
    }

    FxRef& operator = (const FxRef& other)
    {
    #ifdef FX_REF_USE_MUTEX
        std::lock_guard<std::mutex> guard(mMutex);
    #endif

        printf("Inheriting %s\n", typeid(T).name());



        mPtr = other.mPtr;
        mInfo = other.mInfo;

        if (mInfo) {
            mInfo->IncRef();
        }

        return *this;
    }

    FxRef& operator = (FxRef& other)
    {
    #ifdef FX_REF_USE_MUTEX
        std::lock_guard<std::mutex> guard(mMutex);
    #endif

        mPtr = other.mPtr;
        mInfo = other.mInfo;

        printf("Inheriting %s\n", typeid(T).name());


        if (mInfo) {
            mInfo->IncRef();
        }

        return *this;
    }

    T* operator -> () const
    {
        FxAssert((mInfo != nullptr && mPtr != nullptr));

        return mPtr;
    }

    T& operator * () const
    {
        FxAssert((mInfo != nullptr && mPtr != nullptr));

        return *mPtr;
    }

public:
    FxRefInfoBlock* mInfo = nullptr;
    T* mPtr = nullptr;

#ifdef FX_REF_USE_MUTEX
    std::mutex mMutex;
#endif
};
