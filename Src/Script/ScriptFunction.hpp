#pragma once


#include <type_traits>

namespace fx {
namespace script {

template <typename TSignature>
struct ScriptFunctionImpl;

template <typename TReturnType, typename... TArgs>
struct ScriptFunctionImpl<TReturnType(TArgs...)>
{
	using ReturnType = TReturnType;

	// Add the void* on as the first parameter (for globals context)
	using Type = TReturnType (*)(void*, TArgs...);
};

/**
 * @brief Use ScriptFunction<signature> as a way to cache functions in a hot path where you want to store the exact
 * function pointer. This appends the `void*` for the globals context as the first argument.
 */
template <typename TSignature>
using ScriptFunction = ScriptFunctionImpl<TSignature>;

template <typename TSignature>
using ScriptFunctionType = typename ScriptFunctionImpl<TSignature>::Type;


/////////////////////////////////////
// Concepts
/////////////////////////////////////

template <typename T>
struct IsScriptFunctionImpl : std::false_type
{
};

template <typename TSignature>
struct IsScriptFunctionImpl<ScriptFunctionImpl<TSignature>> : std::true_type
{
};

template <typename T>
concept C_IsScriptFunction = IsScriptFunctionImpl<std::remove_cvref_t<T>>::value;

} // namespace script
} // namespace fx
