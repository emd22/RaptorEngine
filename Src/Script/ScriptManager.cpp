#include "ScriptManager.hpp"

#include <strata/strata.h>

namespace fx {

ScriptManager::ScriptManager()
{
	mpCompiler = strataCompilerCreate();
	mScripts.Init(64);
}

script::Script* ScriptManager::LoadScript(const String& path)
{
	script::Script* script = mScripts.NewItem(nullptr, mpCompiler, path);
	return script;
}

ScriptManager::~ScriptManager() { strataCompilerDestroy(mpCompiler); }

} // namespace fx
