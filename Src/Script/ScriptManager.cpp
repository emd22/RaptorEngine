#include "ScriptManager.hpp"

#include <strata/strata.h>

namespace fx {

ScriptManager::ScriptManager()
{
	mpCompiler = strataCompilerCreate();
	mScripts.Init(64);
}

script::Script* ScriptManager::LoadScript(const String& path) { return mScripts.NewItem(nullptr, path); }

void ScriptManager::ReloadAllScripts()
{
	for (uint32 i = 0; i < mScripts.Capacity; i++) {
		if (!mScripts.SlotsInUse.Get(i)) {
			continue;
		}

		script::Script* script = mScripts.GetItem(i);
		script->ReloadScript();
	}
}

ScriptManager::~ScriptManager() { strataCompilerDestroy(mpCompiler); }

} // namespace fx
