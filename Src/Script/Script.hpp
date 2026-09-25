#pragma once

#include "ScriptFunction.hpp"

#include <Core/String.hpp>

struct StrataJit;
struct StrataCompiler;

namespace fx::script {

class Script
{
public:
	Script() = delete;
	Script(const String& path);

	Script(const Script& other) = delete;
	Script(Script&& other);

	Script& operator=(const Script& other) = delete;
	Script& operator=(Script&& other);

	void ReloadScript();

	template <typename TSignature>
	auto GetFunction(const char* fn_name) const -> ScriptFunctionType<TSignature>
	{
		return reinterpret_cast<ScriptFunctionType<TSignature>>(GetFunctionPtr(fn_name));
	}

	template <typename T>
	T GetFunctionRaw(const char* fn_name) const
	{
		return reinterpret_cast<T>(GetFunctionPtr(fn_name));
	}

	template <typename TSignature, typename... TArgs>
	auto CallFunction(const char* name, TArgs... args) -> ScriptFunction<TSignature>::ReturnType
	{
		using SC = ScriptFunction<TSignature>;
		using ReturnType = SC::ReturnType;

		auto func_ptr = GetFunctionRaw<typename SC::Type>(name);

		if (func_ptr != nullptr) {
			if constexpr (std::is_void_v<ReturnType>) {
				func_ptr(mpGlobalContext, args...);
				return;
			}
			else {
				return func_ptr(mpGlobalContext, args...);
			}
		}

		// Fallback if function ptr was not found

		if constexpr (std::is_void_v<ReturnType>) {
			return;
		}
		else {
			return ReturnType {};
		}
	}

	/**
	 * @brief Calls a function in the script from its signature.
	 *
	 * For example, you can call a function as the following:
	 * ```cpp
	 * float result = CallFunctionPtr<float(float, float)>(ptr_add_floats, 10.0f, 20.0f);
	 * ```
	 */
	template <typename TSignature, typename... TArgs>
	auto CallFunctionPtr(ScriptFunctionType<TSignature> func_ptr, TArgs... args)
		-> ScriptFunction<TSignature>::ReturnType
	{
		using SF = ScriptFunction<TSignature>;

		if constexpr (std::is_void_v<typename SF::ReturnType>) {
			func_ptr(mpGlobalContext, args...);
			return;
		}
		else {
			return func_ptr(mpGlobalContext, args...);
		}
	}


	FX_FORCE_INLINE void* GetGlobalContext() { return mpGlobalContext; }


	FX_FORCE_INLINE bool HasErrors() const { return (mpErrors != nullptr); };
	FX_FORCE_INLINE const char* GetErrors() const { return mpErrors; }

	~Script();

private:
	void* GetFunctionPtr(const char* fn_name) const;

	void CreateGlobalData();
	void DestroyGlobalData();


	void SetExterns();

private:
	struct StrataJit* mpJit = nullptr;
	const char* mpErrors = nullptr;

	void* mpGlobalContext = nullptr;

	String mPath;
};

} // namespace fx::script
