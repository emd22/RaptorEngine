#pragma once

#include "Script.hpp"

#include <Core/FreeArray.hpp>

struct StrataCompiler;


namespace fx {

class ScriptManager
{
public:
	static constexpr uint32 scMaxScripts = 1536;

public:
	ScriptManager();

	script::Script* LoadScript(const String& path);
	struct StrataCompiler* GetCompiler() { return mpCompiler; };
	void FreeScript(script::Script* script);

	void ReloadAllScripts();

	~ScriptManager();

private:
	struct StrataCompiler* mpCompiler = nullptr;
	FreeArray<script::Script> mScripts;
};

} // namespace fx
