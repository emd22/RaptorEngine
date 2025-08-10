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

	constexpr FxHash kw_action = FxHashStr("action");
	constexpr FxHash kw_local = FxHashStr("local");


	if (hash == kw_action) {
		EatToken(TT::Identifier); // [action]
		//ParseActionDeclare();
		return ParseActionDeclare();
	}
	if (hash == kw_local) {
		EatToken(TT::Identifier);
		return ParseVarDeclare();
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

FxAstVarDecl* FxConfigScript::ParseVarDeclare()
{
	Token& type = EatToken(TT::Identifier);
	Token& name = EatToken(TT::Identifier);

	FxAstVarDecl* node = new FxAstVarDecl;
	node->Name = &name;
	node->Type = &type;

	FxScriptVar var{ &type, &name, mCurrentScope };

	node->Assignment = TryParseAssignment(node->Name);
	if (node->Assignment) {
		var.Value = node->Assignment->Value;
	}
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
			printf("Undefined reference to variable \"%.*s\"!\n", token.Length, token.Start);
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

	node->Value = ParseValue();

	return node;
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
		node->Params.push_back(ParseValue());

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

	FxAstPrinter printer(root_block);
	printer.Print(root_block);



	return root_block;
}




//////////////////////////////////////////
// Script Interpreter
//////////////////////////////////////////




void FxScriptInterpreter::Interpret()
{
	mScopes.Create(8);
	mCurrentScope = mScopes.Insert();
	mCurrentScope->Parent = nullptr;
	mCurrentScope->Vars.Create(FX_SCRIPT_SCOPE_LOCAL_VARS_START_SIZE);
	mCurrentScope->Actions.Create(FX_SCRIPT_SCOPE_LOCAL_ACTIONS_START_SIZE);

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

bool FxScriptInterpreter::CheckInternalCallArgs(FxAstActionCall* call, FxScriptInternalFunc& func)
{
	if (func.IsVariadic) {
		return true;
	}

	if (call->Params.size() != func.ParameterTypes.size()) {
		return false;
	}

	for (int i = 0; i < call->Params.size(); i++) {
		FxScriptValue& val = call->Params[i];

		if (val.Type != func.ParameterTypes[i]) {
			return false;
		}
	}

	return true;
}

void FxScriptInterpreter::VisitInternalCall(FxAstActionCall* call, FxScriptInternalFunc& func)
{
	PushScope();

	if (!CheckInternalCallArgs(call, func)) {
		printf("!!! Parameters do not match for function call!\n");
		return;
	}

	std::vector<FxScriptValue> params;
	params.reserve(call->Params.size());

	for (FxScriptValue& param : call->Params) {
		params.push_back(param);
	}

	FxScriptValue return_value;
	func.Function(params, &return_value);

	PopScope();
}

void FxScriptInterpreter::VisitActionCall(FxAstActionCall* call)
{
	//puts("Visit Call");

	// This is not a local call, check for an internal function
	if (call->Action == nullptr) {
		for (FxScriptInternalFunc& func : mInternalFuncs) {
			if (func.HashedName != call->HashedName) {
				continue;
			}

			VisitInternalCall(call, func);
			return;
		}
	}

	if (call->Action == nullptr) {
		puts("!!! Could not find action!");
		return;
	}

	PushScope();

	std::vector<FxAstNode*> param_decls = call->Action->Declaration->Params->Statements;

	if (call->Params.size() != param_decls.size()) {
		printf("!!! MISMATCHED PARAM COUNTS\n");
		PopScope();

		return;
	}

	// Assign each passed in value to the each parameter declaration
	for (int i = 0; i < param_decls.size(); i++) {
		FxAstVarDecl* decl = reinterpret_cast<FxAstVarDecl*>(param_decls[i]);

		FxScriptVar param(decl->Type, decl->Name, mCurrentScope);
		param.Value = GetImmediateValue(call->Params[i]);

		mCurrentScope->Vars.Insert(param);
	}

	Visit(call->Action->Block);

	mCurrentScope->PrintAllVarsInScope();

	PopScope();
}



void FxScriptInterpreter::VisitAssignment(FxAstAssign* assign)
{
	FxScriptVar* var = FindVar(assign->Var->Name->GetHash());

	if (!var) {
		printf("!!! Could not find variable!\n");
		return;
	}

	var->Value = GetImmediateValue(assign->Value);

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

		FxScriptVar var(vardecl->Type, vardecl->Name, mCurrentScope);
		mCurrentScope->Vars.Insert(var);

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

FxScriptValue& FxScriptInterpreter::GetImmediateValue(FxScriptValue& value)
{
	if (value.Type == FxScriptValue::REF) {
		FxScriptVar* var = FindVar(value.ValueRef->Name->GetHash());
		if (!var) {
			printf("!!! Could not evaluate ref!\n");
			return value;
		}

		return var->Value;
	}

	return value;
}


void FxScriptInterpreter::RegisterInternalFunc(FxHash func_name, std::vector<FxScriptValue::ValueType> param_types, FxScriptInternalFunc::FuncType callback, bool is_variadic)
{
	FxScriptInternalFunc func{
		.HashedName = func_name,
		.Function = callback,
		.ParameterTypes = param_types,
		.IsVariadic = is_variadic,
	};

	mInternalFuncs.push_back(func);
}
