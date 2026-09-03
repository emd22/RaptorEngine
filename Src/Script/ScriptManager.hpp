#pragma once

#include "Script.hpp"

#include <Core/FreeArray.hpp>

struct StrataCompiler;


namespace fx {

class ScriptManager
{
public:
	ScriptManager();

	script::Script* LoadScript(const String& path);
	struct StrataCompiler* GetCompiler() { return mpCompiler; };

	void ReloadAllScripts();

	~ScriptManager();

private:
	struct StrataCompiler* mpCompiler = nullptr;
	FreeArray<script::Script> mScripts;
};

} // namespace fx
