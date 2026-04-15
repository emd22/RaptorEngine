// #pragma once

// #include "Types.hpp"

// #include <concepts>

// /**
//  * Concept to determine if a type is an allocator
//  */
// template <typename T>
// concept C_IsAllocator = requires() {
//     { T::Alloc(0xB00B) };
//     { T::Free(nullptr) };
// };

// /**
//  * Allocates and frees memory using `new` and `delete`.
//  */
// template <typename T>
// class CppAllocator
// {
// public:
//     template <typename... Args>
//     static T* Alloc(uint32 size, Args... args)
//     {
//         return new T[size];
//     }

//     static void Free(T* ptr) { delete[] ptr; }
// };

// #include "Memory.hpp"

// /**
//  * Allocated and frees memory on the global memory pool
//  * @see MemPool
//  */
// template <typename T>
// class GlobalPoolAllocator
// {
// public:
//     template <typename... Args>
//     static T* Alloc(uint32 size, Args... args)
//     {
//         return MemPool::Alloc<T>(size, std::forward<Args>(args)...);
//     }

//     static void Free(T* ptr) { MemPool::Free<T>(ptr); }
// };
