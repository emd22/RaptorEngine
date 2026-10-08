#pragma once

#include "Types.hpp"

#include <concepts>
#include <new>
#include <type_traits>
#include <utility>

#define FX_VALIDATE_ALLOCATOR(TType_) static_assert(C_IsAllocator<TType_>)


namespace fx {

template <typename T>
concept C_IsAllocator = requires(T t, int* ptr) {
    // Function requirements
    { T::AllocRaw(64) } -> std::same_as<void*>;
    { T::FreeRaw(nullptr) };
    { T::template Alloc<int>(64) } -> std::same_as<int*>;
    { T::template Free<int>(ptr) };
};


class NullAllocator
{
public:
    static void* AllocRaw(uint32 size) { return nullptr; }
    static void FreeRaw(FX_UNUSED void* ptr) {}

    template <typename T>
    static T* Alloc(uint32 size)
    {
        return static_cast<T*>(nullptr);
    };

    template <typename T>
    static void Free(FX_UNUSED T* ptr)
    {
    }
};

FX_VALIDATE_ALLOCATOR(NullAllocator);


class StdAllocator
{
public:
    static void* AllocRaw(size_t size);
    static void* ReallocRaw(void* ptr, size_t size);
    static void FreeRaw(void* ptr);

    template <typename T, typename... TArgs>
    static T* Alloc(size_t size, TArgs&&... args)
    {
        T* ptr = static_cast<T*>(AllocRaw(size));

        if constexpr (std::is_constructible_v<T, TArgs...>) {
            ::new (ptr) T(std::forward<TArgs>(args)...);
        }

        return ptr;
    }

    template <typename T>
    static T* Realloc(T* ptr, size_t size)
    {
        return static_cast<T*>(ReallocRaw(ptr, size));
    }

    template <typename T>
    static void Free(T* ptr)
    {
        if constexpr (std::is_destructible_v<T>) {
            ptr->~T();
        }

        FreeRaw(reinterpret_cast<void*>(ptr));
    }
};

FX_VALIDATE_ALLOCATOR(StdAllocator);


enum class eMemHeap : uint8
{
    Script,
    Physics,

    Count,
};


class MemHeaps
{
public:
    static void* AllocRaw(eMemHeap heap, size_t size);
    static void* ReallocRaw(eMemHeap heap, void* ptr, size_t size);
    static void FreeRaw(void* ptr);

    static void Collect(eMemHeap heap, bool force = false);
    static void Reset(eMemHeap heap);
    static void Destroy();
};


template <eMemHeap THeap>
class HeapAllocator
{
public:
    static void* AllocRaw(size_t size) { return MemHeaps::AllocRaw(THeap, size); }
    static void* ReallocRaw(void* ptr, size_t size) { return MemHeaps::ReallocRaw(THeap, ptr, size); }
    static void FreeRaw(void* ptr) { MemHeaps::FreeRaw(ptr); }

    template <typename T, typename... TArgs>
    static T* Alloc(size_t size, TArgs&&... args)
    {
        T* ptr = static_cast<T*>(AllocRaw(size));

        if constexpr (std::is_constructible_v<T, TArgs...>) {
            ::new (ptr) T(std::forward<TArgs>(args)...);
        }

        return ptr;
    }

    template <typename T>
    static T* Realloc(T* ptr, size_t size)
    {
        return static_cast<T*>(ReallocRaw(ptr, size));
    }

    template <typename T>
    static void Free(T* ptr)
    {
        if constexpr (std::is_destructible_v<T>) {
            ptr->~T();
        }

        FreeRaw(reinterpret_cast<void*>(ptr));
    }
};

using ScriptAllocator = HeapAllocator<eMemHeap::Script>;
using PhysicsAllocator = HeapAllocator<eMemHeap::Physics>;

FX_VALIDATE_ALLOCATOR(ScriptAllocator);
FX_VALIDATE_ALLOCATOR(PhysicsAllocator);


} // namespace fx
