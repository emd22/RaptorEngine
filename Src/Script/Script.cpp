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

	SetExterns();
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

#define EXTERN_RESOLVE(name_, fn_)                                                                                     \
	if (strcmp(name, "printf") == 0) {                                                                                 \
		return (void*)printf;                                                                                          \
	}


struct PredefExtern
{
	const char* pcName;
	void* pFunction;
};

static const PredefExtern scAvailableExterns[] = {
	PredefExtern { "printf", reinterpret_cast<void*>(printf) },
};

static const PredefExtern* FindExtern(const char* name)
{
	for (uint32 i = 0; i < std::size(scAvailableExterns); i++) {
		const PredefExtern* pd = &scAvailableExterns[i];
		if (!strcmp(pd->pcName, name)) {
			return pd;
		}
	}

	return nullptr;
}

void Script::SetExterns()
{
	if (mpJit == nullptr || HasErrors()) {
		return;
	}

	for (uint32 i = 0; i < strataJitGetExternSymbolCount(mpJit); ++i) {
		const char* name = strataJitGetExternSymbolName(mpJit, i);

		const PredefExtern* pd = FindExtern(name);

		if (pd == nullptr || !strataJitAddSymbol(mpJit, name, pd->pFunction)) {
			LogError(LC_SCRIPT, "No host binding for extern '{}'", name);
		}
	}
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
