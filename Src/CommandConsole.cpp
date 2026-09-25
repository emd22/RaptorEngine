#include "CommandConsole.hpp"

#include "CVar.hpp"
#include "Controls.hpp"

#include <Core/DynArray.hpp>
#include <Core/Slice.hpp>
#include <Core/String.hpp>
#include <Editor/RaptorEditor.hpp>
#include <Engine.hpp>

namespace fx {

void Console::ExecuteScriptCommand(const String& cmd_name)
{
#ifdef FX_IS_EDITOR
	if (gEditor->RunCommand(cmd_name)) {
		Output = "Executed";
		return;
	}
#endif

	Output = "Cmd not found";
}

void Console::ExecuteCommand(const DynArray<String>& tokens)
{
	if (tokens.Size == 0) {
		return;
	}
	const String& cmd = tokens[0];
	if (cmd.Length < 1) {
		return;
	}
	if (cmd[0] == '$') {
		CVarValue* cv = gCVars->GetCVar(cmd.SubStr(1, cmd.Length - 1));

		// Get CVar value
		if (tokens.Size == 1) {
			if (cv != nullptr) {
				Output = cv->AsString();
			}
			else {
				Output = "not defined";
			}
		}

		// Set CVar value
		else if (tokens.Size == 2) {
			const String& value = tokens[1];

			if (cv != nullptr) {
				cv->SetFromString(value);
				Output = cv->AsString();
			}
			else {
				Output = "not defined";
			}
		}
	}
	else {
		// Try and call an editor function.
		ExecuteScriptCommand(cmd);
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

	for (uint32 i = 0; i < mEntryBufferIndex; i++) {
		char ch = mpEntryBuffer[i];

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

	mEntryBufferIndex = 0;
}


void Console::HandleKeyboard()
{
	if (ControlManager::IsKeyPressed(eKey::FX_KEY_BACKSPACE) && mEntryBufferIndex > 0) {
		// Clear the entire line, Cmd + Delete or Win + Backspace
		if (ControlManager::IsKeyDown(eKey::FX_KEY_LMETA)) {
			mEntryBufferIndex = 0;
		}
		else {
			--mEntryBufferIndex;
		}
	}


	char ch = ControlManager::GetAlphaKey();
	if (ch == 0) {
		return;
	}

	mpEntryBuffer[mEntryBufferIndex++] = ch;

	if (ch == '\n') {
		mpEntryBuffer[mEntryBufferIndex] = '\0';
		ParseCommand();
	}
}

StringView Console::GetString() const { return MakeStringView(mpEntryBuffer, mEntryBufferIndex); }


} // namespace fx
