#include "ShaderPreproc.hpp"

#include <array>
#include <cctype>
#include <cstdlib>
#include <functional>

namespace fx {

namespace ShaderPreproc {

static constexpr uint32 scDataPageSize = 512;

enum eStringId
{
	// Program type definitions
	F_PROGRAM,
	FPT_VERTEX,
	FPT_PIXEL,
	FPT_COMPUTE,
	FPT_ALL,

	// Reflection definitions
	F_REFLECT,
	FR_STRUCTBUFFER,
	FR_CBUFFER,
	FR_SAMPLER2D,

	F_Texture2D,
	F_ShadowTexture2D,

	F_StructBuffer,
	F_CBuffer,

	// Test definitions
	F_PARAMTEST,
};

static constexpr const char* scStrings[] = {
	// Program type definitions
	"F_PROGRAM",
	"FPT_VERTEX",
	"FPT_PIXEL",
	"FPT_COMPUTE",
	"FPT_ALL",

	// Reflection definitions
	"F_REFLECT",
	"FR_STRUCTBUFFER",
	"FR_CBUFFER",
	"FR_SAMPLER2D",

	"F_Texture2D",
	"F_ShadowTexture2D",

	"F_StructBuffer",
	"F_CBuffer",

	// Test definitions
	"F_PARAMTEST",
};

constexpr const char* FStr(eStringId id) { return scStrings[static_cast<uint32>(id)]; }
constexpr Hash32 FHash(eStringId id) { return HashStr32(FStr(id)); }


struct State
{
public:
	State(const Slice<char> data) : FileData(data) {}

	char Get(uint32 offset) const
	{
		uint32 idx = Index + offset;
		if (idx >= FileData.Size) {
			return 0;
		}

		return FileData.pData[idx];
	}

	char* GetCurrentPtr() { return FileData.pData + Index; }

	char Get() const { return Get(0); }

	void NextChar()
	{
		++Index;

		if (Get() == '\n') {
			++CurrentLine;
		}
	}

	void Skip(uint32 skip)
	{
		for (uint32 i = 0; i < skip; i++) {
			NextChar();
		}
	}

	void NextIfEqual(char p)
	{
		if (Get() == p) {
			NextChar();
		}
	}

	/**
	 * @brief Attempts to read the string `s` from the buffer. If it exists, the function returns true and skips past
	 * the string. If it was not found, the buffer remains in the original position.
	 */
	bool TryReadString(const char* s)
	{
		uint32 offset = 0;
		char ch;

		while ((ch = *(s + offset)) != 0) {
			if (Get(offset) != ch) {
				return false;
			}

			++offset;
		}

		Skip(offset);

		return true;
	}

public:
	Slice<char> FileData;
	uint32 Index = 0;

	uint32 CurrentLine = 0;
};

enum class eParseResult
{
	Error,
	Success,
};

struct PPFuncEntry
{
	using FuncType = void(const std::vector<Slice<char>>& param, State& state, Result& result);

	PPFuncEntry() = delete;

	PPFuncEntry(const char* name, bool has_parameters, bool do_not_eat, const std::function<FuncType> func)
		: pName(name), bHasParameters(has_parameters), bDoNotEat(do_not_eat), Func(func)
	{
	}

	const char* pName;
	bool bHasParameters;
	bool bDoNotEat;
	const std::function<FuncType> Func;
};

#define REQUIRE_PARAMS(_params, _amt_req)                                                                              \
	if (_params.size() < _amt_req) {                                                                                   \
		LogError(LC_SHADER, "Not enough parameters found in preprocessor function!");                                  \
		return;                                                                                                        \
	}

static int32 ParamGetInt(const Slice<char>& param)
{
	char* endptr;

	// hacky...
	char restore = 0;
	std::swap(restore, *(param.pData + param.Size));
	const int32 value = std::strtol(param.pData, &endptr, 10);
	std::swap(restore, *(param.pData + param.Size));

	return value;
}

static bool ParsePPFuncCall(State& state, Result& result);

/////////////////////////////////////
// Preprocessor Definitions
/////////////////////////////////////


static bool ParseIfdef(State& state, Result& result, const SizedArray<ShaderMacro>& macros,
					   bool ifdef_directive_is_eaten, bool force_is_not_defined);

static void ParseProgramDefinition(const std::vector<Slice<char>>& params, State& state, Result& result)
{
	REQUIRE_PARAMS(params, 1);

	Hash32 type_hash = HashData32(params[0]);

	switch (type_hash) {
	case FHash(FPT_VERTEX):
		result.SetCurrentShader(eShaderType::Vertex);
		break;
	case FHash(FPT_PIXEL):
		result.SetCurrentShader(eShaderType::Pixel);
		break;
	case FHash(FPT_COMPUTE):
		result.SetCurrentShader(eShaderType::Compute);
		break;
	case FHash(FPT_ALL):
		result.bBroadcastToAllPrograms = true;
		return;
	default:;
	}

	// Notify DXC about the actual line number to make the HLSL error messages less ass
	result.InsertString(std::format("#line {}\n", state.CurrentLine));
}


static void ParseReflectionDefinition(const std::vector<Slice<char>>& params, State& state, Result& result)
{
	REQUIRE_PARAMS(params, 3);

	const Slice<char>& refl_type = params[0];

	const int32 binding = ParamGetInt(params[1]);
	const int32 set = ParamGetInt(params[2]);

	Hash32 refl_hash = HashData32(refl_type);
	eShaderReflectionType type = eShaderReflectionType::StructuredBuffer;

	switch (refl_hash) {
	case FHash(FR_STRUCTBUFFER):
		type = eShaderReflectionType::StructuredBuffer;
		break;
	case FHash(FR_CBUFFER):
		type = eShaderReflectionType::CBuffer;
		break;
	default:;
	}

	const char* shader_refl_type[] = { "Structured Buffer", "Uniform Buffer", "Texture2D" };

	// LogInfo(LC_SHADER, "Reflected shader: {} at Binding={}, Set={}", shader_refl_type[static_cast<uint32>(type)],
	// 		binding, set);

	result.GetReflection().emplace_back(type, set, binding);
}


static void ParseParamTestDefinition(const std::vector<Slice<char>>& params, State& state, Result& result)
{
	printf("== PARAMTEST ==\n");

	for (const Slice<char>& param : params) {
		printf("PARAMETER: '%.*s'\n", static_cast<uint32>(param.Size), param.pData);
	}

	printf("=====\n");
}


static void ParseTexture2DDefinition(const std::vector<Slice<char>>& params, State& state, Result& result)
{
	// F_Texture(texture, register_n))
	REQUIRE_PARAMS(params, 2);

	const Slice<char>& texture_name = params[0];
	const int32 slot_n = ParamGetInt(params[1]);

	// LogInfo(LC_SHADER, "Reflected shader: {} at slot {}", String(texture_name.pData, texture_name.Size), slot_n);

	result.GetReflection().emplace_back(eShaderReflectionType::Texture, 0, slot_n);
}

static void ParseStructBufferDefinition(const std::vector<Slice<char>>& params, State& state, Result& result)
{
	// F_StructBuffer(name, objtype, binding, set)
	REQUIRE_PARAMS(params, 4);

	const Slice<char>& buffer_name = params[0];
	const Slice<char>& objtype = params[1];
	const int32 binding = ParamGetInt(params[2]);
	const int32 set = ParamGetInt(params[3]);

	// LogInfo(LC_SHADER, "Reflected shader: {} (type={}) at Binding={}, Set={}",
	// 		String(buffer_name.pData, buffer_name.Size), String(objtype.pData, objtype.Size), binding, set);

	result.GetReflection().emplace_back(eShaderReflectionType::StructuredBuffer, set, binding);
}

static void ParseCBufferDefinition(const std::vector<Slice<char>>& params, State& state, Result& result)
{
	// F_CBuffer(name, binding, set)
	REQUIRE_PARAMS(params, 3);

	const Slice<char>& buffer_name = params[0];
	const int32 binding = ParamGetInt(params[1]);
	const int32 set = ParamGetInt(params[2]);

	// LogInfo(LC_SHADER, "Reflected shader: {} at Binding={}, Set={}", String(buffer_name.pData, buffer_name.Size),
	// 		binding, set);

	result.GetReflection().emplace_back(eShaderReflectionType::CBuffer, set, binding);
}

static const PPFuncEntry PPFunctions[] = {
	PPFuncEntry(FStr(F_PROGRAM), true, false, ParseProgramDefinition),
	PPFuncEntry(FStr(F_REFLECT), true, false, ParseReflectionDefinition),
	PPFuncEntry(FStr(F_PARAMTEST), true, false, ParseParamTestDefinition),

	// Texture definition macros
	PPFuncEntry(FStr(F_Texture2D), true, true, ParseTexture2DDefinition),
	PPFuncEntry(FStr(F_ShadowTexture2D), true, true, ParseTexture2DDefinition),

	// Buffer definition macros
	PPFuncEntry(FStr(F_StructBuffer), true, true, ParseStructBufferDefinition),
	PPFuncEntry(FStr(F_CBuffer), true, true, ParseCBufferDefinition),

};

static void WriteCurrentCharToProgram(State& state, Result& result)
{
	char ch = state.Get();

	if (result.bBroadcastToAllPrograms) {
		// Only broadcast to vertex and pixel shaders
		result.GetBuffer(eShaderType::Vertex).Insert(ch);
		result.GetBuffer(eShaderType::Pixel).Insert(ch);

		return;
	}

	result.GetBuffer().Insert(ch);
}


static void WriteUntilHash(State& state, Result& result)
{
	while (state.Get() != '#') {
		if (ParsePPFuncCall(state, result)) {
			continue;
		}

		// Continue saving each character until the condition is closed
		WriteCurrentCharToProgram(state, result);
		state.NextChar();
	}
};


static void SkipUntilHash(State& state)
{
	while (state.Get() != '#') {
		// Continue saving each character until the condition is closed
		state.NextChar();
	}
};

static void SkipUntilEndif(State& state)
{
	int32 scope_index = 1;

	while (scope_index > 0) {
		if (state.TryReadString("#endif")) {
			--scope_index;
			continue;
		}
		else if (state.TryReadString("#ifdef")) {
			++scope_index;
			continue;
		}
		else {
			// Skip the hash character to be able to jump to the next one
			state.NextChar();
		}

		SkipUntilHash(state);
	}
}

static bool SkipUntilElse(State& state)
{
	int32 scope_index = 1;

	while (scope_index > 0) {
		if (state.TryReadString("#endif")) {
			--scope_index;
			continue;
		}
		else if (state.TryReadString("#ifdef")) {
			++scope_index;
			continue;
		}
		else if (state.TryReadString("#else") && scope_index <= 1) {
			return true;
		}
		else {
			// Skip the hash character to be able to jump to the next one
			state.NextChar();
		}

		SkipUntilHash(state);
	}

	return false;
}

static void EmitLineMarker(State& state, Result& result)
{
	result.InsertString(std::format("#line {}\n", state.CurrentLine));
}


static void DoIfSection(State& state, Result& result, const SizedArray<ShaderMacro>& macros)
{
	while (true) {
		EmitLineMarker(state, result);

		// Write until the next hash character is encountered.
		WriteUntilHash(state, result);

		// If it is an ifdef, parse it. This also eats the else's and endifs for that ifdef block.
		if (state.TryReadString("#ifdef")) {
			ParseIfdef(state, result, macros, true, false);

			// Continue back to writing after encountering the nested ifdef.
			continue;
		}
		else if (state.TryReadString("#ifndef")) {
			ParseIfdef(state, result, macros, true, true);

			// Continue back to writing after encountering the nested ifdef.
			continue;
		}

		// We encountered an else block. Since this condition is true, skip all data inside of the else directive.
		if (state.TryReadString("#else")) {
			SkipUntilEndif(state);
			break;
		}


		// There are no else directives or nested blocks, just a direct endif. End the loop.
		if (state.TryReadString("#endif")) {
			break;
		}
	}
}

static void DoElseSection(State& state, Result& result, const SizedArray<ShaderMacro>& macros)
{
	bool found_else = SkipUntilElse(state);

	while (found_else) {
		EmitLineMarker(state, result);

		// Write until the next hash character is encountered.
		WriteUntilHash(state, result);

		// If it is an ifdef, parse it. This also eats the else's and endifs for that ifdef block.
		if (state.TryReadString("#ifdef")) {
			ParseIfdef(state, result, macros, true, false);

			// Continue back to writing after encountering the nested ifdef.
			continue;
		}
		else if (state.TryReadString("#ifndef")) {
			ParseIfdef(state, result, macros, true, true);

			// Continue back to writing after encountering the nested ifndef.
			continue;
		}

		if (state.TryReadString("#endif")) {
			break;
		}
	}
}

static bool ParseIfdef(State& state, Result& result, const SizedArray<ShaderMacro>& macros,
					   bool ifdef_directive_is_eaten, bool force_is_not_defined)
{
	static constexpr uint32 scBufferSize = 256;
	char read_macro[scBufferSize];
	uint32 read_index = 0;

	const char* directive = "#ifdef";


	bool directive_found = false;

	if (!ifdef_directive_is_eaten) {
		directive_found = state.TryReadString("#ifdef");

		if (!directive_found && (state.TryReadString("#ifndef"))) {
			directive_found = true;
			force_is_not_defined = true;
		}
	}


	if (ifdef_directive_is_eaten || directive_found) {
		// Skip whitespace
		while (isspace(state.Get())) {
			state.NextChar();
		}

		read_index = 0;

		while (state.Get() != '\n') {
			char ch = state.Get();

			if (ch == '\r') {
				state.NextChar();
				continue;
			}

			if (read_index >= scBufferSize) {
				LogError(LC_SHADER, "Preproc: Variable index is larger than the allocated buffer size");

				// It will cause more issues to break than to just reset the index
				read_index = 0;
			}

			read_macro[read_index++] = ch;
			state.NextChar();
		}

		state.NextIfEqual('\r');
		state.NextIfEqual('\n');

		read_macro[read_index] = 0;

		bool condition_is_true = false;

		for (const ShaderMacro& macro : macros) {
			if (!std::strncmp(macro.pcName, read_macro, read_index)) {
				condition_is_true = true;
				break;
			}
		}

		// Flip the condition if we are checking if a macro is _not_ defined
		if (force_is_not_defined) {
			condition_is_true = !condition_is_true;
		}

		if (condition_is_true) {
			DoIfSection(state, result, macros);
		}
		else {
			DoElseSection(state, result, macros);
		}

		// Hit endif, finish
		state.NextIfEqual('\r');
		state.NextIfEqual('\n');

		return true;
	}

	return false;
}

static bool ParsePPFuncCall(State& state, Result& result)
{
	const PPFuncEntry* func = nullptr;

	// The start index before parsing a preprocessor function
	uint32 origin_index = state.Index;

	for (uint32 index = 0; index < std::size(PPFunctions); index++) {
		func = &PPFunctions[index];

		if (state.TryReadString(func->pName)) {
			break;
		}

		func = nullptr;
	}

	bool do_not_eat = (func) ? func->bDoNotEat : false;


	if (func && func->bHasParameters) {
		state.NextChar(); // Skip LParen

		Slice<char> param(state.GetCurrentPtr(), 0);

		std::vector<Slice<char>> param_list;

		// Parse arguments
		while (state.Get() != ')') {
			if (state.Get() == ',') {
				param_list.push_back(param);

				state.NextChar(); // Skip comma
				while (std::isspace(state.Get())) {
					state.NextChar();
				}

				param.pData = state.GetCurrentPtr();
				param.Size = 0;

				continue;
			}

			++param.Size;
			state.NextChar();
		}


		// Push the final parameter
		param_list.push_back(param);
		state.NextChar(); // Skip RParen

		state.NextIfEqual(';');

		state.NextIfEqual('\r');
		state.NextIfEqual('\n');

		// After parsing the preprocessor function, emit the call as text if the function is marked to.
		uint32 final_index = state.Index;
		if (do_not_eat) {
			for (state.Index = origin_index; state.Index < final_index;) {
				WriteCurrentCharToProgram(state, result);
				state.NextChar();
			}
		}

		func->Func(param_list, state, result);
		return true;
	}

	return false;
}


Result Process(const Slice<char>& data, const SizedArray<ShaderMacro>& macros)
{
	Result result {};
	result.GetBuffer().SetPageSize(scDataPageSize);

	State state(data);

	while (state.Index < data.Size) {
		// If there is a comment, skip until the end
		if (state.Get() == '/' && state.Get(1) == '/') {
			char ch;
			while ((ch = state.Get()) && ch != '\n') {
				state.NextChar();
			}
		}

		if (ParsePPFuncCall(state, result)) {
			continue;
		}

		if (state.Get() == '#' && ParseIfdef(state, result, macros, false, false)) {
			continue;
		}

		WriteCurrentCharToProgram(state, result);
		state.NextChar();
	}

	return result;
}


static void SaveProgramToDisk(const char* name, eShaderType shader_type, const Result& result)
{
	const DataBuffer& buffer = result.ProgramData[static_cast<uint32>(shader_type)];

	if (buffer.Size > 0) {
		std::string sname = std::string(name) + "_" + ShaderUtil::TypeToName(shader_type) + ".hlsl";
		File file(sname.c_str(), File::eModType::Write, File::eDataType::Binary);
		file.Write(Slice<char>(buffer.pData, buffer.Size));
		file.Close();
	}
}

void DebugSaveToDisk(const char* name, const Result& result)
{
	SaveProgramToDisk(name, eShaderType::Vertex, result);
	SaveProgramToDisk(name, eShaderType::Pixel, result);
	SaveProgramToDisk(name, eShaderType::Compute, result);
}

}; // namespace ShaderPreproc

} // namespace fx
