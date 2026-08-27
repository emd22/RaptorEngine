#include "Script.hpp"

#include <strata/strata.h>

#include <Core/Log.hpp>

namespace fx::script {

Script::Script(struct StrataCompiler* compiler, const String& path)
{
	if (compiler == nullptr) {
		return;
	}

	mpJit = strataJitCompileFile(compiler, path.CStr(), &mpErrors);

	if (HasErrors()) {
		LogError(LC_SCRIPT, "Could not compile script '{}'", path);
		LogError(LC_SCRIPT, "Errors:\n{}", GetErrors());
	}
}


Script::Script(const Script& other)
{
	mpJit = other.mpJit;
	mpErrors = other.mpErrors;
}

Script::Script(Script&& other)
{
	mpJit = other.mpJit;
	mpErrors = other.mpErrors;

	other.mpJit = nullptr;
	other.mpErrors = nullptr;
}

void* Script::GetFunctionPtr(const char* fn_name) const
{
	if (mpJit == nullptr || HasErrors()) {
		return nullptr;
	}

	return strataJitGetFunction(mpJit, fn_name);
}


Script& Script::operator=(const Script& other)
{
	mpJit = other.mpJit;
	mpErrors = other.mpErrors;

	return *this;
}

Script& Script::operator=(Script&& other)
{
	mpJit = other.mpJit;
	mpErrors = other.mpErrors;

	other.mpJit = nullptr;
	other.mpErrors = nullptr;

	return *this;
}


Script::~Script()
{
	if (mpErrors != nullptr) {
		strataFree(const_cast<char*>(mpErrors));
	}

	if (mpJit != nullptr) {
		strataJitDestroy(mpJit);
	}
}


} // namespace fx::script
