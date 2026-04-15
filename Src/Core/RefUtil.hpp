#pragma once

#include <Core/Ref.hpp>
#include <Core/TSRef.hpp>

namespace fx {

template <typename T>
concept C_IsRefType = requires(T ref) {
    typename T::UserType;
    { ref.mpPtr } -> std::convertible_to<typename T::UserType*>;
    { ref.mpRefCnt->Inc() } -> std::same_as<void>;
};


/**
 * Constructs a new `Ref` for the type `T` using the arguments `args`.
 * @tparam T the undelying type of the reference.
 * @tparam Types the types of the arguments passed in.
 */
template <typename T, typename... Types>
Ref<T> MakeRef(Types... args)
{
    return Ref<T>::New(std::forward<Types>(args)...);
}

/**
 * Constructs a new thread-safe reference counted object.
 * @tparam T the undelying type of the reference.
 * @tparam Types the types of the arguments passed in.
 */
template <typename T, typename... Types>
Ref<T> MakeTSRef(Types... args)
{
    return Ref<T>::New(std::forward<Types>(args)...);
}


template <C_IsRefType TRefType>
struct RefContext
{
public:
    using UserType = TRefType::UserType;

    RefContext(const TRefType& ref) : mRef(ref)
    {
        Assert(mRef.mpRefCnt != nullptr);
        mRef.mpRefCnt->Inc();
    }

    RefContext(const RefContext<TRefType>& other) = delete;
    RefContext(RefContext<TRefType>&& other) = delete;

    FX_FORCE_INLINE UserType* GetPtr() { return mRef.mpPtr; }
    FX_FORCE_INLINE UserType& Get() { return *mRef.mpPtr; }

    ~RefContext() { mRef.mpRefCnt->Dec(); }

private:
    const TRefType& mRef;
};

} // namespace fx
