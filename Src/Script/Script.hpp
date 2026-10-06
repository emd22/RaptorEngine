#pragma once

#include "ScriptFunction.hpp"

#include <Core/String.hpp>

#include <raptor_ffi.h>

namespace fx::script {

class Script
{
public:
	Script() = delete;
	explicit Script(const String& path) : mId(rx_script_load(path.CStr())) {}

	Script(const Script& other) = delete;
	Script& operator=(const Script& other) = delete;

	void ReloadScript() { rx_script_reload(mId); }

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
				func_ptr(GetGlobalContext(), args...);
				return;
			}
			else {
				return func_ptr(GetGlobalContext(), args...);
			}
		}

		if constexpr (std::is_void_v<ReturnType>) {
			return;
		}
		else {
			return ReturnType {};
		}
	}

	template <typename TSignature, typename... TArgs>
	auto CallFunctionPtr(ScriptFunctionType<TSignature> func_ptr, TArgs... args)
		-> ScriptFunction<TSignature>::ReturnType
	{
		using SF = ScriptFunction<TSignature>;

		if constexpr (std::is_void_v<typename SF::ReturnType>) {
			func_ptr(GetGlobalContext(), args...);
			return;
		}
		else {
			return func_ptr(GetGlobalContext(), args...);
		}
	}

	void* GetGlobalContext() { return rx_script_context(mId); }

	bool HasErrors() const { return rx_script_has_errors(mId); }

	uint32_t GetId() const { return mId; }

	~Script() { rx_script_free(mId); }

private:
	void* GetFunctionPtr(const char* fn_name) const
	{
		return const_cast<void*>(rx_script_function(mId, fn_name));
	}

	uint32_t mId;
};

} // namespace fx::script
