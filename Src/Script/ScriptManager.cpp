#include "ScriptManager.hpp"

#include <strata/strata.h>

namespace fx {

ScriptManager::ScriptManager() { mpCompiler = strataCompilerCreate(); }

script::Script ScriptManager::LoadScript(const char* path) { return script::Script(mpCompiler, path); }

ScriptManager::~ScriptManager() { strataCompilerDestroy(mpCompiler); }

} // namespace fx
