#pragma once

#include "Script.hpp"

#include <memory>
#include <vector>

namespace fx {

class ScriptManager
{
public:
	script::Script* LoadScript(const String& path)
	{
		mScripts.push_back(std::make_unique<script::Script>(path));
		return mScripts.back().get();
	}

	void FreeScript(script::Script* script)
	{
		std::erase_if(mScripts, [script](const auto& owned) { return owned.get() == script; });
	}

	void ReloadAllScripts() { rx_script_reload_all(); }

	~ScriptManager()
	{
		mScripts.clear();
		rx_script_shutdown();
	}

private:
	std::vector<std::unique_ptr<script::Script>> mScripts;
};

} // namespace fx
