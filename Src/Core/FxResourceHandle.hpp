#pragma once

// #include <atomic>

// #include "FxMemory.hpp"

// template <typename T>
// class FxResourceHandle
// {
// public:
//     static FxResourceHandle New()
//     {
//         FxResourceHandle<T> handle;
//         handle.Ptr = FxMemPool::Alloc(sizeof(T));
//         handle.mIsAvailable.store(true);
//         handle.mIsAvailable.notify_all();
//     }

//     T* operator -> () const
//     {
//         FxAssert(mIsAvailable.load() == true);
//         return Ptr;
//     }

//     T& operator * () const
//     {
//         FxAssert(mIsAvailable.load() == true);
//         return *Ptr;
//     }

//     void Destroy()
//     {
//         FxMemPool::Free(Ptr);
//         mIsAvailable.store(false);
//     }

// public:
//     T* Ptr = nullptr;

// private:
//     std::atomic_bool mIsAvailable = ATOMIC_VAR_INIT(false);
// };
