#pragma once

#include <Core/DynArray.hpp>
#include <Core/Slice.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>

namespace fx {

namespace script {
class Script;
}

class Console
{
	static constexpr uint32 scMaxChars = 512;

public:
	Console() = default;

	void HandleKeyboard();
	void ParseCommand();
	void ExecuteCommand(const DynArray<String>& tokens);
	void ExecuteScriptCommand(const String& cmd_name);

	StringView GetString() const;

public:
	String Output;

private:
	char mpEntryBuffer[scMaxChars];
	uint32 mEntryBufferIndex = 0;

	script::Script* mpScript = nullptr;
};

} // namespace fx
