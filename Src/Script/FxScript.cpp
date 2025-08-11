#include "FxScript.hpp"

#include <stdio.h>
#include <Core/Types.hpp>
#include <Core/FxUtil.hpp>
#include <Core/FxMemory.hpp>

#define FX_SCRIPT_SCOPE_GLOBAL_VARS_START_SIZE 32
#define FX_SCRIPT_SCOPE_LOCAL_VARS_START_SIZE 16

#define FX_SCRIPT_SCOPE_GLOBAL_ACTIONS_START_SIZE 32
#define FX_SCRIPT_SCOPE_LOCAL_ACTIONS_START_SIZE 32

using Token = FxTokenizer::Token;
using TT = FxTokenizer::TokenType;

void FxConfigScript::LoadFile(const char* path)
{
	FILE* fp = FxUtil::FileOpen(path, "rb");
	if (fp == nullptr) {
		Log::Error("Could not open config file at '%s'", path);
		return;
	}

	std::fseek(fp, 0, SEEK_END);
	size_t file_size = std::ftell(fp);
	std::rewind(fp);

	mFileData = FxMemPool::Alloc<char>(file_size);

	size_t read_size = std::fread(mFileData, 1, file_size, fp);
	if (read_size != file_size) {
		Log::Warning("Error reading all data from config file at '%s' (read=%zu, size=%zu)", path, read_size, file_size);
	}

	FxTokenizer tokenizer(mFileData, read_size);
	tokenizer.Tokenize();


	fclose(fp);

	mTokens = std::move(tokenizer.GetTokens());

	/*for (const auto& token : mTokens) {
		token.Print();
	}*/

	mScopes.Create(8);
	mCurrentScope = mScopes.Insert();
	mCurrentScope->Vars.Create(FX_SCRIPT_SCOPE_GLOBAL_VARS_START_SIZE);
	mCurrentScope->Actions.Create(FX_SCRIPT_SCOPE_GLOBAL_ACTIONS_START_SIZE);
}

Token& FxConfigScript::GetToken(int offset)
{
	const uint32 idx = mTokenIndex + offset;
	assert(idx >= 0 && idx <= mTokens.size());
	return mTokens[idx];
}

Token& FxConfigScript::EatToken(TT token_type)
{
	Token& token = GetToken();
	if (token.Type != token_type) {
		printf("[ERROR] %u:%u: Unexpected token type %s when expecting %s!\n", token.FileLine, token.FileColumn, FxTokenizer::GetTypeName(token.Type), FxTokenizer::GetTypeName(token_type));
		mHasErrors = true;
	}
	++mTokenIndex;
	return token;
}

FxAstNode* FxConfigScript::TryParseKeyword()
{
	if (mTokenIndex >= mTokens.size()) {
		return nullptr;
	}

	Token& tk = GetToken();

	FxHash hash = tk.GetHash();

	//constexpr FxHash kw_method = FxHashStr("method");
	constexpr FxHash kw_action = FxHashStr("action");
	constexpr FxHash kw_local = FxHashStr("local");
	constexpr FxHash kw_global = FxHashStr("global");
	constexpr FxHash kw_return = FxHashStr("return");

	if (hash == kw_action) {
		EatToken(TT::Identifier); // [action]
		//ParseActionDeclare();
		return ParseActionDeclare();
	}
	if (hash == kw_local) {
		EatToken(TT::Identifier); // [local]
		return ParseVarDeclare();
	}
	if (hash == kw_global) {
		EatToken(TT::Identifier); // [global]
		return ParseVarDeclare(&mScopes[0]);
	}
	if (hash == kw_return) {
		EatToken(TT::Identifier); // [return]
		FxAstReturn* ret = new FxAstReturn;
		return ret;
	}
	//if (hash == kw_do) {
	//	EatToken(TT::Identifier);
	//	ParseDoCall();
	//	return true;
	//}

	return nullptr;
}

void FxConfigScript::PushScope()
{
	FxScriptScope* current = mCurrentScope;

	FxScriptScope* new_scope = mScopes.Insert();
	new_scope->Parent = current;
	new_scope->Vars.Create(FX_SCRIPT_SCOPE_LOCAL_VARS_START_SIZE);
	new_scope->Actions.Create(FX_SCRIPT_SCOPE_LOCAL_ACTIONS_START_SIZE);

	mCurrentScope = new_scope;
}

void FxConfigScript::PopScope()
{
	FxScriptScope* new_scope = mCurrentScope->Parent;
	mScopes.RemoveLast();

	assert(new_scope == &mScopes.GetLast());

	mCurrentScope = new_scope;
}

FxAstVarDecl* FxConfigScript::ParseVarDeclare(FxScriptScope* scope)
{
	if (scope == nullptr) {
		scope = mCurrentScope;
	}

	Token& type = EatToken(TT::Identifier);
	Token& name = EatToken(TT::Identifier);

	FxAstVarDecl* node = new FxAstVarDecl;
	node->Name = &name;
	node->Type = &type;
	node->DefineAsGlobal = (scope == &mScopes[0]);

	FxScriptVar var{ &type, &name, scope };

	node->Assignment = TryParseAssignment(node->Name);
	/*if (node->Assignment) {
		var.Value = node->Assignment->Value;
	}*/
	mCurrentScope->Vars.Insert(var);

	return node;
}

//FxScriptVar& FxConfigScript::ParseVarDeclare()
//{
//	Token& type = EatToken(TT::Identifier);
//	Token& name = EatToken(TT::Identifier);
//
//	FxScriptVar var{ name.GetHash(), &type, &name };
//
//	TryParseAssignment(var);
//
//	mCurrentScope->Vars.Insert(var);
//
//	return mCurrentScope->Vars.GetLast();
//}

FxScriptVar* FxConfigScript::FindVar(FxHash hashed_name)
{
	FxScriptScope* scope = mCurrentScope;

	while (scope) {
		FxScriptVar* var = scope->FindVarInScope(hashed_name);
		if (var) {
			return var;
		}

		scope = scope->Parent;
	}

	return nullptr;
}

FxScriptAction* FxConfigScript::FindAction(FxHash hashed_name)
{
	FxScriptScope* scope = mCurrentScope;

	while (scope) {
		FxScriptAction* var = scope->FindActionInScope(hashed_name);
		if (var) {
			return var;
		}

		scope = scope->Parent;
	}

	return nullptr;
}

void FxConfigScript::Execute(FxScriptInterpreter& interpreter)
{
	FxAstBlock* root_block = Parse();

	// If there are errors, exit early
	if (mHasErrors || root_block == nullptr) {
		return;
	}

	interpreter.Create(root_block);

	// Copy any external variable declarations from the parser to the interpreter
	FxScriptScope& global_scope = mScopes[0];
	FxScriptScope& interpreter_global_scope = interpreter.mScopes[0];

	for (FxScriptVar& var : global_scope.Vars) {
		if (var.IsExternal) {
			interpreter_global_scope.Vars.Insert(var);
		}
	}

	interpreter.DefineDefaultExternalFunctions();

	interpreter.Interpret();
}

FxScriptValue FxConfigScript::ParseValue()
{
	Token& token = GetToken();
	TT token_type = token.Type;
	FxScriptValue value;

	if (token_type == TT::Identifier) {
		FxScriptVar* var = FindVar(token.GetHash());

		if (var) {
			value.Type = FxScriptValue::REF;

			FxAstVarRef* var_ref = new FxAstVarRef;
			var_ref->Name = var->Name;
			var_ref->Scope = var->Scope;

			value.ValueRef = var_ref;

			EatToken(TT::Identifier);

			return value;
		}
		else {
			// We cannot find the definition for the variable, assume that it is an external variable that will be defined
			// during the interpret stage.
			/*value.Type = FxScriptValue::REF;

			FxAstVarRef* var_ref = new FxAstVarRef;
			var_ref->Name = &token;
			var_ref->Scope = &mScopes[0];

			value.ValueRef = var_ref;*/
			printf("ERROR: Undefined reference to variable ");
			token.Print();
			printf("\n");

			//printf("Undefined reference to variable \"%.*s\"! (Hash:%u)\n", token.Length, token.Start, token.GetHash());
			EatToken(TT::Identifier);
		}
	}

	switch (token_type) {
	case TT::Integer:
		EatToken(TT::Integer);
		value.Type = FxScriptValue::INT;
		value.ValueInt = token.ToInt();
		break;
	case TT::Float:
		EatToken(TT::Float);
		value.Type = FxScriptValue::FLOAT;
		value.ValueFloat = token.ToFloat();
		break;
	case TT::String:
		EatToken(TT::String);
		value.Type = FxScriptValue::STRING;
		value.ValueString = token.GetHeapStr();
		break;
	default:;
	}

	return value;
}

FxAstNode* FxConfigScript::ParseRhs()
{
	if (GetToken().Type == TT::Identifier && GetToken(1).Type == TT::LParen) {
		return ParseActionCall();
	}

	FxScriptValue value = ParseValue();

	FxAstLiteral* literal = new FxAstLiteral;
	literal->Value = value;

	TT op_type = GetToken(0).Type;
	if (op_type == TT::Plus || op_type == TT::Minus) {
		FxAstBinop* binop = new FxAstBinop;

		binop->Left = literal;
		binop->OpToken = &EatToken(op_type);
		binop->Right = ParseRhs();

		return binop;
	}

	return literal;
}

FxAstAssign* FxConfigScript::TryParseAssignment(FxTokenizer::Token* var_name)
{
	if (GetToken().Type != TT::Equals) {
		return nullptr;
	}

	EatToken(TT::Equals);

	FxAstAssign* node = new FxAstAssign;

	FxAstVarRef* var_ref = new FxAstVarRef;
	var_ref->Name = var_name;
	var_ref->Scope = mCurrentScope;
	node->Var = var_ref;

	//node->Value = ParseValue();
	node->Rhs = ParseRhs();

	return node;
}

void FxConfigScript::DefineExternalVar(const char* type, const char* name, const FxScriptValue& value)
{
	Token* name_token = FxMemPool::Alloc<Token>(sizeof(Token));
	Token* type_token = FxMemPool::Alloc<Token>(sizeof(Token));

	{
		const uint32 type_len = strlen(type);
		char* type_buffer = FxMemPool::Alloc<char>(type_len + 1);
		strcpy(type_buffer, type);
		//type_buffer[type_len + 1] = 0;


		type_token->Start = type_buffer;
		type_token->Type = TT::Identifier;
		type_token->Start[type_len] = 0;
		type_token->Length = type_len + 1;
	}

	{
		const uint32 name_len = strlen(name);

		char* name_buffer = FxMemPool::Alloc<char>(name_len + 1);
		strcpy(name_buffer, name);

		name_token->Start = name_buffer;
		name_token->Type = TT::Identifier;
		name_token->Start[name_len] = 0;
		name_token->Length = name_len + 1;
	}

	FxScriptScope* definition_scope = &mScopes[0];

	FxScriptVar var(type_token, name_token, definition_scope, true);
	var.Value = value;

	definition_scope->Vars.Insert(var);
}

//bool FxConfigScript::TryParseAssignment(FxScriptVar& dest)
//{
//	if (GetToken().Type != TT::Equals) {
//		return false;
//	}
//
//	EatToken(TT::Equals);
//
//	dest.Value = ParseValue();
//
//	return false;
//}

FxAstNode* FxConfigScript::ParseCommand()
{
	if (mHasErrors) {
		return nullptr;
	}

	if (mTokenIndex >= mTokens.size()) {
		return nullptr;
	}

	// Eat any extraneous semicolons
	while (GetToken().Type == TT::Semicolon) {
		EatToken(TT::Semicolon);
		if (mTokenIndex >= mTokens.size()) {
			return nullptr;
		}
	}

	FxAstNode* node = TryParseKeyword();

	if (!node && (mTokenIndex < mTokens.size() && GetToken().Type == TT::Identifier)) {
		if (mTokenIndex + 1 < mTokens.size() && GetToken(1).Type == TT::LParen) {
			node = ParseActionCall();
		}
		else if (mTokenIndex + 1 < mTokens.size() && GetToken(1).Type == TT::Equals) {
			Token& assign_name = EatToken(TT::Identifier);
			node = TryParseAssignment(&assign_name);
		}
		else {
			GetToken().Print();
			printf("Print value here\n");
			//node = ParseValue();
		}
	}


	if (!node) {
		return nullptr;
	}

	// Blocks do not require semicolons
	if (node->NodeType == FX_AST_BLOCK || node->NodeType == FX_AST_ACTIONDECL) {
		return node;
	}

	EatToken(TT::Semicolon);

	return node;
}

FxAstBlock* FxConfigScript::ParseBlock()
{
	FxAstBlock* block = new FxAstBlock;

	EatToken(TT::LBrace);

	while (GetToken().Type != TT::RBrace) {
		FxAstNode* command = ParseCommand();
		if (command == nullptr) {
			break;
		}
		block->Statements.push_back(command);
	}

	EatToken(TT::RBrace);

	return block;
}

FxAstActionDecl* FxConfigScript::ParseActionDeclare()
{
	FxAstActionDecl* node = new FxAstActionDecl();

	// Name of the action
	Token& name = EatToken(TT::Identifier);

	node->Name = &name;

	PushScope();
	EatToken(TT::LParen);

	FxAstBlock* params = new FxAstBlock;

	while (true) {
		params->Statements.push_back(ParseVarDeclare());

		if (GetToken().Type == TT::Comma) {
			EatToken(TT::Comma);
			continue;
		}

		break;
	}

	EatToken(TT::RParen);

	if (GetToken().Type != TT::LBrace) {
		FxAstVarDecl* return_decl = ParseVarDeclare();
		node->ReturnVar = return_decl;
	}

	node->Block = ParseBlock();
	PopScope();

	node->Params = params;

	FxScriptAction action(&name, mCurrentScope, node->Block, node);
	mCurrentScope->Actions.Insert(action);

	return node;
}

//FxScriptValue FxConfigScript::TryCallInternalFunc(FxHash func_name, std::vector<FxScriptValue>& params)
//{
//	FxScriptValue return_value;
//
//	for (const FxScriptInternalFunc& func : mInternalFuncs) {
//		if (func.HashedName == func_name) {
//			func.Func(params, &return_value);
//			return return_value;
//		}
//	}
//
//	return return_value;
//}

FxAstActionCall* FxConfigScript::ParseActionCall()
{
	FxAstActionCall* node = new FxAstActionCall;

	Token& name = EatToken(TT::Identifier);

	node->HashedName = name.GetHash();
	node->Action = FindAction(node->HashedName);

	EatToken(TT::LParen);

	while (true) {
		node->Params.push_back(ParseRhs());

		if (GetToken().Type == TT::Comma) {
			EatToken(TT::Comma);
			continue;
		}

		break;
	}

	EatToken(TT::RParen);

	return node;
}



//void FxConfigScript::ParseDoCall()
//{
//	Token& call_name = EatToken(TT::Identifier);
//
//	EatToken(TT::LParen);
//
//	std::vector<FxScriptValue> params;
//
//	while (true) {
//		params.push_back(ParseValue());
//
//		if (GetToken().Type == TT::Comma) {
//			EatToken(TT::Comma);
//			continue;
//		}
//
//		break;
//	}
//
//	EatToken(TT::RParen);
//
//	TryCallInternalFunc(call_name.GetHash(), params);
//
//	printf("Calling ");
//	call_name.Print();
//
//	for (const auto& param : params) {
//		printf("param : ");
//		param.Print();
//	}
//}
//
//


FxAstBlock* FxConfigScript::Parse()
{
	FxAstBlock* root_block = new FxAstBlock;

	FxAstNode* keyword;
	while ((keyword = ParseCommand())) {
		root_block->Statements.push_back(keyword);
	}

	/*for (const auto& var : mCurrentScope->Vars) {
		var.Print();
	}*/

	if (mHasErrors) {
		return nullptr;
	}

	// FxAstPrinter printer(root_block);
	// printer.Print(root_block);

	return root_block;
}



//////////////////////////////////////////
// Script Interpreter
//////////////////////////////////////////

FxScriptInterpreter::FxScriptInterpreter()
{
    mScopes.Create(8);
	mCurrentScope = mScopes.Insert();
	mCurrentScope->Parent = nullptr;
	mCurrentScope->Vars.Create(FX_SCRIPT_SCOPE_LOCAL_VARS_START_SIZE);
	mCurrentScope->Actions.Create(FX_SCRIPT_SCOPE_LOCAL_ACTIONS_START_SIZE);
}

void FxScriptInterpreter::Create(FxAstBlock* root_block)
{
	mRootBlock = root_block;
}


void FxScriptInterpreter::Interpret()
{
	Visit(mRootBlock);

	mScopes[0].PrintAllVarsInScope();
}

void FxScriptInterpreter::PushScope()
{
	FxScriptScope* current = mCurrentScope;

	FxScriptScope* new_scope = mScopes.Insert();
	new_scope->Parent = current;
	new_scope->Vars.Create(FX_SCRIPT_SCOPE_LOCAL_VARS_START_SIZE);
	new_scope->Actions.Create(FX_SCRIPT_SCOPE_LOCAL_ACTIONS_START_SIZE);

	mCurrentScope = new_scope;
}

void FxScriptInterpreter::PopScope()
{
	FxScriptScope* new_scope = mCurrentScope->Parent;
	mScopes.RemoveLast();

	assert(new_scope == &mScopes.GetLast());

	mCurrentScope = new_scope;
}

FxScriptVar* FxScriptInterpreter::FindVar(FxHash hashed_name)
{
	FxScriptScope* scope = mCurrentScope;

	while (scope) {
		FxScriptVar* var = scope->FindVarInScope(hashed_name);
		if (var) {
			return var;
		}

		scope = scope->Parent;
	}

	return nullptr;
}

FxScriptAction* FxScriptInterpreter::FindAction(FxHash hashed_name)
{
	FxScriptScope* scope = mCurrentScope;

	while (scope) {
		FxScriptAction* var = scope->FindActionInScope(hashed_name);
		if (var) {
			return var;
		}

		scope = scope->Parent;
	}

	return nullptr;
}

bool FxScriptInterpreter::CheckExternalCallArgs(FxAstActionCall* call, FxScriptExternalFunc& func)
{
	if (func.IsVariadic) {
		return true;
	}

	if (call->Params.size() != func.ParameterTypes.size()) {
		return false;
	}

	for (int i = 0; i < call->Params.size(); i++) {
		FxScriptValue val = VisitRhs(call->Params[i]);

		if (!(val.Type & func.ParameterTypes[i])) {
			return false;
		}
	}

	return true;
}

FxScriptValue FxScriptInterpreter::VisitExternalCall(FxAstActionCall* call, FxScriptExternalFunc& func)
{
	FxScriptValue return_value;

	PushScope();

	if (!CheckExternalCallArgs(call, func)) {
		printf("!!! Parameters do not match for function call!\n");
		return return_value;
	}

	std::vector<FxScriptValue> params;
	params.reserve(call->Params.size());

	for (FxAstNode* param_node : call->Params) {
		params.push_back(VisitRhs(param_node));
	}

	func.Function(params, &return_value);

	PopScope();

	return return_value;
}

FxScriptValue FxScriptInterpreter::VisitActionCall(FxAstActionCall* call)
{
	FxScriptValue return_value;

	// This is not a local call, check for an internal function
	if (call->Action == nullptr) {
		for (FxScriptExternalFunc& func : mExternalFuncs) {
			if (func.HashedName != call->HashedName) {
				continue;
			}

			return VisitExternalCall(call, func);
		}
	}

	if (call->Action == nullptr) {
		puts("!!! Could not find action!");
		return return_value;
	}

	PushScope();

	std::vector<FxAstNode*> param_decls = call->Action->Declaration->Params->Statements;

	if (call->Params.size() != param_decls.size()) {
		printf("!!! MISMATCHED PARAM COUNTS\n");
		PopScope();

		return return_value;
	}

	// Assign each passed in value to the each parameter declaration
	for (int i = 0; i < param_decls.size(); i++) {
		FxAstVarDecl* decl = reinterpret_cast<FxAstVarDecl*>(param_decls[i]);

		FxScriptVar param(decl->Type, decl->Name, mCurrentScope);
		param.Value = GetImmediateValue(VisitRhs(call->Params[i]));

		mCurrentScope->Vars.Insert(param);
	}


	if (call->Action->Declaration->ReturnVar) {
		FxAstVarDecl* decl = call->Action->Declaration->ReturnVar;

		FxScriptVar return_var(decl->Type, decl->Name, mCurrentScope);
		mCurrentScope->Vars.Insert(return_var);

		mCurrentScope->ReturnVar = &mCurrentScope->Vars.GetLast();

	}

	Visit(call->Action->Block);

	// Acquire the return value from the scope
	if (mCurrentScope->ReturnVar) {
		return_value = GetImmediateValue(mCurrentScope->ReturnVar->Value);
	}

	// mCurrentScope->PrintAllVarsInScope();

	PopScope();

	return return_value;
}

FxScriptValue FxScriptInterpreter::VisitRhs(FxAstNode* node)
{
	if (node->NodeType == FX_AST_LITERAL) {
		FxAstLiteral* literal = reinterpret_cast<FxAstLiteral*>(node);
		return literal->Value;
	}
	else if (node->NodeType == FX_AST_ACTIONCALL) {
		FxAstActionCall* call = reinterpret_cast<FxAstActionCall*>(node);
		return VisitActionCall(call);
	}
	else if (node->NodeType == FX_AST_BINOP) {
		FxAstBinop* binop = reinterpret_cast<FxAstBinop*>(node);

		FxScriptValue lhs = GetImmediateValue(VisitRhs(binop->Left));
		FxScriptValue rhs = GetImmediateValue(VisitRhs(binop->Right));

		float sign = (binop->OpToken->Type == TT::Plus) ? 1.0f : -1.0f;

		FxScriptValue result;

		if (lhs.Type == FxScriptValue::INT) {
			result.ValueInt = lhs.ValueInt;
			result.Type = FxScriptValue::INT;

			if (rhs.Type == FxScriptValue::INT) {
				result.ValueInt += rhs.ValueInt * sign;
			}
			else if (rhs.Type == FxScriptValue::FLOAT) {
				result.ValueInt += rhs.ValueFloat * sign;
			}
		}
		else if (lhs.Type == FxScriptValue::FLOAT) {
			result.ValueFloat = lhs.ValueFloat;
			result.Type = FxScriptValue::FLOAT;

			if (rhs.Type == FxScriptValue::INT) {
				result.ValueFloat += rhs.ValueInt * sign;
			}
			else if (rhs.Type == FxScriptValue::FLOAT) {
				result.ValueFloat += rhs.ValueFloat * sign;
			}
		}

		return result;
	}

	FxScriptValue value{};
	return value;
}

void FxScriptInterpreter::DefineDefaultExternalFunctions()
{
	// log([int | float | string | ref] args...)
	RegisterExternalFunc(
		FxHashStr("log"),
		{},		// Do not check argument types as we handle it here
		[&](std::vector<FxScriptValue>& args, FxScriptValue* return_value)
		{
			printf("fxS: ");

			for (FxScriptValue& arg : args) {
				const FxScriptValue& value = GetImmediateValue(arg);

				switch (value.Type) {
				case FxScriptValue::NONETYPE:
					printf("[none]");
					break;
				case FxScriptValue::INT:
					printf("%d", value.ValueInt);
					break;
				case FxScriptValue::FLOAT:
					printf("%f", value.ValueFloat);
					break;
				case FxScriptValue::STRING:
					printf("%s", value.ValueString);
					break;
				default:
					printf("Unknown type\n");
					break;
				}

				putchar(' ');
			}

			putchar('\n');
		},
		true	// Is variadic?
	);

	// delete([ref] to_delete)


}

void FxScriptInterpreter::VisitAssignment(FxAstAssign* assign)
{
	FxScriptVar* var = FindVar(assign->Var->Name->GetHash());

	if (!var) {
		printf("!!! Could not find variable!\n");
		return;
	}

	constexpr FxHash builtin_int = FxHashStr("int");
	constexpr FxHash builtin_playerid = FxHashStr("playerid");
	constexpr FxHash builtin_float = FxHashStr("float");
	constexpr FxHash builtin_string = FxHashStr("string");

	const FxScriptValue& new_value = GetImmediateValue(VisitRhs(assign->Rhs));

	FxScriptValue::ValueType var_type = FxScriptValue::NONETYPE;

	switch (var->Type->GetHash()) {
	case builtin_playerid:
		[[fallthrough]];
	case builtin_int:
		var_type = FxScriptValue::INT;
		break;
	case builtin_float:
		var_type = FxScriptValue::FLOAT;
		break;
	case builtin_string:
		var_type = FxScriptValue::STRING;
		break;
	default:
		printf("!!! Unknown type for variable %.*s!\n", var->Type->Length, var->Type->Start);
		return;
	}

	if (var_type != new_value.Type) {
		printf("!!! Assignment value type does not match variable type!\n");
		return;
	}


	var->Value = new_value;

	//puts("Visit Assign");
}

void FxScriptInterpreter::Visit(FxAstNode* node)
{
	if (node == nullptr) {
		return;
	}

	if (node->NodeType == FX_AST_BLOCK) {
		//puts("Visit Block");

		FxAstBlock* block = reinterpret_cast<FxAstBlock*>(node);
		for (FxAstNode* child : block->Statements) {
			if (child->NodeType == FX_AST_RETURN) {
				break;
			}

			Visit(child);
		}
	}
	else if (node->NodeType == FX_AST_ACTIONDECL) {
		FxAstActionDecl* actiondecl = reinterpret_cast<FxAstActionDecl*>(node);
		//puts("Visit ActionDecl");

		FxScriptAction action(actiondecl->Name, mCurrentScope, actiondecl->Block, actiondecl);
		mCurrentScope->Actions.Insert(action);

		//Visit(actiondecl->Block);
	}
	else if (node->NodeType == FX_AST_VARDECL) {
		FxAstVarDecl* vardecl = reinterpret_cast<FxAstVarDecl*>(node);
		//puts("Visit VarDecl");

		FxScriptScope* scope = mCurrentScope;
		if (vardecl->DefineAsGlobal) {
			scope = &mScopes[0];
		}

		FxScriptVar var(vardecl->Type, vardecl->Name, scope);
		scope->Vars.Insert(var);

		Visit(vardecl->Assignment);
	}
	else if (node->NodeType == FX_AST_ASSIGN) {
		FxAstAssign* assign = reinterpret_cast<FxAstAssign*>(node);
		VisitAssignment(assign);
	}
	else if (node->NodeType == FX_AST_ACTIONCALL) {
		FxAstActionCall* actioncall = reinterpret_cast<FxAstActionCall*>(node);
		VisitActionCall(actioncall);
	}
	else {
		puts("[UNKNOWN]");
	}
}

// void FxScriptInterpreter::DefineExternalVar(const char* type, const char* name, const FxScriptValue& value)
// {
// 	Token* name_token = FxMemPool::Alloc<Token>(sizeof(Token));
// 	Token* type_token = FxMemPool::Alloc<Token>(sizeof(Token));

// 	{
// 		const uint32 type_len = strlen(type);
// 		char* type_buffer = FxMemPool::Alloc<char>(type_len + 1);
// 		strcpy(type_buffer, type);
// 		//type_buffer[type_len + 1] = 0;


// 		type_token->Start = type_buffer;
// 		type_token->Type = TT::Identifier;
// 		type_token->Start[type_len] = 0;
// 		type_token->Length = type_len + 1;
// 	}

// 	{
// 		const uint32 name_len = strlen(name);

// 		char* name_buffer = FxMemPool::Alloc<char>(name_len + 1);
// 		strcpy(name_buffer, name);

// 		name_token->Start = name_buffer;
// 		name_token->Type = TT::Identifier;
// 		name_token->Start[name_len] = 0;
// 		name_token->Length = name_len + 1;
// 	}

// 	FxScriptScope* definition_scope = &mScopes[0];

// 	FxScriptVar var(type_token, name_token, definition_scope, true);
// 	var.Value = value;

// 	definition_scope->Vars.Insert(var);
// }

const FxScriptValue& FxScriptInterpreter::GetImmediateValue(const FxScriptValue& value)
{
	if (value.Type == FxScriptValue::REF) {
		FxScriptVar* var = FindVar(value.ValueRef->Name->GetHash());
		if (!var) {
			printf("!!! Undefined reference to variable\n");
			value.ValueRef->Name->Print();
			putchar('\n');

			return value;
		}

		return var->Value;
	}

	return value;
}


void FxScriptInterpreter::RegisterExternalFunc(FxHash func_name, std::vector<FxScriptValue::ValueType> param_types, FxScriptExternalFunc::FuncType callback, bool is_variadic)
{
	FxScriptExternalFunc func{
		.HashedName = func_name,
		.Function = callback,
		.ParameterTypes = param_types,
		.IsVariadic = is_variadic,
	};

	mExternalFuncs.push_back(func);
}
