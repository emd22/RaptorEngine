#include "CommandConsole.hpp"

#include "CVar.hpp"
#include "Controls.hpp"

#include <Core/DynArray.hpp>
#include <Core/Slice.hpp>
#include <Core/String.hpp>

namespace fx {

void Console::ExecuteCommand(const DynArray<String>& tokens)
{
	if (tokens.Size == 0) {
		return;
	}

	if (tokens.Size == 1) {
		const CVarValue* cv = gCVars->GetCVar(tokens[0]);

		if (cv != nullptr) {
			Output = cv->AsString();
		}
		else {
			Output = "null";
		}
	}
}


void Console::ParseCommand()
{
	// Parse into chunks
	DynArray<String> args;
	args.SetPageSize(16);

	static constexpr uint32 scTempSize = 1024;

	char tmp[scTempSize];
	uint32 tmp_index = 0;

	for (uint32 i = 0; i < EntryBufferIndex; i++) {
		char ch = pEntryBuffer[i];

		if (ch == ' ' || ch == '\t' || ch == '\n') {
			if (tmp_index > 0) {
				LogInfo("pushing arg of size {}", tmp_index);
				args.Emplace(tmp, tmp_index);
				tmp_index = 0;
			}

			continue;
		}

		// Skip whatever garbage comes after this huge ass token. It will likely get flushed by a succeeding whitespace.
		if (tmp_index >= scTempSize) {
			continue;
		}

		tmp[tmp_index] = ch;
		++tmp_index;
	}

	if (tmp_index > 0) {
		LogInfo("pushing arg of size {}", tmp_index);
		args.Emplace(tmp, tmp_index);
		tmp_index = 0;
	}

	ExecuteCommand(args);

	EntryBufferIndex = 0;
}


void Console::HandleKeyboard()
{
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_BACKSPACE) && EntryBufferIndex > 0) {
		// Clear the entire line, Cmd + Delete or Win + Backspace
		if (ControlManager::IsKeyDown(eKey::FX_KEY_LMETA)) {
			EntryBufferIndex = 0;
		}
		else {
			--EntryBufferIndex;
		}
	}


	char ch = ControlManager::GetAlphaKey();
	if (ch == 0) {
		return;
	}

	pEntryBuffer[EntryBufferIndex++] = ch;

	if (ch == '\n') {
		pEntryBuffer[EntryBufferIndex] = '\0';
		ParseCommand();
	}
}

StringView Console::GetString() const { return MakeStringView(pEntryBuffer, EntryBufferIndex); }


} // namespace fx
