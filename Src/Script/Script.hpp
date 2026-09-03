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

	Script(const Script& other);
	Script(Script&& other);

	Script& operator=(const Script& other);
	Script& operator=(Script&& other);

	void ReloadScript();

	template <typename T>
	T GetFunction(const char* fn_name) const
	{
		return reinterpret_cast<T>(GetFunctionPtr(fn_name));
	}


	FX_FORCE_INLINE bool HasErrors() const { return (mpErrors != nullptr); };
	FX_FORCE_INLINE const char* GetErrors() const { return mpErrors; }

	~Script();

private:
	void* GetFunctionPtr(const char* fn_name) const;

	void SetExterns();

private:
	struct StrataJit* mpJit = nullptr;
	const char* mpErrors = nullptr;

	String mPath;
};

} // namespace fx::script
