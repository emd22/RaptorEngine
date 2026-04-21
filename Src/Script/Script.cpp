// #include "Script.hpp"

// #include <stdio.h>

// #include <Core/Memory.hpp>
// #include <Core/Types.hpp>
// #include <Core/Util.hpp>

// #define FX_SCRIPT_SCOPE_GLOBAL_VARS_START_SIZE 32
// #define FX_SCRIPT_SCOPE_LOCAL_VARS_START_SIZE  16

// #define FX_SCRIPT_SCOPE_GLOBAL_ACTIONS_START_SIZE 32
// #define FX_SCRIPT_SCOPE_LOCAL_ACTIONS_START_SIZE  32

// using Token = Tokenizer::Token;
// using TT = Tokenizer::TokenType;

// ScriptValue ScriptValue::None {};

// void ConfigScript::LoadFile(const char* path)
// {
//     FILE* fp = Util::FileOpen(path, "rb");
//     if (fp == nullptr) {
//         printf("[ERROR] Could not open config file at '%s'\n", path);
//         return;
//     }

//     std::fseek(fp, 0, SEEK_END);
//     size_t file_size = std::ftell(fp);
//     std::rewind(fp);

//     mFileData = FX_SCRIPT_ALLOC_MEMORY(char, file_size);

//     size_t read_size = std::fread(mFileData, 1, file_size, fp);
//     if (read_size != file_size) {
//         printf("[WARNING] Error reading all data from config file at '%s' "
//                "(read=%zu, size=%zu)\n",
//                path, read_size, file_size);
//     }

//     Tokenizer tokenizer(mFileData, read_size);
//     tokenizer.Tokenize();

//     fclose(fp);

//     mTokens = std::move(tokenizer.GetTokens());

//     /*for (const auto& token : mTokens) {
//         token.Print();
//     }*/

//     mScopes.Create(8);
//     mCurrentScope = mScopes.Insert();
//     mCurrentScope->Vars.Create(FX_SCRIPT_SCOPE_GLOBAL_VARS_START_SIZE);
//     mCurrentScope->Actions.Create(FX_SCRIPT_SCOPE_GLOBAL_ACTIONS_START_SIZE);
// }

// Token& ConfigScript::GetToken(int offset)
// {
//     const uint32 idx = mTokenIndex + offset;
//     if (idx < 0 || idx >= mTokens.Size()) {
//         printf("SOMETHING IS MISSING\n");
//     }
//     assert(idx >= 0 && idx <= mTokens.Size());
//     return mTokens[idx];
// }

// Token& ConfigScript::EatToken(TT token_type)
// {
//     Token& token = GetToken();
//     if (token.Type != token_type) {
//         printf("[ERROR] %u:%u: Unexpected token type %s when expecting %s!\n", token.FileLine, token.FileColumn,
//                Tokenizer::GetTypeName(token.Type), Tokenizer::GetTypeName(token_type));
//         mHasErrors = true;
//     }
//     ++mTokenIndex;
//     return token;
// }

// void PrintDocCommentExample(Tokenizer::Token* comment, int example_tag_length, bool is_command_mode)
// {
//     char* start = comment->Start + example_tag_length;

//     for (int i = 0; i < comment->Length - example_tag_length; i++) {
//         char ch = start[i];

//         if (!is_command_mode) {
//             putchar(ch);
//             continue;
//         }

//         // If we are in command mode, print the example using the command syntax
//         if (ch == '(' || ch == ')' || ch == ',') {
//             putchar(' ');
//             continue;
//         }

//         if (ch == ' ' || ch == '\t' || ch == ';') {
//             continue;
//         }

//         putchar(ch);
//     }

//     puts("");
// }

// void PrintDocComment(Tokenizer::Token* comment, bool is_command_mode)
// {
//     const char* example_tag = "EX: ";
//     const int example_tag_length = 4;

//     char* start = comment->Start;

//     if (comment->Length > example_tag_length && !strncmp(start, example_tag, example_tag_length)) {
//         printf("\n\t-> Script : ");
//         PrintDocCommentExample(comment, example_tag_length, false);

//         printf("\t-> Command: ");
//         PrintDocCommentExample(comment, example_tag_length, true);

//         return;
//     }

//     printf("%.*s\n", comment->Length, start);
// }

// AstNode* ConfigScript::TryParseKeyword()
// {
//     if (mTokenIndex >= mTokens.Size()) {
//         return nullptr;
//     }

//     Token& tk = GetToken();
//     Hash32 hash = tk.GetHash();

//     // action [name] ( < [arg type] [arg name] ...> ) { <statements...> }
//     constexpr Hash32 kw_action = HashStr32("action");

//     // local [type] [name] <?assignment> ;
//     constexpr Hash32 kw_local = HashStr32("local");

//     // global [type] [name] <?assignment> ;
//     constexpr Hash32 kw_global = HashStr32("global");

//     // return ;
//     constexpr Hash32 kw_return = HashStr32("return");

//     // help [name of action] ;
//     constexpr Hash32 kw_help = HashStr32("help");

//     if (hash == kw_action) {
//         EatToken(TT::Identifier);
//         // ParseActionDeclare();
//         return ParseActionDeclare();
//     }
//     if (hash == kw_local) {
//         EatToken(TT::Identifier);
//         return ParseVarDeclare();
//     }
//     if (hash == kw_global) {
//         EatToken(TT::Identifier);
//         return ParseVarDeclare(&mScopes[0]);
//     }
//     if (hash == kw_return) {
//         EatToken(TT::Identifier);
//         AstReturn* ret = FX_SCRIPT_ALLOC_NODE(AstReturn);
//         return ret;
//     }
//     if (hash == kw_help) {
//         EatToken(TT::Identifier);

//         Tokenizer::Token& func_ref = EatToken(TT::Identifier);

//         ScriptAction* action = FindAction(func_ref.GetHash());

//         if (action) {
//             for (AstDocComment* comment : action->Declaration->DocComments) {
//                 printf("[DOC] %.*s: ", action->Name->Length, action->Name->Start);
//                 PrintDocComment(comment->Comment, mInCommandMode);
//             }
//         }

//         return nullptr;
//     }

//     return nullptr;
// }

// void ConfigScript::PushScope()
// {
//     ScriptScope* current = mCurrentScope;

//     ScriptScope* new_scope = mScopes.Insert();
//     new_scope->Parent = current;
//     new_scope->Vars.Create(FX_SCRIPT_SCOPE_LOCAL_VARS_START_SIZE);
//     new_scope->Actions.Create(FX_SCRIPT_SCOPE_LOCAL_ACTIONS_START_SIZE);

//     mCurrentScope = new_scope;
// }

// void ConfigScript::PopScope()
// {
//     ScriptScope* new_scope = mCurrentScope->Parent;
//     mScopes.RemoveLast();

//     assert(new_scope == &mScopes.GetLast());

//     mCurrentScope = new_scope;
// }

// AstVarDecl* ConfigScript::ParseVarDeclare(ScriptScope* scope)
// {
//     if (scope == nullptr) {
//         scope = mCurrentScope;
//     }

//     Token& type = EatToken(TT::Identifier);
//     Token& name = EatToken(TT::Identifier);

//     AstVarDecl* node = FX_SCRIPT_ALLOC_NODE(AstVarDecl);

//     node->Name = &name;
//     node->Type = &type;
//     node->DefineAsGlobal = (scope == &mScopes[0]);

//     ScriptVar var { &type, &name, scope };

//     node->Assignment = TryParseAssignment(node->Name);
//     /*if (node->Assignment) {
//         var.Value = node->Assignment->Value;
//     }*/
//     mCurrentScope->Vars.Insert(var);

//     return node;
// }

// // ScriptVar& ConfigScript::ParseVarDeclare()
// //{
// //     Token& type = EatToken(TT::Identifier);
// //     Token& name = EatToken(TT::Identifier);
// //
// //     ScriptVar var{ name.GetHash(), &type, &name };
// //
// //     TryParseAssignment(var);
// //
// //     mCurrentScope->Vars.Insert(var);
// //
// //     return mCurrentScope->Vars.GetLast();
// // }

// ScriptVar* ConfigScript::FindVar(Hash32 hashed_name)
// {
//     ScriptScope* scope = mCurrentScope;

//     while (scope) {
//         ScriptVar* var = scope->FindVarInScope(hashed_name);
//         if (var) {
//             return var;
//         }

//         scope = scope->Parent;
//     }

//     return nullptr;
// }

// ScriptExternalFunc* ConfigScript::FindExternalAction(Hash32 hashed_name)
// {
//     for (ScriptExternalFunc& func : mExternalFuncs) {
//         if (func.HashedName == hashed_name) {
//             return &func;
//         }
//     }

//     return nullptr;
// }

// ScriptAction* ConfigScript::FindAction(Hash32 hashed_name)
// {
//     ScriptScope* scope = mCurrentScope;

//     while (scope) {
//         ScriptAction* var = scope->FindActionInScope(hashed_name);
//         if (var) {
//             return var;
//         }

//         scope = scope->Parent;
//     }

//     return nullptr;
// }

// void ConfigScript::Execute(ScriptVM& vm)
// {
//     DefineDefaultExternalFunctions();

//     mRootBlock = Parse();

//     // If there are errors, exit early
//     if (mHasErrors || mRootBlock == nullptr) {
//         return;
//     }
//     printf("\n=====\n");
//     ScriptBCEmitter emitter;
//     emitter.BeginEmitting(mRootBlock);

//     printf("\n=====\n");

//     ScriptBCPrinter printer(emitter.mBytecode);
//     printer.Print();

//     printf("\n=====\n");

//     for (ScriptBytecodeVarHandle& handle : emitter.VarHandles) {
//         printf("Var(%u) AT %lld\n", handle.HashedName, handle.Offset);
//     }

//     // return;

//     vm.mExternalFuncs = mExternalFuncs;
//     vm.Start(std::move(emitter.mBytecode));

//     for (ScriptBytecodeVarHandle& handle : emitter.VarHandles) {
//         printf("Var(%u) AT %lld -> %u\n", handle.HashedName, handle.Offset, vm.Stack[handle.Offset]);
//     }

//     return;
//     /*
//     interpreter.Create(mRootBlock);

//     // Copy any external variable declarations from the parser to the interpreter
//     ScriptScope& global_scope = mScopes[0];
//     ScriptScope& interpreter_global_scope = interpreter.mScopes[0];

//     for (ScriptVar& var : global_scope.Vars) {
//         if (var.IsExternal) {
//             interpreter_global_scope.Vars.Insert(var);
//         }
//     }

//     interpreter.mExternalFuncs = mExternalFuncs;

//     interpreter.Interpret();*/
// }

// bool ConfigScript::ExecuteUserCommand(const char* command, ScriptInterpreter& interpreter)
// {
//     mHasErrors = false;

//     // If there are errors, exit early
//     if (mRootBlock == nullptr) {
//         return false;
//     }

//     uint32 new_token_index = mTokens.Size();

//     uint32 old_token_index = mTokenIndex;

//     {
//         uint32 length_of_command = strlen(command);

//         char* data_buffer = FX_SCRIPT_ALLOC_MEMORY(char, length_of_command + 1);
//         memcpy(data_buffer, command, length_of_command);

//         Tokenizer tokenizer(data_buffer, length_of_command);
//         tokenizer.Tokenize();

//         for (Tokenizer::Token& token : tokenizer.GetTokens()) {
//             // token.Print();
//             mTokens.Insert(token);
//         }

//         /*for (Tokenizer::Token& token : mTokens) {
//             token.Print();
//         }*/
//     }

//     mTokenIndex = new_token_index;
//     mInCommandMode = true;

//     AstBlock* root_block = FX_SCRIPT_ALLOC_NODE(AstBlock);

//     AstNode* statement;
//     while ((statement = ParseStatementAsCommand())) {
//         root_block->Statements.push_back(statement);
//     }

//     // AstNode* node = ParseStatementAsCommand();

//     // Revert the current state if there have been errors
//     if (root_block == nullptr) {
//         printf("Ignoring state...\n");
//         // Get the size of the tokens
//         // int current_token_index = mTokens.Size();
//         for (int i = new_token_index; i < new_token_index; i++) {
//             mTokens.RemoveLast();
//         }

//         mTokenIndex = old_token_index;

//         mInCommandMode = false;

//         // Since we have reverted state, clear errors flag
//         mHasErrors = false;

//         return false;
//     }

// #if 0

//     // Run the new block
//     {
//         interpreter.mInCommandMode = true;
//         interpreter.Visit(root_block);
//         interpreter.mInCommandMode = false;
//     }
// #endif
//     mInCommandMode = false;

//     return true;
// }

// ScriptValue ConfigScript::ParseValue()
// {
//     Token& token = GetToken();
//     TT token_type = token.Type;
//     ScriptValue value;

//     if (token_type == TT::Identifier) {
//         ScriptVar* var = FindVar(token.GetHash());

//         if (var) {
//             value.Type = ScriptValue::REF;

//             AstVarRef* var_ref = FX_SCRIPT_ALLOC_NODE(AstVarRef);
//             var_ref->Name = var->Name;
//             var_ref->Scope = var->Scope;

//             value.ValueRef = var_ref;

//             EatToken(TT::Identifier);

//             return value;
//         }
//         else {
//             // We cannot find the definition for the variable, assume that it is an
//             // external variable that will be defined during the interpret stage.

//             printf("ERROR: Undefined reference to variable ");
//             token.Print();
//             printf("\n");

//             // printf("Undefined reference to variable \"%.*s\"! (Hash:%u)\n",
//             // token.Length, token.Start, token.GetHash());
//             EatToken(TT::Identifier);
//         }
//     }

//     switch (token_type) {
//     case TT::Integer:
//         EatToken(TT::Integer);
//         value.Type = ScriptValue::INT;
//         value.ValueInt = token.ToInt();
//         break;
//     case TT::Float:
//         EatToken(TT::Float);
//         value.Type = ScriptValue::FLOAT;
//         value.ValueFloat = token.ToFloat();
//         break;
//     case TT::String:
//         EatToken(TT::String);
//         value.Type = ScriptValue::STRING;
//         value.ValueString = token.GetHeapStr();
//         break;
//     default:;
//     }

//     return value;
// }

// #define RETURN_IF_NO_TOKENS(rval_) \
//     { \
//         if (mTokenIndex >= mTokens.Size()) \
//             return (rval_); \
//     }

// static bool IsTokenTypeLiteral(Tokenizer::TokenType type)
// {
//     return (type == TT::Integer || type == TT::Float || type == TT::String);
// }

// AstNode* ConfigScript::ParseRhs()
// {
//     RETURN_IF_NO_TOKENS(nullptr);

//     bool has_parameters = false;

//     if (mTokenIndex + 1 < mTokens.Size()) {
//         TT next_token_type = GetToken(1).Type;
//         has_parameters = next_token_type == TT::LParen;

//         if (mInCommandMode) {
//             has_parameters = next_token_type == TT::Identifier || next_token_type == TT::Integer ||
//                              next_token_type == TT::Float || next_token_type == TT::String;
//         }
//     }

//     Tokenizer::Token& token = GetToken();

//     AstNode* lhs = nullptr;

//     if (token.Type == TT::Identifier) {
//         if (has_parameters) {
//             lhs = ParseActionCall();
//         }
//         else {
//             ScriptExternalFunc* external_action = FindExternalAction(token.GetHash());
//             if (external_action != nullptr) {
//                 lhs = ParseActionCall();
//             }

//             ScriptAction* action = FindAction(token.GetHash());
//             if (action != nullptr) {
//                 lhs = ParseActionCall();
//             }
//         }
//     }
//     if (!lhs) {
//         if (IsTokenTypeLiteral(token.Type) || token.Type == TT::Identifier) {
//             AstLiteral* literal = FX_SCRIPT_ALLOC_NODE(AstLiteral);

//             ScriptValue value = ParseValue();
//             literal->Value = value;

//             lhs = literal;
//         }
//         else {
//             lhs = ParseRhs();
//         }
//     }

//     // AstLiteral* literal = FX_SCRIPT_ALLOC_NODE(AstLiteral);
//     // literal->Value = value;

//     TT op_type = GetToken(0).Type;
//     if (op_type == TT::Plus || op_type == TT::Minus) {
//         AstBinop* binop = FX_SCRIPT_ALLOC_NODE(AstBinop);

//         binop->Left = lhs;
//         binop->OpToken = &EatToken(op_type);
//         binop->Right = ParseRhs();

//         return binop;
//     }

//     return lhs;

//     /*
//     RETURN_IF_NO_TOKENS(nullptr);

//     bool has_parameters = false;

//     if (mTokenIndex + 1 < mTokens.Size()) {
//         TT next_token_type = GetToken(1).Type;
//         has_parameters = next_token_type == TT::LParen;

//         if (mInCommandMode) {
//             has_parameters = next_token_type == TT::Identifier || next_token_type
//     == TT::Integer || next_token_type == TT::Float || next_token_type ==
//     TT::String;
//         }
//     }

//     Tokenizer::Token& token = GetToken();

//     AstNode* lhs = nullptr;

//     if (token.Type == TT::Identifier) {
//         if (has_parameters) {
//             lhs = ParseActionCall();
//         }
//         else {
//             ScriptExternalFunc* external_action =
//     FindExternalAction(token.GetHash()); if (external_action != nullptr) { return
//     ParseActionCall();
//             }

//             ScriptAction* action = FindAction(token.GetHash());
//             if (action != nullptr) {
//                 return ParseActionCall();
//             }

//         }
//     }

//     else if (IsTokenTypeLiteral(token.Type) || token.Type == TT::Identifier) {
//         AstLiteral* literal = FX_SCRIPT_ALLOC_NODE(AstLiteral);
//         literal->Value = ParseValue();
//         lhs = literal;
//     }

//     TT op_type = GetToken(0).Type;
//     if (op_type == TT::Plus || op_type == TT::Minus) {
//         AstBinop* binop = FX_SCRIPT_ALLOC_NODE(AstBinop);

//         binop->Left = lhs;
//         binop->OpToken = &EatToken(op_type);
//         binop->Right = ParseRhs();

//         return binop;
//     }

//     return lhs;
//     */
// }

// AstAssign* ConfigScript::TryParseAssignment(Tokenizer::Token* var_name)
// {
//     if (GetToken().Type != TT::Equals) {
//         return nullptr;
//     }

//     EatToken(TT::Equals);

//     AstAssign* node = FX_SCRIPT_ALLOC_NODE(AstAssign);

//     AstVarRef* var_ref = FX_SCRIPT_ALLOC_NODE(AstVarRef);
//     var_ref->Name = var_name;
//     var_ref->Scope = mCurrentScope;
//     node->Var = var_ref;

//     // node->Value = ParseValue();
//     node->Rhs = ParseRhs();

//     return node;
// }

// void ConfigScript::DefineExternalVar(const char* type, const char* name, const ScriptValue& value)
// {
//     Token* name_token = FX_SCRIPT_ALLOC_MEMORY(Token, sizeof(Token));
//     Token* type_token = FX_SCRIPT_ALLOC_MEMORY(Token, sizeof(Token));

//     {
//         const uint32 type_len = strlen(type);

//         char* type_buffer = FX_SCRIPT_ALLOC_MEMORY(char, (type_len + 1));
//         strcpy(type_buffer, type);

//         type_token->Start = type_buffer;
//         type_token->Type = TT::Identifier;
//         type_token->Start[type_len] = 0;
//         type_token->Length = type_len + 1;
//     }

//     {
//         const uint32 name_len = strlen(name);

//         char* name_buffer = FX_SCRIPT_ALLOC_MEMORY(char, (name_len + 1));
//         strcpy(name_buffer, name);

//         name_token->Start = name_buffer;
//         name_token->Type = TT::Identifier;
//         name_token->Start[name_len] = 0;
//         name_token->Length = name_len + 1;
//     }

//     ScriptScope* definition_scope = &mScopes[0];

//     ScriptVar var(type_token, name_token, definition_scope, true);
//     var.Value = value;

//     definition_scope->Vars.Insert(var);

//     // To prevent the variable data from being deleted here.
//     var.Name = nullptr;
//     var.Type = nullptr;
// }

// AstNode* ConfigScript::ParseStatementAsCommand()
// {
//     if (mHasErrors) {
//         return nullptr;
//     }

//     RETURN_IF_NO_TOKENS(nullptr);

//     // Eat any extraneous semicolons
//     while (GetToken().Type == TT::Semicolon) {
//         EatToken(TT::Semicolon);
//         if (mTokenIndex >= mTokens.Size()) {
//             return nullptr;
//         }
//     }

//     // Try to parse as a keyword first
//     AstNode* node = TryParseKeyword();

//     if (node) {
//         RETURN_IF_NO_TOKENS(node);

//         EatToken(TT::Semicolon);
//         return node;
//     }

//     // Check identifier
//     if (mTokenIndex < mTokens.Size() && GetToken().Type == TT::Identifier) {
//         TT next_token_type = TT::Unknown;

//         if (mTokenIndex + 1 < mTokens.Size()) {
//             next_token_type = GetToken(1).Type;
//         }

//         if (mTokenIndex + 1 < mTokens.Size() && GetToken(1).Type == TT::Equals) {
//             Token& assign_name = EatToken(TT::Identifier);
//             node = TryParseAssignment(&assign_name);
//         }
//         // If there is what looks to be a function call, try it
//         else {
//             ScriptExternalFunc* external_action = FindExternalAction(GetToken().GetHash());
//             if (external_action != nullptr) {
//                 node = ParseActionCall();
//             }
//             else {
//                 ScriptAction* action = FindAction(GetToken().GetHash());
//                 if (action != nullptr) {
//                     node = ParseActionCall();
//                 }
//             }
//         }
//         // If there is just a semicolon after an identifier, parse as an rhs to
//         // print out later
//         if (!node && next_token_type == TT::Semicolon) {
//             node = ParseRhs();
//         }
//     }

//     RETURN_IF_NO_TOKENS(node);

//     EatToken(TT::Semicolon);

//     return node;
// }

// AstNode* ConfigScript::ParseStatement()
// {
//     if (mHasErrors) {
//         return nullptr;
//     }

//     RETURN_IF_NO_TOKENS(nullptr);

//     if (GetToken().Type == TT::Dollar) {
//         EatToken(TT::Dollar);

//         mInCommandMode = true;

//         AstCommandMode* cmd_mode = FX_SCRIPT_ALLOC_NODE(AstCommandMode);
//         cmd_mode->Node = ParseStatementAsCommand();

//         mInCommandMode = false;

//         return cmd_mode;
//     }

//     while (GetToken().Type == TT::DocComment) {
//         AstDocComment* comment = FX_SCRIPT_ALLOC_NODE(AstDocComment);
//         comment->Comment = &EatToken(TT::DocComment);
//         CurrentDocComments.push_back(comment);

//         if (mTokenIndex >= mTokens.Size()) {
//             return nullptr;
//         }
//     }

//     // Eat any extraneous semicolons
//     while (GetToken().Type == TT::Semicolon) {
//         EatToken(TT::Semicolon);

//         if (mTokenIndex >= mTokens.Size()) {
//             return nullptr;
//         }
//     }

//     AstNode* node = TryParseKeyword();

//     if (!node && (mTokenIndex < mTokens.Size() && GetToken().Type == TT::Identifier)) {
//         if (mTokenIndex + 1 < mTokens.Size() && GetToken(1).Type == TT::LParen) {
//             node = ParseActionCall();
//         }
//         else if (mTokenIndex + 1 < mTokens.Size() && GetToken(1).Type == TT::Equals) {
//             Token& assign_name = EatToken(TT::Identifier);
//             node = TryParseAssignment(&assign_name);
//         }
//         else {
//             GetToken().Print();
//         }
//     }

//     if (!node) {
//         return nullptr;
//     }

//     // Blocks do not require semicolons
//     if (node->NodeType == FX_AST_BLOCK || node->NodeType == FX_AST_ACTIONDECL) {
//         return node;
//     }

//     EatToken(TT::Semicolon);

//     return node;
// }

// AstBlock* ConfigScript::ParseBlock()
// {
//     bool in_command = mInCommandMode;
//     mInCommandMode = false;

//     AstBlock* block = FX_SCRIPT_ALLOC_NODE(AstBlock);

//     EatToken(TT::LBrace);

//     while (GetToken().Type != TT::RBrace) {
//         AstNode* command = ParseStatement();
//         if (command == nullptr) {
//             break;
//         }
//         block->Statements.push_back(command);
//     }

//     EatToken(TT::RBrace);

//     mInCommandMode = in_command;
//     return block;
// }

// AstActionDecl* ConfigScript::ParseActionDeclare()
// {
//     AstActionDecl* node = FX_SCRIPT_ALLOC_NODE(AstActionDecl);

//     if (!CurrentDocComments.empty()) {
//         node->DocComments = CurrentDocComments;
//         CurrentDocComments.clear();
//     }

//     // Name of the action
//     Token& name = EatToken(TT::Identifier);

//     node->Name = &name;

//     PushScope();
//     EatToken(TT::LParen);

//     AstBlock* params = FX_SCRIPT_ALLOC_NODE(AstBlock);

//     while (GetToken().Type != TT::RParen) {
//         params->Statements.push_back(ParseVarDeclare());

//         if (GetToken().Type == TT::Comma) {
//             EatToken(TT::Comma);
//             continue;
//         }

//         break;
//     }

//     EatToken(TT::RParen);

//     if (GetToken().Type != TT::LBrace) {
//         AstVarDecl* return_decl = ParseVarDeclare();
//         node->ReturnVar = return_decl;
//     }

//     node->Block = ParseBlock();
//     PopScope();

//     node->Params = params;

//     ScriptAction action(&name, mCurrentScope, node->Block, node);
//     mCurrentScope->Actions.Insert(action);

//     return node;
// }

// // ScriptValue ConfigScript::TryCallInternalFunc(Hash32 func_name,
// // std::vector<ScriptValue>& params)
// //{
// //     ScriptValue return_value;
// //
// //     for (const ScriptInternalFunc& func : mInternalFuncs) {
// //         if (func.HashedName == func_name) {
// //             func.Func(params, &return_value);
// //             return return_value;
// //         }
// //     }
// //
// //     return return_value;
// // }

// AstActionCall* ConfigScript::ParseActionCall()
// {
//     AstActionCall* node = FX_SCRIPT_ALLOC_NODE(AstActionCall);

//     Token& name = EatToken(TT::Identifier);

//     node->HashedName = name.GetHash();
//     node->Action = FindAction(node->HashedName);

//     TT end_token_type = TT::Semicolon;

//     if (!mInCommandMode) {
//         end_token_type = TT::RParen;
//     }

//     if (!mInCommandMode || GetToken().Type == TT::LParen) {
//         EatToken(TT::LParen);
//     }

//     while (GetToken().Type != end_token_type) {
//         AstNode* param = ParseRhs();

//         if (param == nullptr) {
//             break;
//         }

//         node->Params.push_back(param);

//         TT next_tt = GetToken().Type;

//         if (GetToken().Type == TT::Comma) {
//             EatToken(TT::Comma);
//             continue;
//         }

//         if (mInCommandMode && (next_tt != TT::RParen && next_tt != TT::Semicolon)) {
//             continue;
//         }

//         break;
//     }

//     if (!mInCommandMode || GetToken().Type == TT::RParen) {
//         EatToken(TT::RParen);
//     }

//     return node;
// }

// // void ConfigScript::ParseDoCall()
// //{
// //     Token& call_name = EatToken(TT::Identifier);
// //
// //     EatToken(TT::LParen);
// //
// //     std::vector<ScriptValue> params;
// //
// //     while (true) {
// //         params.push_back(ParseValue());
// //
// //         if (GetToken().Type == TT::Comma) {
// //             EatToken(TT::Comma);
// //             continue;
// //         }
// //
// //         break;
// //     }
// //
// //     EatToken(TT::RParen);
// //
// //     TryCallInternalFunc(call_name.GetHash(), params);
// //
// //     printf("Calling ");
// //     call_name.Print();
// //
// //     for (const auto& param : params) {
// //         printf("param : ");
// //         param.Print();
// //     }
// // }

// AstBlock* ConfigScript::Parse()
// {
//     AstBlock* root_block = FX_SCRIPT_ALLOC_NODE(AstBlock);

//     AstNode* keyword;
//     while ((keyword = ParseStatement())) {
//         root_block->Statements.push_back(keyword);
//     }

//     /*for (const auto& var : mCurrentScope->Vars) {
//         var.Print();
//     }*/

//     if (mHasErrors) {
//         return nullptr;
//     }

//     AstPrinter printer(root_block);
//     printer.Print(root_block);

//     return root_block;
// }

// //////////////////////////////////////////
// // Script Interpreter
// //////////////////////////////////////////
// // #if 0
// void ScriptInterpreter::Create(AstBlock* root_block)
// {
//     mRootBlock = root_block;

//     mScopes.Create(8);
//     mCurrentScope = mScopes.Insert();
//     mCurrentScope->Parent = nullptr;
//     mCurrentScope->Vars.Create(FX_SCRIPT_SCOPE_LOCAL_VARS_START_SIZE);
//     mCurrentScope->Actions.Create(FX_SCRIPT_SCOPE_LOCAL_ACTIONS_START_SIZE);
// }

// void ScriptInterpreter::Interpret()
// {
//     Visit(mRootBlock);

//     mScopes[0].PrintAllVarsInScope();
// }

// void ScriptInterpreter::PushScope()
// {
//     ScriptScope* current = mCurrentScope;

//     ScriptScope* new_scope = mScopes.Insert();
//     new_scope->Parent = current;
//     new_scope->Vars.Create(FX_SCRIPT_SCOPE_LOCAL_VARS_START_SIZE);
//     new_scope->Actions.Create(FX_SCRIPT_SCOPE_LOCAL_ACTIONS_START_SIZE);

//     mCurrentScope = new_scope;
// }

// void ScriptInterpreter::PopScope()
// {
//     ScriptScope* new_scope = mCurrentScope->Parent;
//     mScopes.RemoveLast();

//     assert(new_scope == &mScopes.GetLast());

//     mCurrentScope = new_scope;
// }

// ScriptVar* ScriptInterpreter::FindVar(Hash32 hashed_name)
// {
//     ScriptScope* scope = mCurrentScope;

//     while (scope) {
//         ScriptVar* var = scope->FindVarInScope(hashed_name);
//         if (var) {
//             return var;
//         }

//         scope = scope->Parent;
//     }

//     return nullptr;
// }

// ScriptAction* ScriptInterpreter::FindAction(Hash32 hashed_name)
// {
//     ScriptScope* scope = mCurrentScope;

//     while (scope) {
//         ScriptAction* var = scope->FindActionInScope(hashed_name);
//         if (var) {
//             return var;
//         }

//         scope = scope->Parent;
//     }

//     return nullptr;
// }

// ScriptExternalFunc* ScriptInterpreter::FindExternalAction(Hash32 hashed_name)
// {
//     for (ScriptExternalFunc& func : mExternalFuncs) {
//         if (func.HashedName == hashed_name) {
//             return &func;
//         }
//     }
//     return nullptr;
// }

// bool ScriptInterpreter::CheckExternalCallArgs(AstActionCall* call, ScriptExternalFunc& func)
// {
//     if (func.IsVariadic) {
//         return true;
//     }

//     if (call->Params.size() != func.ParameterTypes.size()) {
//         return false;
//     }

//     for (int i = 0; i < call->Params.size(); i++) {
//         ScriptValue val = VisitRhs(call->Params[i]);

//         if (!(val.Type & func.ParameterTypes[i])) {
//             return false;
//         }
//     }

//     return true;
// }

// ScriptValue ScriptInterpreter::VisitExternalCall(AstActionCall* call, ScriptExternalFunc& func)
// {
//     ScriptValue return_value;

//     // PushScope();

//     if (!CheckExternalCallArgs(call, func)) {
//         printf("!!! Parameters do not match for function call!\n");
//         PopScope();
//         return return_value;
//     }

//     std::vector<ScriptValue> params;
//     params.reserve(call->Params.size());

//     for (AstNode* param_node : call->Params) {
//         params.push_back(VisitRhs(param_node));
//     }

//     // func.Function(*this, params, &return_value);

//     // PopScope();

//     return return_value;
// }

// ScriptValue ScriptInterpreter::VisitActionCall(AstActionCall* call)
// {
//     ScriptValue return_value;

//     // This is not a local call, check for an internal function
//     if (call->Action == nullptr) {
//         for (ScriptExternalFunc& func : mExternalFuncs) {
//             if (func.HashedName != call->HashedName) {
//                 continue;
//             }

//             return VisitExternalCall(call, func);
//         }
//     }

//     if (call->Action == nullptr) {
//         puts("!!! Could not find action!");
//         return return_value;
//     }

//     PushScope();

//     std::vector<AstNode*> param_decls = call->Action->Declaration->Params->Statements;

//     if (call->Params.size() != param_decls.size()) {
//         printf("!!! MISMATCHED PARAM COUNTS\n");
//         PopScope();

//         return return_value;
//     }

//     // Assign each passed in value to the each parameter declaration
//     for (int i = 0; i < param_decls.size(); i++) {
//         AstVarDecl* decl = reinterpret_cast<AstVarDecl*>(param_decls[i]);

//         ScriptVar param(decl->Type, decl->Name, mCurrentScope);
//         param.Value = GetImmediateValue(VisitRhs(call->Params[i]));

//         mCurrentScope->Vars.Insert(param);
//     }

//     if (call->Action->Declaration->ReturnVar) {
//         AstVarDecl* decl = call->Action->Declaration->ReturnVar;

//         ScriptVar return_var(decl->Type, decl->Name, mCurrentScope);
//         mCurrentScope->Vars.Insert(return_var);

//         mCurrentScope->ReturnVar = &mCurrentScope->Vars.GetLast();
//     }

//     Visit(call->Action->Block);

//     // Acquire the return value from the scope
//     if (mCurrentScope->ReturnVar) {
//         return_value = GetImmediateValue(mCurrentScope->ReturnVar->Value);
//     }

//     // mCurrentScope->PrintAllVarsInScope();

//     PopScope();

//     return return_value;
// }

// ScriptValue ScriptInterpreter::VisitRhs(AstNode* node)
// {
//     if (node->NodeType == FX_AST_LITERAL) {
//         AstLiteral* literal = reinterpret_cast<AstLiteral*>(node);
//         return literal->Value;
//     }
//     else if (node->NodeType == FX_AST_ACTIONCALL) {
//         AstActionCall* call = reinterpret_cast<AstActionCall*>(node);
//         return VisitActionCall(call);
//     }
//     else if (node->NodeType == FX_AST_BINOP) {
//         AstBinop* binop = reinterpret_cast<AstBinop*>(node);

//         ScriptValue lhs_pre_val = VisitRhs(binop->Left);
//         ScriptValue rhs_pre_val = VisitRhs(binop->Right);

//         ScriptValue lhs = GetImmediateValue(lhs_pre_val);
//         ScriptValue rhs = GetImmediateValue(rhs_pre_val);

//         float sign = (binop->OpToken->Type == TT::Plus) ? 1.0f : -1.0f;

//         ScriptValue result;

//         if (lhs.Type == ScriptValue::INT) {
//             result.ValueInt = lhs.ValueInt;
//             result.Type = ScriptValue::INT;

//             if (rhs.Type == ScriptValue::INT) {
//                 result.ValueInt += rhs.ValueInt * sign;
//             }
//             else if (rhs.Type == ScriptValue::FLOAT) {
//                 result.ValueInt += rhs.ValueFloat * sign;
//             }
//         }
//         else if (lhs.Type == ScriptValue::FLOAT) {
//             result.ValueFloat = lhs.ValueFloat;
//             result.Type = ScriptValue::FLOAT;

//             if (rhs.Type == ScriptValue::INT) {
//                 result.ValueFloat += rhs.ValueInt * sign;
//             }
//             else if (rhs.Type == ScriptValue::FLOAT) {
//                 result.ValueFloat += rhs.ValueFloat * sign;
//             }
//         }

//         return result;
//     }

//     ScriptValue value {};
//     return value;
// }

// void ConfigScript::DefineDefaultExternalFunctions()
// {
//     // log([int | float | string | ref] args...)
//     RegisterExternalFunc(
//         HashStr32("log"), {}, // Do not check argument types as we handle it here
//         [](ScriptVM* vm, std::vector<ScriptValue>& args, ScriptValue* return_value)
//         {
//             printf("[SCRIPT]: ");

//             for (int i = args.size() - 1; i >= 0; i--) {
//                 ScriptValue& arg = args[i];

//                 // const ScriptValue& value = interpreter.GetImmediateValue(arg);
//                 const ScriptValue& value = arg;

//                 switch (value.Type) {
//                 case ScriptValue::NONETYPE:
//                     printf("[none]");
//                     break;
//                 case ScriptValue::INT:
//                     printf("%d", value.ValueInt);
//                     break;
//                 case ScriptValue::FLOAT:
//                     printf("%f", value.ValueFloat);
//                     break;
//                 case ScriptValue::STRING:
//                     printf("%s", value.ValueString);
//                     break;
//                 default:
//                     printf("Unknown type\n");
//                     break;
//                 }

//                 putchar(' ');
//             }

//             putchar('\n');
//         },
//         true // Is variadic?
//     );

//     // listvars()
//     // RegisterExternalFunc(
//     //    HashStr32("__listvars__"),
//     //    {},
//     //    [](ScriptVM* vm, std::vector<ScriptValue>& args, ScriptValue*
//     //    return_value)
//     //    {
//     //        ScriptScope* scope = interpreter.mCurrentScope;
//     //        // Since there is a new scope created on function call, we need to
//     //        start from the parent scope if (scope && scope->Parent) {
//     //            scope = scope->Parent;
//     //        }

//     //        while (scope != nullptr) {
//     //            scope->PrintAllVarsInScope();
//     //            scope = scope->Parent;
//     //        }
//     //    },
//     //    false
//     //);

//     // listactions()
//     // RegisterExternalFunc(
//     //    HashStr32("__listactions__"),
//     //    {},
//     //    [](ScriptVM* vm, std::vector<ScriptValue>& args, ScriptValue*
//     //    return_value)
//     //    {
//     //        ScriptScope* scope = interpreter.mCurrentScope;
//     //        // Since there is a new scope created on function call, we need to
//     //        start from the parent scope if (scope && scope->Parent) {
//     //            scope = scope->Parent;
//     //        }

//     //        while (scope != nullptr) {

//     //            for (const ScriptAction& action : scope->Actions) {
//     //                // Print out the action name
//     //                printf("action %.*s", action.Declaration->Name->Length,
//     //                action.Declaration->Name->Start);

//     //                // Retrieve the declarations for all parameters
//     //                std::vector<AstNode*>& param_decls =
//     //                action.Declaration->Params->Statements; const size_t
//     //                param_count = param_decls.size();

//     //                // Print out the parameter list
//     //                putchar('(');

//     //                for (int i = 0; i < param_count; i++) {
//     //                    AstNode* param_node = param_decls[i];
//     //                    if (param_node->NodeType != FX_AST_VARDECL) {
//     //                        continue;
//     //                    }

//     //                    AstVarDecl* param_decl =
//     //                    reinterpret_cast<AstVarDecl*>(param_node);

//     //                    // Print the type of the parameter
//     //                    printf("%.*s ", param_decl->Type->Length,
//     //                    param_decl->Type->Start);

//     //                    // Print the name of the parameter
//     //                    printf("%.*s", param_decl->Name->Length,
//     //                    param_decl->Name->Start);

//     //                    // If there are more parameters following, output a
//     //                    comma if (i < param_count - 1) {
//     //                        printf(", ");
//     //                    }
//     //                }

//     //                putchar(')');

//     //                // Print out the return type at the end of the declaration
//     //                if it exists if (action.Declaration->ReturnVar) {
//     //                    printf(" %.*s",
//     //                    action.Declaration->ReturnVar->Type->Length,
//     //                    action.Declaration->ReturnVar->Type->Start);
//     //                }

//     //                putchar('\n');
//     //            }

//     //            scope = scope->Parent;
//     //        }
//     //    },
//     //    false
//     //);
// }

// void ScriptInterpreter::VisitAssignment(AstAssign* assign)
// {
//     ScriptVar* var = FindVar(assign->Var->Name->GetHash());

//     if (!var) {
//         printf("!!! Could not find variable!\n");
//         return;
//     }

//     constexpr Hash32 builtin_int = HashStr32("int");
//     constexpr Hash32 builtin_playerid = HashStr32("playerid");
//     constexpr Hash32 builtin_float = HashStr32("float");
//     constexpr Hash32 builtin_string = HashStr32("string");

//     ScriptValue rhs_value = VisitRhs(assign->Rhs);
//     const ScriptValue& new_value = GetImmediateValue(rhs_value);

//     ScriptValue::ValueType var_type = ScriptValue::NONETYPE;

//     Hash32 type_hash = var->Type->GetHash();

//     switch (type_hash) {
//     case builtin_playerid:
//         [[fallthrough]];
//     case builtin_int:
//         var_type = ScriptValue::INT;
//         break;
//     case builtin_float:
//         var_type = ScriptValue::FLOAT;
//         break;
//     case builtin_string:
//         var_type = ScriptValue::STRING;
//         break;
//     default:
//         printf("!!! Unknown type for variable %.*s!\n", var->Type->Length, var->Type->Start);
//         return;
//     }

//     if (var_type != new_value.Type) {
//         printf("!!! Assignment value type does not match variable type!\n");
//         return;
//     }

//     var->Value = new_value;

//     // puts("Visit Assign");
// }

// void ScriptInterpreter::Visit(AstNode* node)
// {
//     if (node == nullptr) {
//         return;
//     }

//     if (node->NodeType == FX_AST_BLOCK) {
//         // puts("Visit Block");

//         AstBlock* block = reinterpret_cast<AstBlock*>(node);
//         for (AstNode* child : block->Statements) {
//             if (child->NodeType == FX_AST_RETURN) {
//                 break;
//             }

//             Visit(child);
//         }
//     }
//     else if (node->NodeType == FX_AST_ACTIONDECL) {
//         AstActionDecl* actiondecl = reinterpret_cast<AstActionDecl*>(node);
//         // puts("Visit ActionDecl");

//         ScriptAction action(actiondecl->Name, mCurrentScope, actiondecl->Block, actiondecl);
//         mCurrentScope->Actions.Insert(action);

//         // Visit(actiondecl->Block);
//     }
//     else if (node->NodeType == FX_AST_VARDECL) {
//         AstVarDecl* vardecl = reinterpret_cast<AstVarDecl*>(node);
//         // puts("Visit VarDecl");

//         ScriptScope* scope = mCurrentScope;
//         if (vardecl->DefineAsGlobal) {
//             scope = &mScopes[0];
//         }

//         ScriptVar var(vardecl->Type, vardecl->Name, scope);
//         scope->Vars.Insert(var);

//         Visit(vardecl->Assignment);
//     }
//     else if (node->NodeType == FX_AST_ASSIGN) {
//         AstAssign* assign = reinterpret_cast<AstAssign*>(node);
//         VisitAssignment(assign);
//     }
//     else if (node->NodeType == FX_AST_ACTIONCALL) {
//         AstActionCall* actioncall = reinterpret_cast<AstActionCall*>(node);
//         ScriptValue return_value = VisitActionCall(actioncall);

//         // If we are in command mode, print the result to the user
//         if (mInCommandMode && return_value.Type != ScriptValue::NONETYPE) {
//             return_value.Print();
//         }
//     }
//     // If we are in command mode, print the variable to the user
//     else if (node->NodeType == FX_AST_VARREF && mInCommandMode) {
//         AstVarRef* ref = reinterpret_cast<AstVarRef*>(node);
//         ScriptVar* var = FindVar(ref->Name->GetHash());

//         if (var) {
//             var->Print();
//         }
//     }
//     // If we are in command mode, print the literal(probably result) to the user
//     else if (node->NodeType == FX_AST_LITERAL && mInCommandMode) {
//         AstLiteral* literal = reinterpret_cast<AstLiteral*>(node);
//         GetImmediateValue(literal->Value).Print();
//     }
//     // Executes a statement in command mode
//     else if (node->NodeType == FX_AST_COMMANDMODE) {
//         mInCommandMode = true;
//         Visit(reinterpret_cast<AstCommandMode*>(node)->Node);
//         mInCommandMode = false;
//     }
//     else {
//         puts("[UNKNOWN]");
//     }
// }

// void ScriptInterpreter::DefineExternalVar(const char* type, const char* name, const ScriptValue& value)
// {
//     Token* name_token = FX_SCRIPT_ALLOC_MEMORY(Token, sizeof(Token));
//     Token* type_token = FX_SCRIPT_ALLOC_MEMORY(Token, sizeof(Token));

//     {
//         const uint32 type_len = strlen(type);
//         char* type_buffer = FX_SCRIPT_ALLOC_MEMORY(char, (type_len + 1));
//         strcpy(type_buffer, type);
//         // type_buffer[type_len + 1] = 0;

//         type_token->Start = type_buffer;
//         type_token->Type = TT::Identifier;
//         type_token->Start[type_len] = 0;
//         type_token->Length = type_len + 1;
//     }

//     {
//         const uint32 name_len = strlen(name);

//         char* name_buffer = FX_SCRIPT_ALLOC_MEMORY(char, (name_len + 1));
//         strcpy(name_buffer, name);

//         name_token->Start = name_buffer;
//         name_token->Type = TT::Identifier;
//         name_token->Start[name_len] = 0;
//         name_token->Length = name_len + 1;
//     }

//     ScriptScope* definition_scope = &mScopes[0];

//     ScriptVar var(type_token, name_token, definition_scope, true);
//     var.Value = value;

//     definition_scope->Vars.Insert(var);
// }

// const ScriptValue& ScriptInterpreter::GetImmediateValue(const ScriptValue& value)
// {
//     // If the value is a reference, get the value of that reference
//     if (value.Type == ScriptValue::REF) {
//         ScriptVar* var = FindVar(value.ValueRef->Name->GetHash());

//         if (!var) {
//             printf("!!! Undefined reference to variable\n");
//             // value.ValueRef->Name->Print();
//             putchar('\n');

//             return ScriptValue::None;
//         }

//         return var->Value;
//     }

//     return value;
// }

// // #endif

// void ConfigScript::RegisterExternalFunc(Hash32 func_name, std::vector<ScriptValue::ValueType> param_types,
//                                           ScriptExternalFunc::FuncType callback, bool is_variadic)
// {
//     ScriptExternalFunc func {
//         .HashedName = func_name,
//         .Function = callback,
//         .ParameterTypes = param_types,
//         .IsVariadic = is_variadic,
//     };

//     mExternalFuncs.push_back(func);
// }

// /////////////////////////////////////////
// // Script Bytecode Emitter
// /////////////////////////////////////////

// #include "ScriptBytecode.hpp"

// void ScriptBCEmitter::BeginEmitting(AstNode* node)
// {
//     mStackSize = 1024;

//     /*mStack = new uint8[1024];
//     mStackStart = mStack;*/

//     mBytecode.Create(4096);
//     VarHandles.Create(64);

//     Emit(node);

//     printf("\n");

//     PrintBytecode();
// }

// #define RETURN_IF_NO_NODE(node_) \
//     if ((node_) == nullptr) { \
//         return; \
//     }

// #define RETURN_VALUE_IF_NO_NODE(node_, value_) \
//     if ((node_) == nullptr) { \
//         return (value_); \
//     }

// void ScriptBCEmitter::Emit(AstNode* node)
// {
//     RETURN_IF_NO_NODE(node);

//     if (node->NodeType == FX_AST_BLOCK) {
//         return EmitBlock(reinterpret_cast<AstBlock*>(node));
//     }
//     else if (node->NodeType == FX_AST_ACTIONDECL) {
//         return EmitAction(reinterpret_cast<AstActionDecl*>(node));
//     }
//     else if (node->NodeType == FX_AST_ACTIONCALL) {
//         return DoActionCall(reinterpret_cast<AstActionCall*>(node));
//     }
//     else if (node->NodeType == FX_AST_ASSIGN) {
//         return EmitAssign(reinterpret_cast<AstAssign*>(node));
//     }
//     else if (node->NodeType == FX_AST_VARDECL) {
//         DoVarDeclare(reinterpret_cast<AstVarDecl*>(node));
//         return;
//     }
// }

// ScriptBytecodeVarHandle* ScriptBCEmitter::FindVarHandle(Hash32 hashed_name)
// {
//     for (ScriptBytecodeVarHandle& handle : VarHandles) {
//         if (handle.HashedName == hashed_name) {
//             return &handle;
//         }
//     }
//     return nullptr;
// }

// ScriptBytecodeActionHandle* ScriptBCEmitter::FindActionHandle(Hash32 hashed_name)
// {
//     for (ScriptBytecodeActionHandle& handle : ActionHandles) {
//         if (handle.HashedName == hashed_name) {
//             return &handle;
//         }
//     }
//     return nullptr;
// }

// ScriptRegister ScriptBCEmitter::FindFreeRegister()
// {
//     uint16 gp_r = 0x01;

//     const uint16 num_gp_regs = FX_REG_X3;

//     for (int i = 0; i < num_gp_regs; i++) {
//         if (!(mRegsInUse & gp_r)) {
//             // We are starting on 0x01, so register index should be N + 1
//             const int register_index = i + 1;

//             return static_cast<ScriptRegister>(register_index);
//         }

//         gp_r <<= 1;
//     }

//     return ScriptRegister::FX_REG_NONE;
// }

// const char* ScriptBCEmitter::GetRegisterName(ScriptRegister reg)
// {
//     switch (reg) {
//     case FX_REG_NONE:
//         return "NONE";
//     case FX_REG_X0:
//         return "X0";
//     case FX_REG_X1:
//         return "X1";
//     case FX_REG_X2:
//         return "X2";
//     case FX_REG_X3:
//         return "X3";
//     case FX_REG_RA:
//         return "RA";
//     case FX_REG_XR:
//         return "XR";
//     case FX_REG_SP:
//         return "SP";
//     default:;
//     };

//     return "NONE";
// }

// ScriptRegister ScriptBCEmitter::RegFlagToReg(ScriptRegisterFlag reg_flag)
// {
//     switch (reg_flag) {
//     case FX_REGFLAG_NONE:
//         return FX_REG_NONE;
//     case FX_REGFLAG_X0:
//         return FX_REG_X0;
//     case FX_REGFLAG_X1:
//         return FX_REG_X1;
//     case FX_REGFLAG_X2:
//         return FX_REG_X2;
//     case FX_REGFLAG_X3:
//         return FX_REG_X3;
//     case FX_REGFLAG_RA:
//         return FX_REG_RA;
//     case FX_REGFLAG_XR:
//         return FX_REG_XR;
//     }

//     return FX_REG_NONE;
// }

// ScriptRegisterFlag ScriptBCEmitter::RegToRegFlag(ScriptRegister reg)
// {
//     switch (reg) {
//     case FX_REG_NONE:
//         return FX_REGFLAG_NONE;
//     case FX_REG_X0:
//         return FX_REGFLAG_X0;
//     case FX_REG_X1:
//         return FX_REGFLAG_X1;
//     case FX_REG_X2:
//         return FX_REGFLAG_X2;
//     case FX_REG_X3:
//         return FX_REGFLAG_X3;
//     case FX_REG_RA:
//         return FX_REGFLAG_RA;
//     case FX_REG_XR:
//         return FX_REGFLAG_XR;
//     case FX_REG_SP:
//         return FX_REGFLAG_NONE;
//     default:;
//     }

//     return FX_REGFLAG_NONE;
// }

// void ScriptBCEmitter::Write16(uint16 value)
// {
//     mBytecode.Insert(static_cast<uint8>(value >> 8));
//     mBytecode.Insert(static_cast<uint8>(value));
// }

// void ScriptBCEmitter::Write32(uint32 value)
// {
//     Write16(static_cast<uint16>(value >> 16));
//     Write16(static_cast<uint16>(value));
// }

// void ScriptBCEmitter::WriteOp(uint8 op_base, uint8 op_spec)
// {
//     mBytecode.Insert(op_base);
//     mBytecode.Insert(op_spec);
// }

// using RhsMode = ScriptBCEmitter::RhsMode;

// #define MARK_REGISTER_USED(regn_) \
//     { \
//         MarkRegisterUsed(regn_); \
//     }
// #define MARK_REGISTER_FREE(regn_) \
//     { \
//         MarkRegisterFree(regn_); \
//     }

// void ScriptBCEmitter::MarkRegisterUsed(ScriptRegister reg)
// {
//     ScriptRegisterFlag rflag = RegToRegFlag(reg);
//     mRegsInUse = static_cast<ScriptRegisterFlag>(uint16(mRegsInUse) | uint16(rflag));
// }

// void ScriptBCEmitter::MarkRegisterFree(ScriptRegister reg)
// {
//     ScriptRegisterFlag rflag = RegToRegFlag(reg);

//     mRegsInUse = static_cast<ScriptRegisterFlag>(uint16(mRegsInUse) & (~uint16(rflag)));
// }

// void ScriptBCEmitter::EmitSave32(int16 offset, uint32 value)
// {
//     // SAVE32 [i16 offset] [i32]

//     printf("save32 %d, %u\n", static_cast<int16>(offset), value);

//     WriteOp(OpBase_Save, OpSpecSave_Int32);

//     Write16(offset);
//     Write32(value);
// }

// void ScriptBCEmitter::EmitSaveReg32(int16 offset, ScriptRegister reg)
// {
//     // SAVE32r [i16 offset] [%r32]

//     printf("save32r %d, %s\n", static_cast<int16>(offset), GetRegisterName(reg));

//     WriteOp(OpBase_Save, OpSpecSave_Reg32);

//     Write16(offset);
//     Write16(reg);
// }

// void ScriptBCEmitter::EmitSaveAbsolute32(uint32 position, uint32 value)
// {
//     // SAVE32a [i32 offset] [i32]

//     printf("save32a %u, %u\n", position, value);

//     WriteOp(OpBase_Save, OpSpecSave_AbsoluteInt32);

//     Write32(position);
//     Write32(value);
// }

// void ScriptBCEmitter::EmitSaveAbsoluteReg32(uint32 position, ScriptRegister reg)
// {
//     // SAVE32r [i32 offset] [%r32]

//     printf("save32ar %u, %s\n", position, GetRegisterName(reg));

//     WriteOp(OpBase_Save, OpSpecSave_AbsoluteReg32);

//     Write32(position);
//     Write16(reg);
// }

// void ScriptBCEmitter::EmitPush32(uint32 value)
// {
//     // PUSH32 [i32]

//     printf("push32 %d \t# --- offset %lld\n", value, mStackOffset);

//     WriteOp(OpBase_Push, OpSpecPush_Int32);
//     Write32(value);

//     mStackOffset += 4;
// }

// void ScriptBCEmitter::EmitPush32r(ScriptRegister reg)
// {
//     // PUSH32r [%r32]
//     printf("push32r %s \t# --- offset %lld\n", GetRegisterName(reg), mStackOffset);

//     WriteOp(OpBase_Push, OpSpecPush_Reg32);
//     Write16(reg);

//     mStackOffset += 4;
// }

// void ScriptBCEmitter::EmitPop32(ScriptRegister output_reg)
// {
//     // POP32 [%r32]
//     printf("pop32 %s\n", GetRegisterName(output_reg));

//     WriteOp(OpBase_Pop, (OpSpecPop_Int32 << 4) | (output_reg & 0x0F));

//     mStackOffset -= 4;
// }

// void ScriptBCEmitter::EmitLoad32(int offset, ScriptRegister output_reg)
// {
//     // LOAD [i16] [%r32]

//     printf("load32  %d, %s\n", offset, GetRegisterName(output_reg));

//     WriteOp(OpBase_Load, (OpSpecLoad_Int32 << 4) | (output_reg & 0x0F));
//     Write16(static_cast<uint16>(offset));
// }

// void ScriptBCEmitter::EmitLoadAbsolute32(uint32 position, ScriptRegister output_reg)
// {
//     // LOADA [i32] [%r32]
//     printf("load32a %u, %s\n", position, GetRegisterName(output_reg));

//     WriteOp(OpBase_Load, (OpSpecLoad_AbsoluteInt32 << 4) | (output_reg & 0x0F));
//     Write32(position);
// }

// void ScriptBCEmitter::EmitJumpRelative(uint16 offset)
// {
//     printf("jmpr %d\n", offset);

//     WriteOp(OpBase_Jump, OpSpecJump_Relative);
//     Write16(offset);
// }

// void ScriptBCEmitter::EmitJumpAbsolute(uint32 position)
// {
//     printf("jmpa %d\n", position);

//     WriteOp(OpBase_Jump, OpSpecJump_Absolute);
//     Write32(position);
// }

// void ScriptBCEmitter::EmitJumpAbsoluteReg32(ScriptRegister reg)
// {
//     printf("jmpar %s\n", ScriptBCEmitter::GetRegisterName(reg));

//     WriteOp(OpBase_Jump, OpSpecJump_AbsoluteReg32);
//     Write16(reg);
// }

// void ScriptBCEmitter::EmitJumpCallAbsolute(uint32 position)
// {
//     printf("calla %u\n", position);

//     WriteOp(OpBase_Jump, OpSpecJump_CallAbsolute);
//     Write32(position);
// }

// void ScriptBCEmitter::EmitJumpCallExternal(Hash32 hashed_name)
// {
//     printf("callext %u\n", hashed_name);

//     WriteOp(OpBase_Jump, OpSpecJump_CallExternal);
//     Write32(hashed_name);
// }

// void ScriptBCEmitter::EmitJumpReturnToCaller()
// {
//     printf("ret\n");

//     WriteOp(OpBase_Jump, OpSpecJump_ReturnToCaller);
// }

// void ScriptBCEmitter::EmitMoveInt32(ScriptRegister reg, uint32 value)
// {
//     printf("move32 %s, %u\n", GetRegisterName(reg), value);

//     WriteOp(OpBase_Move, (OpSpecMove_Int32 << 4) | (reg & 0x0F));
//     Write32(value);
// }

// void ScriptBCEmitter::EmitParamsStart() { WriteOp(OpBase_Data, OpSpecData_ParamsStart); }

// void ScriptBCEmitter::EmitType(ScriptValue::ValueType type)
// {
//     OpSpecType op_type = OpSpecType_Int;

//     if (type == ScriptValue::STRING) {
//         op_type = OpSpecType_String;
//     }

//     WriteOp(OpBase_Type, op_type);
// }

// uint32 ScriptBCEmitter::EmitDataString(char* str, uint16 length)
// {
//     WriteOp(OpBase_Data, OpSpecData_String);

//     uint32 start_index = mBytecode.Size();

//     uint16 final_length = length + 1;

//     // If the length is not a factor of 2 (sizeof uint16) then add a byte of
//     // padding
//     if ((final_length & 0x01)) {
//         ++final_length;
//     }

//     Write16(final_length);

//     for (int i = 0; i < final_length; i += 2) {
//         mBytecode.Insert(str[i]);

//         if (i >= length) {
//             mBytecode.Insert(0);
//             break;
//         }

//         mBytecode.Insert(str[i + 1]);
//     }

//     return start_index;
// }

// ScriptRegister ScriptBCEmitter::EmitBinop(AstBinop* binop, ScriptBytecodeVarHandle* handle)
// {
//     bool will_preserve_lhs = false;
//     // Load the A and B values into the registers
//     ScriptRegister a_reg = EmitRhs(binop->Left, RhsMode::RHS_FETCH_TO_REGISTER, handle);

//     // Since there is a chance that this register will be clobbered (by binop,
//     // action call, etc), we will push the value of the register here and return
//     // it after processing the RHS
//     if (binop->Right->NodeType != FX_AST_LITERAL) {
//         will_preserve_lhs = true;
//         EmitPush32r(a_reg);
//     }

//     ScriptRegister b_reg = EmitRhs(binop->Right, RhsMode::RHS_FETCH_TO_REGISTER, handle);

//     // Retrieve the previous LHS
//     if (will_preserve_lhs) {
//         EmitPop32(a_reg);
//     }

//     if (binop->OpToken->Type == TT::Plus) {
//         printf("add %s, %s\n", GetRegisterName(a_reg), GetRegisterName(b_reg));

//         WriteOp(OpBase_Arith, OpSpecArith_Add);

//         mBytecode.Insert(a_reg);
//         mBytecode.Insert(b_reg);
//     }

//     // We no longer need the lhs or rhs registers, free em
//     MARK_REGISTER_FREE(a_reg);
//     MARK_REGISTER_FREE(b_reg);

//     return FX_REG_XR;
// }

// ScriptRegister ScriptBCEmitter::EmitVarFetch(AstVarRef* ref, RhsMode mode)
// {
//     ScriptBytecodeVarHandle* var_handle = FindVarHandle(ref->Name->GetHash());

//     bool force_absolute_load = false;

//     // If the variable is from a previous scope, load it from an absolute address.
//     // local offsets will change depending on where they are called.
//     if (var_handle->ScopeIndex < mScopeIndex) {
//         force_absolute_load = true;
//     }

//     if (!var_handle) {
//         printf("Could not find var handle!");
//         return FX_REG_NONE;
//     }

//     ScriptRegister reg = FindFreeRegister();

//     MARK_REGISTER_USED(reg);

//     DoLoad(var_handle->Offset, reg, force_absolute_load);

//     if (mode == RhsMode::RHS_FETCH_TO_REGISTER) {
//         return reg;
//     }

//     // If we are just copying the variable to this new variable, we can free the
//     // register after we push to the stack.
//     if (mode == RhsMode::RHS_DEFINE_IN_MEMORY) {
//         if (var_handle->Type == ScriptValue::STRING) {
//             EmitType(var_handle->Type);
//         }

//         EmitPush32r(reg);
//         MARK_REGISTER_FREE(reg);

//         return FX_REG_NONE;
//     }

//     // This is a reference to a variable, return the register we loaded it into
//     return reg;
// }

// void ScriptBCEmitter::DoLoad(uint32 stack_offset, ScriptRegister output_reg, bool force_absolute)
// {
//     if (stack_offset < 0xFFFE && !force_absolute) {
//         // Relative load

//         // Calculate the relative index to the current stack offset
//         int input_offset = -(static_cast<int>(mStackOffset) - static_cast<int>(stack_offset));

//         EmitLoad32(input_offset, output_reg);
//     }
//     else {
//         // Absolute load
//         EmitLoadAbsolute32(stack_offset, output_reg);
//     }
// }

// void ScriptBCEmitter::DoSaveInt32(uint32 stack_offset, uint32 value, bool force_absolute)
// {
//     if (stack_offset < 0xFFFE && !force_absolute) {
//         // Relative save

//         // Calculate the relative index to the current stack offset
//         int input_offset = -(static_cast<int>(mStackOffset) - static_cast<int>(stack_offset));

//         EmitSave32(input_offset, value);
//     }
//     else {
//         // Absolute save
//         EmitSaveAbsolute32(stack_offset, value);
//     }
// }

// void ScriptBCEmitter::DoSaveReg32(uint32 stack_offset, ScriptRegister reg, bool force_absolute)
// {
//     if (stack_offset < 0xFFFE && !force_absolute) {
//         // Relative save

//         // Calculate the relative index to the current stack offset
//         int input_offset = -(static_cast<int>(mStackOffset) - static_cast<int>(stack_offset));

//         EmitSaveReg32(input_offset, reg);
//     }
//     else {
//         // Absolute save
//         EmitSaveAbsoluteReg32(stack_offset, reg);
//     }
// }

// void ScriptBCEmitter::EmitAssign(AstAssign* assign)
// {
//     ScriptBytecodeVarHandle* var_handle = FindVarHandle(assign->Var->Name->GetHash());
//     if (var_handle == nullptr) {
//         printf("!!! Var '%.*s' does not exist!\n", assign->Var->Name->Length, assign->Var->Name->Start);
//         return;
//     }

//     // bool force_absolute_save = false;

//     // if (var_handle->ScopeIndex < mScopeIndex) {
//     //     force_absolute_save = true;
//     // }

//     // int output_offset = -(static_cast<int>(mStackOffset) -
//     // static_cast<int>(var_handle->Offset));

//     if (!var_handle) {
//         printf("Could not find var handle to assign to!");
//         return;
//     }

//     EmitRhs(assign->Rhs, RhsMode::RHS_ASSIGN_TO_HANDLE, var_handle);
// }

// ScriptRegister ScriptBCEmitter::EmitLiteralInt(AstLiteral* literal, RhsMode mode,
//                                                    ScriptBytecodeVarHandle* handle)
// {
//     // If this is on variable definition, push the value to the stack.
//     if (mode == RhsMode::RHS_DEFINE_IN_MEMORY) {
//         EmitPush32(literal->Value.ValueInt);

//         return FX_REG_NONE;
//     }

//     // If this is as a literal, push the value to the stack and pop onto the
//     // target register.
//     else if (mode == RhsMode::RHS_FETCH_TO_REGISTER) {
//         // EmitPush32(literal->Value.ValueInt);

//         ScriptRegister output_reg = FindFreeRegister();
//         // EmitPop32(output_reg);

//         EmitMoveInt32(output_reg, literal->Value.ValueInt);

//         // Mark the output register as used to store it
//         MARK_REGISTER_USED(output_reg);

//         return output_reg;
//     }

//     else if (mode == RhsMode::RHS_ASSIGN_TO_HANDLE) {
//         const bool force_absolute_save = (handle->ScopeIndex < mScopeIndex);
//         DoSaveInt32(handle->Offset, literal->Value.ValueInt, force_absolute_save);

//         return FX_REG_NONE;
//     }

//     return FX_REG_NONE;
// }

// ScriptRegister ScriptBCEmitter::EmitLiteralString(AstLiteral* literal, RhsMode mode,
//                                                       ScriptBytecodeVarHandle* handle)
// {
//     const uint32 string_length = strlen(literal->Value.ValueString);

//     // Emit the length and string data
//     const uint32 string_position = EmitDataString(literal->Value.ValueString, string_length);

//     // local string some_value = "Some String";
//     if (mode == RhsMode::RHS_DEFINE_IN_MEMORY) {
//         // Push the location and mark it as a pointer to a string
//         EmitType(ScriptValue::STRING);
//         EmitPush32(string_position);

//         return FX_REG_NONE;
//     }

//     // some_function("Some String")
//     else if (mode == RhsMode::RHS_FETCH_TO_REGISTER) {
//         // Push the location for the string and pop it back to a register.
//         EmitType(ScriptValue::STRING);

//         // Push the string position
//         // EmitPush32(string_position);

//         // Find a register to output to and write the index
//         ScriptRegister output_reg = FindFreeRegister();
//         // EmitPop32(output_reg);

//         EmitMoveInt32(output_reg, string_position);

//         // Mark the output register as used to store it
//         MARK_REGISTER_USED(output_reg);

//         return output_reg;
//     }

//     // some_previous_value = "Some String";
//     else if (mode == RhsMode::RHS_ASSIGN_TO_HANDLE) {
//         const bool force_absolute_save = (handle->ScopeIndex < mScopeIndex);

//         DoSaveInt32(handle->Offset, string_position, force_absolute_save);
//         handle->Type = ScriptValue::STRING;

//         return FX_REG_NONE;
//     }

//     return FX_REG_NONE;
// }

// ScriptRegister ScriptBCEmitter::EmitRhs(AstNode* rhs, ScriptBCEmitter::RhsMode mode,
//                                             ScriptBytecodeVarHandle* handle)
// {
//     if (rhs->NodeType == FX_AST_LITERAL) {
//         AstLiteral* literal = reinterpret_cast<AstLiteral*>(rhs);

//         if (literal->Value.Type == ScriptValue::INT) {
//             return EmitLiteralInt(literal, mode, handle);
//         }
//         else if (literal->Value.Type == ScriptValue::STRING) {
//             return EmitLiteralString(literal, mode, handle);
//         }
//         else if (literal->Value.Type == ScriptValue::REF) {
//             // Reference another value, load from memory into register
//             ScriptRegister output_register = EmitVarFetch(literal->Value.ValueRef, mode);
//             if (mode == RhsMode::RHS_ASSIGN_TO_HANDLE) {
//                 DoSaveReg32(handle->Offset, output_register);
//             }

//             return output_register;
//         }

//         return FX_REG_NONE;
//     }

//     else if (rhs->NodeType == FX_AST_ACTIONCALL || rhs->NodeType == FX_AST_BINOP) {
//         ScriptRegister result_register = FX_REG_XR;

//         if (rhs->NodeType == FX_AST_BINOP) {
//             result_register = EmitBinop(reinterpret_cast<AstBinop*>(rhs), handle);
//         }

//         else if (rhs->NodeType == FX_AST_ACTIONCALL) {
//             DoActionCall(reinterpret_cast<AstActionCall*>(rhs));
//             // Action results are stored in XR
//             result_register = FX_REG_XR;
//         }

//         if (mode == RhsMode::RHS_DEFINE_IN_MEMORY) {
//             uint32 offset = mStackOffset;
//             EmitPush32r(result_register);

//             if (handle) {
//                 handle->Offset = offset;
//             }

//             return FX_REG_NONE;
//         }

//         else if (mode == RhsMode::RHS_FETCH_TO_REGISTER) {
//             // Push the result to a register
//             EmitPush32r(result_register);

//             // Find a register to output to, and pop the value to there.
//             ScriptRegister output_reg = FindFreeRegister();
//             EmitPop32(output_reg);

//             // Mark the output register as used to store it
//             MARK_REGISTER_USED(output_reg);

//             return output_reg;
//         }
//         else if (mode == RhsMode::RHS_ASSIGN_TO_HANDLE) {
//             const bool force_absolute_save = (handle->ScopeIndex < mScopeIndex);

//             // Save the value back to the variable
//             DoSaveReg32(handle->Offset, result_register, force_absolute_save);

//             return FX_REG_NONE;
//         }
//     }

//     return FX_REG_NONE;
// }

// ScriptBytecodeVarHandle* ScriptBCEmitter::DoVarDeclare(AstVarDecl* decl, VarDeclareMode mode)
// {
//     RETURN_VALUE_IF_NO_NODE(decl, nullptr);

//     const uint16 size_of_type = static_cast<uint16>(sizeof(int32));

//     const Hash32 type_int = HashStr32("int");
//     const Hash32 type_string = HashStr32("string");

//     Hash32 decl_hash = decl->Name->GetHash();

//     ScriptValue::ValueType value_type = ScriptValue::INT;

//     switch (decl_hash) {
//     case type_int:
//         value_type = ScriptValue::INT;
//         break;
//     case type_string:
//         printf("USING STRING\n");
//         value_type = ScriptValue::STRING;
//         break;
//     };

//     ScriptBytecodeVarHandle handle {
//         .HashedName = decl->Name->GetHash(),
//         .Type = value_type, // Just int for now
//         .Offset = (mStackOffset),
//         .SizeOnStack = size_of_type,
//         .ScopeIndex = mScopeIndex,
//     };

//     VarHandles.Insert(handle);

//     printf("\n# variable %.*s:\n", decl->Name->Length, decl->Name->Start);

//     ScriptBytecodeVarHandle* inserted_handle = &VarHandles[VarHandles.Size() - 1];

//     if (mode == DECLARE_NO_EMIT) {
//         // Do not emit any values
//         return inserted_handle;
//     }

//     if (decl->Assignment) {
//         AstNode* rhs = decl->Assignment->Rhs;

//         EmitRhs(rhs, RhsMode::RHS_DEFINE_IN_MEMORY, inserted_handle);

//         // EmitPush32(0);

//         // EmitRhs(rhs, RhsMode::RHS_ASSIGN_TO_HANDLE, inserted_handle);
//     }
//     else {
//         // There is no assignment, push zero as the value for now and
//         // a later assignment can set it using save32.
//         EmitPush32(0);
//     }

//     return inserted_handle;
// }

// void ScriptBCEmitter::DoActionCall(AstActionCall* call)
// {
//     RETURN_IF_NO_NODE(call);

//     ScriptBytecodeActionHandle* handle = FindActionHandle(call->HashedName);

//     std::vector<uint32> call_locations;
//     call_locations.reserve(8);

//     // Push all params to stack
//     for (AstNode* param : call->Params) {
//         // ScriptRegister reg =
//         if (param->NodeType == FX_AST_ACTIONCALL) {
//             call_locations.push_back(mStackOffset);
//             EmitRhs(param, RhsMode::RHS_DEFINE_IN_MEMORY, nullptr);
//         }
//         // MARK_REGISTER_FREE(reg);
//     }

//     EmitPush32r(FX_REG_RA);

//     EmitParamsStart();

//     int call_location_index = 0;

//     // Push all params to stack
//     for (AstNode* param : call->Params) {
//         if (param->NodeType == FX_AST_ACTIONCALL) {
//             ScriptRegister temp_register = FindFreeRegister();

//             DoLoad(call_locations[call_location_index], temp_register);
//             call_location_index++;

//             EmitPush32r(temp_register);

//             continue;
//         }

//         EmitRhs(param, RhsMode::RHS_DEFINE_IN_MEMORY, nullptr);
//     }

//     // The handle could not be found, write it as a possible external symbol.
//     if (!handle) {
//         printf("Call name-> %u\n", call->HashedName);

//         // Since popping the parameters are handled internally in the VM,
//         // we need to decrement the stack offset here.
//         for (int i = 0; i < call->Params.size(); i++) {
//             mStackOffset -= 4;
//         }

//         EmitJumpCallExternal(call->HashedName);

//         EmitPop32(FX_REG_RA);
//         return;
//     }

//     EmitJumpCallAbsolute(handle->BytecodeIndex);

//     EmitPop32(FX_REG_RA);
// }

// ScriptBytecodeVarHandle* ScriptBCEmitter::DefineAndFetchParam(AstNode* param_decl_node)
// {
//     if (param_decl_node->NodeType != FX_AST_VARDECL) {
//         printf("!!! param node type is not vardecl!\n");
//         return nullptr;
//     }

//     // Emit variable without emitting pushes or pops
//     ScriptBytecodeVarHandle* handle = DoVarDeclare(reinterpret_cast<AstVarDecl*>(param_decl_node),
//     DECLARE_NO_EMIT);

//     if (!handle) {
//         printf("!!! could not define and fetch param!\n");
//         return nullptr;
//     }

//     assert(handle->SizeOnStack == 4);

//     mStackOffset += handle->SizeOnStack;

//     return handle;
// }

// ScriptBytecodeVarHandle* ScriptBCEmitter::DefineReturnVar(AstVarDecl* decl)
// {
//     RETURN_VALUE_IF_NO_NODE(decl, nullptr);

//     return DoVarDeclare(decl);
// }

// void ScriptBCEmitter::EmitAction(AstActionDecl* action)
// {
//     RETURN_IF_NO_NODE(action);

//     ++mScopeIndex;

//     // Store the bytecode offset before the action is emitted

//     const size_t start_of_action = mBytecode.Size();
//     printf("Start of action %zu\n", start_of_action);

//     // Emit the jump instruction, we will update the jump position after emitting
//     // all of the code inside the block
//     EmitJumpRelative(0);

//     // const uint32 initial_stack_offset = mStackOffset;

//     const size_t header_jump_start_index = start_of_action + sizeof(uint16);

//     printf("%.*s:\n", action->Name->Length, action->Name->Start);

//     size_t start_var_handle_count = VarHandles.Size();

//     // Offset for the pushed return address
//     mStackOffset += 4;

//     for (AstNode* param_decl_node : action->Params->Statements) {
//         DefineAndFetchParam(param_decl_node);
//     }

//     ScriptBytecodeVarHandle* return_var = DefineReturnVar(action->ReturnVar);

//     EmitBlock(action->Block);

//     if (return_var) {
//         // const int offset = -(static_cast<int>(mStackOffset) -
//         // static_cast<int>(return_var->Offset)); EmitLoad32(offset, FX_REG_XR);

//         DoLoad(return_var->Offset, FX_REG_XR);
//     }

//     // Return offset back to pre-call
//     mStackOffset -= 4;

//     EmitJumpReturnToCaller();

//     const size_t end_of_action = mBytecode.Size();
//     const uint16 distance_to_action = static_cast<uint16>(end_of_action - (start_of_action)-4);

//     printf("End of action: %zu\n", end_of_action);
//     printf("Distance to action: %d\n", distance_to_action);

//     // Update the jump to the end of the action
//     mBytecode[header_jump_start_index] = static_cast<uint8>(distance_to_action >> 8);
//     mBytecode[header_jump_start_index + 1] = static_cast<uint8>((distance_to_action & 0xFF));

//     ScriptBytecodeActionHandle action_handle { .HashedName = action->Name->GetHash(),
//                                                  .BytecodeIndex = static_cast<uint32>(start_of_action + 4) };

//     const size_t number_of_scope_var_handles = VarHandles.Size() - start_var_handle_count;
//     printf("Number of var handles to remove: %zu\n", number_of_scope_var_handles);

//     ActionHandles.push_back(action_handle);

//     --mScopeIndex;

//     for (int i = 0; i < number_of_scope_var_handles; i++) {
//         ScriptBytecodeVarHandle* var = VarHandles.RemoveLast();
//         assert(var->SizeOnStack == 4);
//         mStackOffset -= var->SizeOnStack;
//     }
// }

// void ScriptBCEmitter::EmitBlock(AstBlock* block)
// {
//     RETURN_IF_NO_NODE(block);

//     for (AstNode* node : block->Statements) {
//         Emit(node);
//     }
// }

// void ScriptBCEmitter::PrintBytecode()
// {
//     const size_t size = mBytecode.Size();
//     for (int i = 0; i < 25; i++) {
//         printf("%02d ", i);
//     }
//     printf("\n");
//     for (int i = 0; i < 25; i++) {
//         printf("---");
//     }
//     printf("\n");

//     for (size_t i = 0; i < size; i++) {
//         printf("%02X ", mBytecode[i]);

//         if (i > 0 && ((i + 1) % 25) == 0) {
//             printf("\n");
//         }
//     }
//     printf("\n");
// }

// /////////////////////////////////////
// // Bytecode Printer
// /////////////////////////////////////

// uint16 ScriptBCPrinter::Read16()
// {
//     uint8 lo = mBytecode[mBytecodeIndex++];
//     uint8 hi = mBytecode[mBytecodeIndex++];

//     return ((static_cast<uint16>(lo) << 8) | hi);
// }

// uint32 ScriptBCPrinter::Read32()
// {
//     uint16 lo = Read16();
//     uint16 hi = Read16();

//     return ((static_cast<uint32>(lo) << 16) | hi);
// }

// #define BC_PRINT_OP(fmt_, ...) snprintf(s, 128, fmt_, ##__VA_ARGS__)

// void ScriptBCPrinter::DoLoad(char* s, uint8 op_base, uint8 op_spec_raw)
// {
//     uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
//     uint8 op_reg = (op_spec_raw & 0x0F);

//     if (op_spec == OpSpecLoad_Int32) {
//         int16 offset = Read16();
//         BC_PRINT_OP("load32 %d, %s", offset,
//         ScriptBCEmitter::GetRegisterName(static_cast<ScriptRegister>(op_reg)));
//     }
//     else if (op_spec == OpSpecLoad_AbsoluteInt32) {
//         uint32 offset = Read32();
//         BC_PRINT_OP("load32a %u, %s", offset,
//                     ScriptBCEmitter::GetRegisterName(static_cast<ScriptRegister>(op_reg)));
//     }
// }

// void ScriptBCPrinter::DoPush(char* s, uint8 op_base, uint8 op_spec)
// {
//     if (op_spec == OpSpecPush_Int32) {
//         uint32 value = Read32();
//         BC_PRINT_OP("push32 %u", value);
//     }
//     else if (op_spec == OpSpecPush_Reg32) {
//         uint16 reg = Read16();
//         BC_PRINT_OP("push32r %s", ScriptBCEmitter::GetRegisterName(static_cast<ScriptRegister>(reg)));
//     }
// }

// void ScriptBCPrinter::DoPop(char* s, uint8 op_base, uint8 op_spec_raw)
// {
//     uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
//     uint8 op_reg = (op_spec_raw & 0x0F);

//     if (op_spec == OpSpecPush_Int32) {
//         BC_PRINT_OP("pop32 %s", ScriptBCEmitter::GetRegisterName(static_cast<ScriptRegister>(op_reg)));
//     }
// }

// void ScriptBCPrinter::DoArith(char* s, uint8 op_base, uint8 op_spec)
// {
//     uint8 a_reg = mBytecode[mBytecodeIndex++];
//     uint8 b_reg = mBytecode[mBytecodeIndex++];

//     if (op_spec == OpSpecArith_Add) {
//         BC_PRINT_OP("add32 %s, %s", ScriptBCEmitter::GetRegisterName(static_cast<ScriptRegister>(a_reg)),
//                     ScriptBCEmitter::GetRegisterName(static_cast<ScriptRegister>(b_reg)));
//     }
// }

// void ScriptBCPrinter::DoSave(char* s, uint8 op_base, uint8 op_spec)
// {
//     // Save a imm32 into an offset in the stack
//     if (op_spec == OpSpecSave_Int32) {
//         const int16 offset = Read16();
//         const uint32 value = Read32();

//         BC_PRINT_OP("save32 %d, %u", offset, value);
//     }

//     // Save a register into an offset in the stack
//     else if (op_spec == OpSpecSave_Reg32) {
//         const int16 offset = Read16();
//         uint16 reg = Read16();

//         BC_PRINT_OP("save32r %d, %s", offset,
//         ScriptBCEmitter::GetRegisterName(static_cast<ScriptRegister>(reg)));
//     }
//     else if (op_spec == OpSpecSave_AbsoluteInt32) {
//         const uint32 offset = Read32();
//         const uint32 value = Read32();

//         BC_PRINT_OP("save32a %u, %u", offset, value);
//     }
//     else if (op_spec == OpSpecSave_AbsoluteReg32) {
//         const uint32 offset = Read32();
//         uint16 reg = Read16();

//         BC_PRINT_OP("save32ar %u, %s", offset,
//         ScriptBCEmitter::GetRegisterName(static_cast<ScriptRegister>(reg)));
//     }
// }

// void ScriptBCPrinter::DoJump(char* s, uint8 op_base, uint8 op_spec)
// {
//     if (op_spec == OpSpecJump_Relative) {
//         uint16 offset = Read16();
//         printf("# jump relative to (%u)\n", mBytecodeIndex + offset);
//         BC_PRINT_OP("jmpr %d", offset);
//     }
//     else if (op_spec == OpSpecJump_Absolute) {
//         uint32 position = Read32();
//         BC_PRINT_OP("jmpa %u", position);
//     }
//     else if (op_spec == OpSpecJump_AbsoluteReg32) {
//         uint16 reg = Read16();
//         BC_PRINT_OP("jmpar %s", ScriptBCEmitter::GetRegisterName(static_cast<ScriptRegister>(reg)));
//     }
//     else if (op_spec == OpSpecJump_CallAbsolute) {
//         uint32 position = Read32();
//         BC_PRINT_OP("calla %u", position);
//     }
//     else if (op_spec == OpSpecJump_ReturnToCaller) {
//         BC_PRINT_OP("ret");
//     }
//     else if (op_spec == OpSpecJump_CallExternal) {
//         uint32 hashed_name = Read32();
//         BC_PRINT_OP("callext %u", hashed_name);
//     }
// }

// void ScriptBCPrinter::DoData(char* s, uint8 op_base, uint8 op_spec)
// {
//     if (op_spec == OpSpecData_String) {
//         uint16 length = Read16();
//         char* data_str = FX_SCRIPT_ALLOC_MEMORY(char, length);
//         uint16* data_str16 = reinterpret_cast<uint16*>(data_str);

//         uint32 bytecode_end = mBytecodeIndex + length;
//         int data_index = 0;
//         while (mBytecodeIndex < bytecode_end) {
//             uint16 value16 = Read16();
//             data_str16[data_index++] = ((value16 << 8) | (value16 >> 8));
//         }

//         BC_PRINT_OP("datastr %d, %.*s", length, length, data_str);

//         FX_SCRIPT_FREE(char, data_str);
//     }
//     else if (op_spec == OpSpecData_ParamsStart) {
//         BC_PRINT_OP("paramsstart");
//     }
// }

// void ScriptBCPrinter::DoType(char* s, uint8 op_base, uint8 op_spec)
// {
//     if (op_spec == OpSpecType_Int) {
//         BC_PRINT_OP("typeint");
//     }
//     else if (op_spec == OpSpecType_String) {
//         BC_PRINT_OP("typestr");
//     }
// }

// void ScriptBCPrinter::DoMove(char* s, uint8 op_base, uint8 op_spec_raw)
// {
//     uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
//     uint8 op_reg = (op_spec_raw & 0x0F);

//     if (op_spec == OpSpecMove_Int32) {
//         uint32 value = Read32();
//         BC_PRINT_OP("move32 %s, %u\t", ScriptBCEmitter::GetRegisterName(static_cast<ScriptRegister>(op_reg)),
//                     value);
//     }
// }

// void ScriptBCPrinter::Print()
// {
//     while (mBytecodeIndex < mBytecode.Size()) {
//         PrintOp();
//     }
// }

// void ScriptBCPrinter::PrintOp()
// {
//     uint32 bc_index = mBytecodeIndex;

//     uint16 op_full = Read16();

//     const uint8 op_base = static_cast<uint8>(op_full >> 8);
//     const uint8 op_spec = static_cast<uint8>(op_full & 0xFF);

//     char s[128];

//     switch (op_base) {
//     case OpBase_Push:
//         DoPush(s, op_base, op_spec);
//         break;
//     case OpBase_Pop:
//         DoPop(s, op_base, op_spec);
//         break;
//     case OpBase_Load:
//         DoLoad(s, op_base, op_spec);
//         break;
//     case OpBase_Arith:
//         DoArith(s, op_base, op_spec);
//         break;
//     case OpBase_Jump:
//         DoJump(s, op_base, op_spec);
//         break;
//     case OpBase_Save:
//         DoSave(s, op_base, op_spec);
//         break;
//     case OpBase_Data:
//         DoData(s, op_base, op_spec);
//         break;
//     case OpBase_Type:
//         DoType(s, op_base, op_spec);
//         break;
//     case OpBase_Move:
//         DoMove(s, op_base, op_spec);
//         break;
//     }

//     printf("%-25s", s);

//     printf("\t# Offset: %u\n", bc_index);
// }

// ///////////////////////////////////////////
// // Bytecode VM
// ///////////////////////////////////////////

// void ScriptVM::PrintRegisters()
// {
//     printf("\n=== Register Dump ===\n\n");
//     printf("X0=%u\tX1=%u\tX2=%u\tX3=%u\n", Registers[FX_REG_X0], Registers[FX_REG_X1], Registers[FX_REG_X2],
//            Registers[FX_REG_X3]);

//     printf("XR=%u\tRA=%u\n", Registers[FX_REG_XR], Registers[FX_REG_RA]);

//     printf("\n=====================\n\n");
// }

// uint16 ScriptVM::Read16()
// {
//     uint8 lo = mBytecode[mPC++];
//     uint8 hi = mBytecode[mPC++];

//     return ((static_cast<uint16>(lo) << 8) | hi);
// }

// uint32 ScriptVM::Read32()
// {
//     uint16 lo = Read16();
//     uint16 hi = Read16();

//     return ((static_cast<uint32>(lo) << 16) | hi);
// }

// void ScriptVM::Push16(uint16 value)
// {
//     uint16* dptr = reinterpret_cast<uint16*>(Stack + Registers[FX_REG_SP]);
//     (*dptr) = value;

//     Registers[FX_REG_SP] += sizeof(uint16);
// }

// void ScriptVM::Push32(uint32 value)
// {
//     uint32* dptr = reinterpret_cast<uint32*>(Stack + Registers[FX_REG_SP]);
//     (*dptr) = value;

//     Registers[FX_REG_SP] += sizeof(uint32);
// }

// uint32 ScriptVM::Pop32()
// {
//     if (Registers[FX_REG_SP] == 0) {
//         printf("ERR\n");
//     }

//     Registers[FX_REG_SP] -= sizeof(uint32);

//     uint32 value = *reinterpret_cast<uint32*>(Stack + Registers[FX_REG_SP]);
//     return value;
// }

// ScriptVMCallFrame& ScriptVM::PushCallFrame()
// {
//     mIsInCallFrame = true;

//     ScriptVMCallFrame& start_frame = mCallFrames[mCallFrameIndex++];
//     start_frame.StartStackIndex = Registers[FX_REG_SP];

//     return start_frame;
// }

// void ScriptVM::PopCallFrame()
// {
//     ScriptVMCallFrame* frame = GetCurrentCallFrame();
//     if (!frame) {
//         FX_BREAKPOINT;
//     }

//     while (Registers[FX_REG_SP] > frame->StartStackIndex) {
//         Pop32();
//     }

//     --mCallFrameIndex;

//     if (mCallFrameIndex == 0) {
//         mIsInCallFrame = false;
//     }
// }

// ScriptVMCallFrame* ScriptVM::GetCurrentCallFrame()
// {
//     if (!mIsInCallFrame || mCallFrameIndex < 1) {
//         return nullptr;
//     }

//     return &mCallFrames[mCallFrameIndex - 1];
// }

// ScriptExternalFunc* ScriptVM::FindExternalAction(Hash32 hashed_name)
// {
//     for (ScriptExternalFunc& func : mExternalFuncs) {
//         if (func.HashedName == hashed_name) {
//             return &func;
//         }
//     }
//     return nullptr;
// }

// void ScriptVM::ExecuteOp()
// {
//     uint16 op_full = Read16();

//     const uint8 op_base = static_cast<uint8>(op_full >> 8);
//     const uint8 op_spec = static_cast<uint8>(op_full & 0xFF);

//     switch (op_base) {
//     case OpBase_Push:
//         DoPush(op_base, op_spec);
//         break;
//     case OpBase_Pop:
//         DoPop(op_base, op_spec);
//         break;
//     case OpBase_Load:
//         DoLoad(op_base, op_spec);
//         break;
//     case OpBase_Arith:
//         DoArith(op_base, op_spec);
//         break;
//     case OpBase_Jump:
//         DoJump(op_base, op_spec);
//         break;
//     case OpBase_Save:
//         DoSave(op_base, op_spec);
//         break;
//     case OpBase_Data:
//         DoData(op_base, op_spec);
//         break;
//     case OpBase_Type:
//         DoType(op_base, op_spec);
//         break;
//     case OpBase_Move:
//         DoMove(op_base, op_spec);
//         break;
//     }
// }

// void ScriptVM::DoLoad(uint8 op_base, uint8 op_spec_raw)
// {
//     uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
//     uint8 op_reg = (op_spec_raw & 0x0F);

//     if (op_spec == OpSpecLoad_Int32) {
//         int16 offset = Read16();

//         uint8* dataptr = &Stack[Registers[FX_REG_SP] + offset];
//         uint32 data32 = *reinterpret_cast<uint32*>(dataptr);

//         Registers[op_reg] = data32;
//     }
//     else if (op_spec == OpSpecLoad_AbsoluteInt32) {
//         uint32 offset = Read32();

//         uint8* dataptr = &Stack[offset];
//         uint32 data32 = *reinterpret_cast<uint32*>(dataptr);

//         Registers[op_reg] = data32;
//     }
// }

// void ScriptVM::DoPush(uint8 op_base, uint8 op_spec)
// {
//     if (mIsInParams) {
//         if (mCurrentType != ScriptValue::NONETYPE) {
//             mPushedTypes.Insert(mCurrentType);
//             mCurrentType = ScriptValue::NONETYPE;
//         }
//         else {
//             mPushedTypes.Insert(ScriptValue::INT);
//         }
//     }

//     if (op_spec == OpSpecPush_Int32) {
//         uint32 value = Read32();
//         Push32(value);
//     }
//     else if (op_spec == OpSpecPush_Reg32) {
//         uint16 reg = Read16();
//         Push32(Registers[reg]);
//     }
// }

// void ScriptVM::DoPop(uint8 op_base, uint8 op_spec_raw)
// {
//     uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
//     uint8 op_reg = (op_spec_raw & 0x0F);

//     if (op_spec == OpSpecPush_Int32) {
//         uint32 value = Pop32();

//         Registers[op_reg] = value;
//     }

//     if (mIsInParams) {
//         mPushedTypes.RemoveLast();
//     }
// }

// void ScriptVM::DoArith(uint8 op_base, uint8 op_spec)
// {
//     uint8 a_reg = mBytecode[mPC++];
//     uint8 b_reg = mBytecode[mPC++];

//     if (op_spec == OpSpecArith_Add) {
//         Registers[FX_REG_XR] = Registers[a_reg] + Registers[b_reg];
//     }
// }

// void ScriptVM::DoSave(uint8 op_base, uint8 op_spec)
// {
//     uint32 offset;

//     if (op_spec == OpSpecSave_AbsoluteInt32 || op_spec == OpSpecSave_AbsoluteReg32) {
//         offset = Read32();
//     }
//     else {
//         // The offset is relative to the stack pointer, get the absolute value
//         const int16 relative_offset = static_cast<int16>(Read16());
//         offset = static_cast<uint32>(Registers[FX_REG_SP] + relative_offset);
//     }

//     uint32* dataptr = reinterpret_cast<uint32*>(&Stack[offset]);

//     // Save a imm32 into an offset in the stack
//     if (op_spec == OpSpecSave_Int32) {
//         (*dataptr) = Read32();
//     }

//     // Save a register into an offset in the stack
//     else if (op_spec == OpSpecSave_Reg32) {
//         uint16 reg = Read16();
//         (*dataptr) = Registers[reg];
//     }

//     else if (op_spec == OpSpecSave_AbsoluteInt32) {
//         (*dataptr) = Read32();
//     }

//     else if (op_spec == OpSpecSave_AbsoluteReg32) {
//         uint16 reg = Read16();
//         (*dataptr) = Registers[reg];
//     }
// }

// void ScriptVM::DoJump(uint8 op_base, uint8 op_spec)
// {
//     if (op_spec == OpSpecJump_Relative) {
//         uint16 offset = Read16();
//         mPC += offset;
//     }
//     else if (op_spec == OpSpecJump_Absolute) {
//         uint32 position = Read32();
//         mPC = position;
//     }
//     else if (op_spec == OpSpecJump_AbsoluteReg32) {
//         uint16 reg = Read16();
//         mPC = Registers[reg];
//     }
//     else if (op_spec == OpSpecJump_CallAbsolute) {
//         uint32 call_address = Read32();
//         // printf("Call to address % 4u\n", call_address);

//         // Push32(mPC);

//         Registers[FX_REG_RA] = mPC;

//         mPushedTypes.Clear();
//         mIsInParams = false;

//         PushCallFrame();

//         // Jump to the action address
//         mPC = call_address;
//     }
//     else if (op_spec == OpSpecJump_ReturnToCaller) {
//         PopCallFrame();

//         // uint32 return_address = Pop32();

//         uint32 return_address = Registers[FX_REG_RA];
//         // printf("Return to caller (%04d)\n", return_address);
//         mPC = return_address;

//         // Restore the return address register to its previous value. This is pushed
//         // when `paramsstart` is encountered.
//         // Registers[FX_REG_RA] = Pop32();
//     }
//     else if (op_spec == OpSpecJump_CallExternal) {
//         uint32 hashed_name = Read32();

//         ScriptExternalFunc* external_func = FindExternalAction(hashed_name);

//         if (!external_func) {
//             printf("!!! Could not find external function in VM!\n");
//             return;
//         }

//         std::vector<ScriptValue> params;
//         params.reserve(external_func->ParameterTypes.size());

//         uint32 num_params = mPushedTypes.Size();

//         printf("Num Params: %u\n", num_params);

//         for (int i = 0; i < num_params; i++) {
//             ScriptValue::ValueType param_type = mPushedTypes.GetLast();

//             ScriptValue value;
//             value.Type = param_type;

//             if (param_type == ScriptValue::INT) {
//                 value.ValueInt = Pop32();
//                 value.Type = param_type;
//             }
//             else if (param_type == ScriptValue::STRING) {
//                 uint32 string_location = Pop32();
//                 uint8* str_base_ptr = &mBytecode[string_location];
//                 // uint16 str_length = *((uint16*)str_base_ptr);

//                 str_base_ptr += 2;

//                 value.ValueString = reinterpret_cast<char*>(str_base_ptr);
//                 value.Type = param_type;
//             }

//             mPushedTypes.RemoveLast();

//             params.push_back(value);
//         }

//         mPushedTypes.Clear();
//         mIsInParams = false;

//         ScriptValue return_value {};
//         external_func->Function(this, params, &return_value);
//     }
// }

// void ScriptVM::DoData(uint8 op_base, uint8 op_spec)
// {
//     if (op_spec == OpSpecData_String) {
//         uint16 length = Read16();
//         mPC += length;
//     }
//     else if (op_spec == OpSpecData_ParamsStart) {
//         mIsInParams = true;

//         // Push the current return address pointer. This is so nested action calls
//         // can correctly navigate back to the caller.
//         // Push32(Registers[FX_REG_RA]);
//     }
// }

// void ScriptVM::DoType(uint8 op_base, uint8 op_spec)
// {
//     if (op_spec == OpSpecType_Int) {
//         mCurrentType = ScriptValue::INT;
//     }
//     else if (op_spec == OpSpecType_String) {
//         mCurrentType = ScriptValue::STRING;
//     }
// }

// void ScriptVM::DoMove(uint8 op_base, uint8 op_spec_raw)
// {
//     uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
//     ScriptRegister op_reg = static_cast<ScriptRegister>(op_spec_raw & 0x0F);

//     if (op_spec == OpSpecMove_Int32) {
//         int32 value = Read32();
//         Registers[op_reg] = value;
//     }
// }
