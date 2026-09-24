#include "Script.hpp"

#include "ScriptInterop.hpp"

#include <strata/strata.h>

#include <Core/Log.hpp>
#include <Script/ScriptManager.hpp>

namespace fx::script {

Script::Script(const String& path) : mPath(path) { ReloadScript(); }

Script::Script(Script&& other)
{
	mpJit = other.mpJit;
	mpErrors = other.mpErrors;

	other.mpJit = nullptr;
	other.mpErrors = nullptr;
}

void Script::ReloadScript()
{
	if (mpErrors != nullptr) {
		strataFree(const_cast<char*>(mpErrors));
		mpErrors = nullptr;
	}

	if (mpJit != nullptr) {
		strataJitDestroy(mpJit);
	}

	StrataCompiler* compiler = gScriptManager->GetCompiler();

	if (compiler == nullptr) {
		return;
	}

	if (mpGlobalContext != nullptr) {
		DestroyGlobalData();
	}

	mpJit = strataJitCompileFile(compiler, mPath.CStr(), &mpErrors);

	if (HasErrors()) {
		LogError(LC_SCRIPT, "Could not compile script '{}'", mPath);
		LogError(LC_SCRIPT, "Errors:\n{}", GetErrors());
		return;
	}

	CreateGlobalData();

	SetExterns();
}

void* Script::GetFunctionPtr(const char* fn_name) const
{
	if (mpJit == nullptr || HasErrors()) {
		return nullptr;
	}

	return strataJitGetFunction(mpJit, fn_name);
}

static const PredefExtern* FindExtern(const char* name)
{
	Slice<const PredefExtern> predefs = GetInteropPredefs();

	for (uint32 i = 0; i < predefs.Size; i++) {
		const PredefExtern* pd = &predefs[i];
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


Script& Script::operator=(Script&& other)
{
	mpJit = other.mpJit;
	mpErrors = other.mpErrors;
	mpGlobalContext = other.mpGlobalContext;
	mPath = other.mPath;

	other.mpJit = nullptr;
	other.mpErrors = nullptr;
	other.mpGlobalContext = nullptr;
	other.mPath.Clear();

	return *this;
}

void Script::CreateGlobalData()
{
	auto context_create_func = GetFunction<void* (*)()>("__strata_context_create");
	if (context_create_func != nullptr) {
		mpGlobalContext = context_create_func();
	}
}

void Script::DestroyGlobalData()
{
	auto context_destroy_func = GetFunction<void (*)(void*)>("__strata_context_destroy");

	if (context_destroy_func != nullptr && mpGlobalContext != nullptr) {
		context_destroy_func(mpGlobalContext);
		mpGlobalContext = nullptr;
	}
}


Script::~Script()
{
	if (mpErrors != nullptr) {
		strataFree(const_cast<char*>(mpErrors));
	}

	if (mpJit != nullptr) {
		strataJitDestroy(mpJit);
	}

	if (mpGlobalContext != nullptr) {
		DestroyGlobalData();
	}
}


} // namespace fx::script
