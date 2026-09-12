#pragma once

#include <Core/DynArray.hpp>
#include <Core/Slice.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>

namespace fx {

class Console
{
	static constexpr uint32 scMaxChars = 512;

public:
	Console() = default;

	void HandleKeyboard();
	void ParseCommand();
	void ExecuteCommand(const DynArray<String>& tokens);

	StringView GetString() const;

public:
	String Output;

private:
	char pEntryBuffer[scMaxChars];
	uint32 EntryBufferIndex = 0;
};

} // namespace fx
