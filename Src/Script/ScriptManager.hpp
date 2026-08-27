#pragma once

#include "Script.hpp"

struct StrataCompiler;


namespace fx {

class ScriptManager
{
public:
	ScriptManager();

	script::Script LoadScript(const char* path);

	~ScriptManager();

private:
	struct StrataCompiler* mpCompiler = nullptr;
};

} // namespace fx
