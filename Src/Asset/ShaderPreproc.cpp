#include "ShaderPreproc.hpp"

#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>
#include <functional>
#include <string>

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
	F_TextureCubeArray,
	F_DataTexture2D,
	F_ShadowTexture2D,

	F_StructBuffer,
	F_RWStructBuffer,
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
	"F_TextureCubeArray",
	"F_DataTexture2D",
	"F_ShadowTexture2D",

	"F_StructBuffer",
	"F_RWStructBuffer",
	"F_CBuffer",

	// Test definitions
	"F_PARAMTEST",
};

constexpr const char* FStr(eStringId id) { return scStrings[static_cast<uint32>(id)]; }
constexpr Hash32 FHash(eStringId id) { return HashStr32(FStr(id)); }

static const char* scPermIfDefinedName = "PERMIF";
static const char* scPermIfNotDefinedName = "PERMNOT";
static const char* scPermElseName = "PERMELSE";
static const char* scPermEndIfName = "PERMEND";

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
		// The line number is that of the character at Index, so it moves on when a newline is left behind.
		if (Get() == '\n') {
			++CurrentLine;
		}

		++Index;
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
		if (!MatchesString(s)) {
			return false;
		}

		Skip(static_cast<uint32>(std::strlen(s)));

		return true;
	}

	/**
	 * @brief Checks if the string `s` is at the current position, without consuming anything.
	 */
	bool MatchesString(const char* s) const
	{
		uint32 offset = 0;
		char ch;

		while ((ch = *(s + offset)) != 0) {
			if (Get(offset) != ch) {
				return false;
			}

			++offset;
		}

		return true;
	}

	/// The character before the current position, or 0 at the start of the file.
	char GetPrevious() const { return (Index > 0 && Index <= FileData.Size) ? FileData.pData[Index - 1] : 0; }

public:
	Slice<char> FileData;
	uint32 Index = 0;

	/// One based, as `#line` expects it.
	uint32 CurrentLine = 1;
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


static void ParsePermutation(State& state, Result& result, const SizedArray<ShaderMacro>& macros, bool is_negated);

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
	// F_Texture(texture, binding, set))
	REQUIRE_PARAMS(params, 3);

	const Slice<char>& texture_name = params[0];
	const int32 binding = ParamGetInt(params[1]);
	const int32 set = ParamGetInt(params[2]);

	// LogInfo(LC_SHADER, "Reflected shader: {} at slot {}", String(texture_name.pData, texture_name.Size), slot_n);

	result.GetReflection().emplace_back(eShaderReflectionType::Texture, set, binding);
}

static void ParseDataTexture2DDefinition(const std::vector<Slice<char>>& params, State& state, Result& result)
{
	// F_DataTexture2D(texture, type, binding, set))
	REQUIRE_PARAMS(params, 4);

	const Slice<char>& texture_name = params[0];
	const int32 binding = ParamGetInt(params[2]);
	const int32 set = ParamGetInt(params[3]);

	result.GetReflection().emplace_back(eShaderReflectionType::Texture, set, binding);
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
	PPFuncEntry(FStr(F_TextureCubeArray), true, true, ParseTexture2DDefinition),
	PPFuncEntry(FStr(F_DataTexture2D), true, true, ParseDataTexture2DDefinition),
	PPFuncEntry(FStr(F_ShadowTexture2D), true, true, ParseTexture2DDefinition),

	// Buffer definition macros
	PPFuncEntry(FStr(F_StructBuffer), true, true, ParseStructBufferDefinition),
	PPFuncEntry(FStr(F_RWStructBuffer), true, true, ParseStructBufferDefinition),
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

static bool IsIdentifierChar(char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; }

enum class eDirective
{
	None,
	If,
	IfNot,
	Else,
	EndIf,
};

struct DirectiveEntry
{
	const char* pName;
	eDirective Directive;
};

static const DirectiveEntry scDirectives[] = {
	{ scPermIfDefinedName, eDirective::If },
	{ scPermIfNotDefinedName, eDirective::IfNot },
	{ scPermElseName, eDirective::Else },
	{ scPermEndIfName, eDirective::EndIf },
};

/**
 * @brief Checks if there is a permutation directive at the current position. Nothing is consumed. The directive has to
 * be a whole identifier, so a name that only contains it (e.g. `MYPERMEND`) is not counted
 */
static eDirective PeekDirective(const State& state)
{
	if (IsIdentifierChar(state.GetPrevious())) {
		return eDirective::None;
	}

	for (const DirectiveEntry& entry : scDirectives) {
		if (state.MatchesString(entry.pName) &&
			!IsIdentifierChar(state.Get(static_cast<uint32>(std::strlen(entry.pName))))) {
			return entry.Directive;
		}
	}

	return eDirective::None;
}

static const char* GetDirectiveName(eDirective directive)
{
	for (const DirectiveEntry& entry : scDirectives) {
		if (entry.Directive == directive) {
			return entry.pName;
		}
	}

	return "";
}

static void SkipDirectiveName(State& state, eDirective directive)
{
	state.Skip(static_cast<uint32>(std::strlen(GetDirectiveName(directive))));
}

static void SkipHorizontalWhitespace(State& state)
{
	while (state.Get() == ' ' || state.Get() == '\t') {
		state.NextChar();
	}
}

/**
 * @brief Eats what trails a directive name: an optional `()`, an optional `;` and the rest of the line if there is
 * nothing else on it. Code that follows on the same line is left alone.
 */
static void SkipDirectiveTail(State& state)
{
	SkipHorizontalWhitespace(state);

	if (state.Get() == '(') {
		state.NextChar();
		SkipHorizontalWhitespace(state);
		state.NextIfEqual(')');
	}

	state.NextIfEqual(';');

	SkipHorizontalWhitespace(state);
	state.NextIfEqual('\r');
	state.NextIfEqual('\n');
}

static void SkipLineComment(State& state)
{
	// Leave the newline itself for the caller.
	while (state.Get() && state.Get() != '\n') {
		state.NextChar();
	}
}

static bool AtLineComment(const State& state) { return state.Get() == '/' && state.Get(1) == '/'; }

/**
 * @brief Write out characters as data until a directive is hit, without consuming the directive. Comments are dropped,
 * and are never searched for directives.
 */
static void WriteUntilDirective(State& state, Result& result)
{
	while (state.Get()) {
		if (PeekDirective(state) != eDirective::None) {
			break;
		}

		if (AtLineComment(state)) {
			SkipLineComment(state);
			continue;
		}

		if (ParsePPFuncCall(state, result)) {
			continue;
		}

		WriteCurrentCharToProgram(state, result);
		state.NextChar();
	}
}

/**
 * @brief Skips the part of a conditional that is not included in the final shader, tracking nested conditionals.
 * Stops after the matching PERMEND (and its tail), or after the matching PERMELSE if `stop_at_else` is set.
 *
 * @returns true if it stopped at a PERMELSE, false if it stopped at the PERMEND (or the end of the file).
 */
static bool SkipConditional(State& state, bool stop_at_else)
{
	int32 depth = 1;

	while (state.Get()) {
		if (AtLineComment(state)) {
			SkipLineComment(state);
			continue;
		}

		const eDirective directive = PeekDirective(state);

		switch (directive) {
		case eDirective::If:
		case eDirective::IfNot:
			SkipDirectiveName(state, directive);
			++depth;
			continue;
		case eDirective::Else:
			SkipDirectiveName(state, directive);

			if (depth == 1 && stop_at_else) {
				SkipDirectiveTail(state);
				return true;
			}
			continue;
		case eDirective::EndIf:
			SkipDirectiveName(state, directive);

			if (--depth == 0) {
				SkipDirectiveTail(state);
				return false;
			}
			continue;
		case eDirective::None:
			break;
		}

		state.NextChar();
	}

	LogError(LC_SHADER, "Preproc: Reached the end of the file without a matching {}", scPermEndIfName);
	return false;
}

static void EmitLineMarker(State& state, Result& result)
{
	result.InsertString(std::format("#line {}\n", state.CurrentLine));
}

/**
 * @brief Writes out the active part of a conditional, resolving nested conditionals on the way. Runs until the
 * matching PERMEND has been consumed.
 *
 * `in_true_branch` is whether we are in the branch that was chosen because its condition held. A PERMELSE there means
 * the else branch is skipped, while a PERMELSE in the else branch itself is a mistake.
 */
static void WriteConditional(State& state, Result& result, const SizedArray<ShaderMacro>& macros, bool in_true_branch)
{
	while (true) {
		EmitLineMarker(state, result);

		WriteUntilDirective(state, result);

		const eDirective directive = PeekDirective(state);

		switch (directive) {
		case eDirective::If:
		case eDirective::IfNot:
			SkipDirectiveName(state, directive);
			ParsePermutation(state, result, macros, directive == eDirective::IfNot);
			continue;
		case eDirective::Else:
			SkipDirectiveName(state, directive);

			if (!in_true_branch) {
				LogError(LC_SHADER, "Preproc: Multiple {} in the same conditional", scPermElseName);
			}

			// This branch is the one being kept, so the rest of the conditional is dead.
			SkipConditional(state, false);
			return;
		case eDirective::EndIf:
			SkipDirectiveName(state, directive);
			SkipDirectiveTail(state);
			return;
		case eDirective::None:
			LogError(LC_SHADER, "Preproc: Reached the end of the file without a matching {}", scPermEndIfName);
			return;
		}
	}
}

/**
 * @brief Parses `(MACRO);` after a PERMIF/PERMNOT, then keeps the branch that applies and drops the other one.
 * The directive name itself has to be consumed already.
 */
static void ParsePermutation(State& state, Result& result, const SizedArray<ShaderMacro>& macros, bool is_negated)
{
	static constexpr uint32 scBufferSize = 256;
	char read_macro[scBufferSize];
	uint32 read_index = 0;

	SkipHorizontalWhitespace(state);

	if (state.Get() != '(') {
		LogError(LC_SHADER, "Preproc: Missing '(' on permutation call");
		return;
	}

	state.NextChar();

	while (state.Get() && state.Get() != ')' && state.Get() != '\n') {
		const char ch = state.Get();
		state.NextChar();

		if (ch == '\r') {
			continue;
		}

		if (read_index >= scBufferSize - 1) {
			LogError(LC_SHADER, "Preproc: Variable index is larger than the allocated buffer size");

			// It will cause more issues to break than to just reset the index
			read_index = 0;
		}

		read_macro[read_index++] = ch;
	}

	if (state.Get() != ')') {
		LogError(LC_SHADER, "Preproc: Missing ')' on permutation call");
		return;
	}

	// Eat the ')', the optional ';' and the rest of the line
	state.NextChar();
	state.NextIfEqual(';');
	SkipHorizontalWhitespace(state);
	state.NextIfEqual('\r');
	state.NextIfEqual('\n');

	read_macro[read_index] = 0;

	// Whitespace around the name is not part of it
	const char* name = read_macro;
	while (isspace(static_cast<unsigned char>(*name))) {
		++name;
	}

	uint32 name_length = static_cast<uint32>(std::strlen(name));
	while (name_length > 0 && isspace(static_cast<unsigned char>(name[name_length - 1]))) {
		--name_length;
	}

	bool condition_is_true = false;

	for (const ShaderMacro& macro : macros) {
		// Compared in full, not over the length of one of them: a prefix match would let `PERMIF(USE_NORMAL)` be
		// answered by a defined `USE_NORMAL_MAPS`.
		if (macro.pcName != nullptr && std::strlen(macro.pcName) == name_length &&
			std::strncmp(macro.pcName, name, name_length) == 0) {
			condition_is_true = true;
			break;
		}
	}

	// Flip the condition if we are checking if a macro is _not_ defined
	if (is_negated) {
		condition_is_true = !condition_is_true;
	}

	if (condition_is_true) {
		WriteConditional(state, result, macros, true);
	}
	else if (SkipConditional(state, true)) {
		// Found an else, that branch is the one that applies.
		WriteConditional(state, result, macros, false);
	}
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
			// The replay walks over lines that were already counted while parsing the call.
			const uint32 final_line = state.CurrentLine;

			for (state.Index = origin_index; state.Index < final_index;) {
				WriteCurrentCharToProgram(state, result);
				state.NextChar();
			}

			state.CurrentLine = final_line;
		}

		func->Func(param_list, state, result);
		return true;
	}

	return false;
}


static void PrependMacroDefines(Result& result, const SizedArray<ShaderMacro>& macros)
{
	// TODO: replace this with just passing in the macros to DXC. Why am I not doing that right now? This is dumb!
	std::string defines;

	for (const ShaderMacro& macro : macros) {
		// A macro with no value is a plain switch, and ParsePermutation() has already resolved it
		if (macro.pcName == nullptr || macro.pcValue == nullptr) {
			continue;
		}

		defines += std::format("#define {} {}\n", macro.pcName, macro.pcValue);
	}

	if (defines.empty()) {
		return;
	}

	for (uint32 type_index = 0; type_index < ShaderUtil::scNumShaderTypes; type_index++) {
		DataBuffer& buffer = result.ProgramData[type_index];

		// An empty buffer is how the shader says it has no program of this type, and the compiler skips it on
		// exactly that. Writing the defines into one would turn it into a program with no entry point.
		if (buffer.Size == 0) {
			continue;
		}

		DataBuffer combined;
		combined.SetPageSize(scDataPageSize);

		for (const char ch : defines) {
			combined.Insert(ch);
		}

		for (uint32 index = 0; index < buffer.Size; index++) {
			combined.Insert(buffer[index]);
		}

		buffer = std::move(combined);
	}
}

Result Process(const Slice<char>& data, const SizedArray<ShaderMacro>& macros)
{
	Result result {};
	result.GetBuffer().SetPageSize(scDataPageSize);

	State state(data);

	while (state.Index < data.Size) {
		WriteUntilDirective(state, result);

		const eDirective directive = PeekDirective(state);

		switch (directive) {
		case eDirective::If:
		case eDirective::IfNot:
			SkipDirectiveName(state, directive);
			ParsePermutation(state, result, macros, directive == eDirective::IfNot);
			break;
		case eDirective::Else:
		case eDirective::EndIf:
			LogError(LC_SHADER, "Preproc: {} without a matching {}", GetDirectiveName(directive), scPermIfDefinedName);
			SkipDirectiveName(state, directive);
			SkipDirectiveTail(state);
			break;
		case eDirective::None:
			break;
		}
	}

	PrependMacroDefines(result, macros);

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
