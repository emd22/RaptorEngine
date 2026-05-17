#pragma once

#include "Types.hpp"

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
    static void* AllocRaw(uint32 size);
    static void FreeRaw(void* ptr);

    template <typename T>
    static T* Alloc(uint32 size)
    {
        return static_cast<T*>(AllocRaw(size));
    };

    template <typename T>
    static void Free(T* ptr)
    {
        Free(reinterpret_cast<void*>(ptr));
    }
};

FX_VALIDATE_ALLOCATOR(StdAllocator);


} // namespace fx
