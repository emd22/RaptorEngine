#pragma once

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

	template <typename T>
	T GetFunction(const char* fn_name) const
	{
		return reinterpret_cast<T>(GetFunctionPtr(fn_name));
	}

	template <typename TReturnType, typename... TArgs>
	TReturnType CallFunction(const char* name, TArgs... args)
	{
		auto func_ptr = GetFunction<TReturnType (*)(void*, TArgs...)>(name);

		if (func_ptr != nullptr) {
			if constexpr (std::is_void_v<TReturnType>) {
				func_ptr(mpGlobalContext, args...);
				return;
			}
			else {
				return func_ptr(mpGlobalContext, args...);
			}
		}

		// Fallback if function ptr was not found

		if constexpr (std::is_void_v<TReturnType>) {
			return;
		}
		else {
			return TReturnType {};
		}
	}

	template <typename TReturnType, typename... TArgs>
	TReturnType CallFunctionPtr(TReturnType (*func_ptr)(void*, TArgs...), TArgs... args)
	{
		DebugAssert(func_ptr != nullptr);

		if constexpr (std::is_void_v<TReturnType>) {
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
