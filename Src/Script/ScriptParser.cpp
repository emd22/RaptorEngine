#include "ScriptParser.hpp"

#include "BytecodeEmitter.hpp"
// #include "FoxCGArm64.hpp"
#include "ScriptVM.hpp"

#include <Core/File.hpp>
#include <Core/Log.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Engine.hpp>
#include <Util/Tokenizer.hpp>

namespace fx::script {

#define FX_SCRIPT_ALLOC_NODE(type_)          gScriptMemPool->Alloc<type_>(sizeof(type_))
#define FX_SCRIPT_ALLOC_MEMORY(type_, size_) gScriptMemPool->Alloc<type_>(size_)
#define FX_SCRIPT_FREE(type_, ptr_)          gScriptMemPool->Free<type_>(ptr_);

void FoxScript::LoadFile(const char* path)
{
    File fp(path, File::eModType::Read, File::eDataType::Binary);

    if (!fp.IsFileOpen()) {
        // printf("[ERROR] Could not open config file at '%s'\n", path);
        LogError("Could not open script file at '{}'", path);
        return;
    }

    Slice<char> file_data = fp.Read<char>();
    mpFileData = file_data.pData;

    LogInfo("Length: {}", file_data.Size);

    Tokenizer tokenizer(mpFileData, file_data.Size);
    tokenizer.Tokenize();


    mTokens = std::move(tokenizer.mTokens);

    mScopes.Create(8);
    mCurrentScope = mScopes.Insert();
    mCurrentScope->Vars.Create(FX_SCRIPT_SCOPE_GLOBAL_VARS_START_SIZE);
    mCurrentScope->Functions.Create(FX_SCRIPT_SCOPE_GLOBAL_ACTIONS_START_SIZE);

    CreateInternalVariableTokens();
}

Token& FoxScript::GetToken(int offset)
{
    const uint32 idx = mTokenIndex + offset;
    if (idx < 0 || idx >= mTokens.Size()) {
        printf("SOMETHING IS MISSING\n");
    }
    assert(idx >= 0 && idx <= mTokens.Size());
    return mTokens[idx];
}

Token& FoxScript::EatToken(TT token_type)
{
    Token& token = GetToken();
    if (token.Type != token_type) {
        LogError("{}:{}: Unexpected token type {} when expecting {}!", token.FileLine, token.FileColumn,
                 Token::GetTypeName(token.Type), Token::GetTypeName(token_type));
        mHasErrors = true;
    }
    ++mTokenIndex;
    return token;
}

Token* FoxScript::CreateTokenFromString(eTokenType type, const char* text)
{
    const uint32 name_len = strlen(text);

    Token* token = FX_SCRIPT_ALLOC_MEMORY(Token, sizeof(Token));
    token->Start = FX_SCRIPT_ALLOC_MEMORY(char, name_len);
    token->End = token->Start + name_len;
    token->Length = name_len;
    token->Type = type;

    // Copy the name to the buffer in the name token
    memcpy(token->Start, text, name_len);

    return token;
}

void FoxScript::CreateInternalVariableTokens()
{
    mTokenReturnVar = CreateTokenFromString(TT::Identifier, FX_SCRIPT_VAR_RETURN_VAL);
}

static void PrintDocCommentExample(Token* comment, int example_tag_length, bool is_command_mode)
{
    char* start = comment->Start + example_tag_length;

    for (int i = 0; i < comment->Length - example_tag_length; i++) {
        char ch = start[i];

        if (!is_command_mode) {
            putchar(ch);
            continue;
        }

        // If we are in command mode, print the example using the command syntax
        if (ch == '(' || ch == ')' || ch == ',') {
            putchar(' ');
            continue;
        }

        if (ch == ' ' || ch == '\t' || ch == ';') {
            continue;
        }

        putchar(ch);
    }

    puts("");
}

static void PrintDocComment(Token* comment, bool is_command_mode)
{
    const char* example_tag = "EX: ";
    const int example_tag_length = 4;

    char* start = comment->Start;

    if (comment->Length > example_tag_length && !strncmp(start, example_tag, example_tag_length)) {
        printf("\n\t-> Script : ");
        PrintDocCommentExample(comment, example_tag_length, false);

        printf("\t-> Command: ");
        PrintDocCommentExample(comment, example_tag_length, true);

        return;
    }

    printf("%.*s\n", comment->Length, start);
}

FoxAstNode* FoxScript::TryParseKeyword(FoxAstBlock* parent_block)
{
    if (mTokenIndex >= mTokens.Size()) {
        return nullptr;
    }

    Token& tk = GetToken();
    Hash32 hash = tk.GetHash();

    // function [name] ( < [arg type] [arg name] ...> ) { <statements...> }
    constexpr Hash32 kw_proc = HashStr32("proc");

    constexpr Hash32 kw_extfn = HashStr32("extfn");

    // local [type] [name] <?assignment> ;
    constexpr Hash32 kw_local = HashStr32("local");

    // global [type] [name] <?assignment> ;
    constexpr Hash32 kw_global = HashStr32("global");

    // return ;
    constexpr Hash32 kw_return = HashStr32("return");

    // help [name of function] ;
    constexpr Hash32 kw_help = HashStr32("help");

    LogInfo("TK = {}, TK HASH = {}, PROC HASH = {}", tk.GetStr(), hash, kw_proc);

    // extern [name of function] ;

    if (hash == kw_proc) {
        EatToken(TT::Identifier);
        // ParseFunctionDeclare();
        return ParseProcedureDeclare();
    }
    if (hash == kw_extfn) {
        EatToken(TT::Identifier);
        // ParseFunctionDeclare();
        return ParseExtfnDeclare();
    }
    if (hash == kw_local) {
        EatToken(TT::Identifier);
        return ParseVarDeclare();
    }
    if (hash == kw_global) {
        EatToken(TT::Identifier);
        return ParseVarDeclare(&mScopes[0]);
    }
    if (hash == kw_return) {
        EatToken(TT::Identifier);

        FoxAstNode* return_rhs = nullptr;

        if (GetToken().Type != TT::Semicolon) {
            // There is a value that follows, get the value
            // FoxAstNode* rhs = ParseRhs();
            return_rhs = ParseRhs();


            // Assign the return value to __ReturnVal__

            // FoxAstVarRef* var_ref = FX_SCRIPT_ALLOC_NODE(FoxAstVarRef);
            // var_ref->Name = mTokenReturnVar;

            // FoxAstAssign* assign = FX_SCRIPT_ALLOC_NODE(FoxAstAssign);
            // assign->Var = var_ref;
            // assign->Rhs = rhs;

            // parent_block->Statements.push_back(assign);
        }

        FoxAstReturn* ret = FX_SCRIPT_ALLOC_NODE(FoxAstReturn);
        ret->pRhs = return_rhs;

        return ret;
    }
    if (hash == kw_help) {
        EatToken(TT::Identifier);

        Token& func_ref = EatToken(TT::Identifier);

        FoxFunction* function = FindFunction(func_ref.GetHash());

        if (function) {
            for (FoxAstDocComment* comment : function->pDeclaration->DocComments) {
                printf("[DOC] %.*s: ", function->Name->Length, function->Name->Start);
                PrintDocComment(comment->Comment, mInCommandMode);
            }
        }

        return nullptr;
    }

    return nullptr;
}

void FoxScript::PushScope()
{
    FoxScope* current = mCurrentScope;

    FoxScope* new_scope = mScopes.Insert();
    new_scope->Parent = current;
    new_scope->Vars.Create(FX_SCRIPT_SCOPE_LOCAL_VARS_START_SIZE);
    new_scope->Functions.Create(FX_SCRIPT_SCOPE_LOCAL_ACTIONS_START_SIZE);

    mCurrentScope = new_scope;
}

void FoxScript::PopScope()
{
    FoxScope* new_scope = mCurrentScope->Parent;
    mScopes.RemoveLast();

    assert(new_scope == &mScopes.GetLast());

    mCurrentScope = new_scope;
}

FoxAstVarDecl* FoxScript::InternalVarDeclare(Token* name_token, Token* type_token, FoxScope* scope)
{
    if (scope == nullptr) {
        scope = mCurrentScope;
    }

    // Allocate the declaration node
    FoxAstVarDecl* node = FX_SCRIPT_ALLOC_NODE(FoxAstVarDecl);

    node->Name = name_token;
    node->pType = type_token;
    node->DefineAsGlobal = (scope == &mScopes[0]);

    // Push the variable to the scope
    FoxVar var { name_token, type_token, scope };
    scope->Vars.Insert(var);

    return node;
}

FoxAstVarDecl* FoxScript::ParseVarDeclare(FoxScope* scope)
{
    if (scope == nullptr) {
        scope = mCurrentScope;
    }

    Token& type = EatToken(TT::Identifier);
    Token& name = EatToken(TT::Identifier);

    FoxAstVarDecl* node = FX_SCRIPT_ALLOC_NODE(FoxAstVarDecl);

    node->Name = &name;
    node->pType = &type;
    node->DefineAsGlobal = (scope == &mScopes[0]);

    FoxVar var { &type, &name, scope };

    node->Assignment = TryParseAssignment(node->Name);
    /*if (node->Assignment) {
        var.Value = node->Assignment->Value;
    }*/
    scope->Vars.Insert(var);

    return node;
}

// FoxVar& FoxConfigScript::ParseVarDeclare()
//{
//     Token& type = EatToken(TT::Identifier);
//     Token& name = EatToken(TT::Identifier);
//
//     FoxVar var{ name.GetHash(), &type, &name };
//
//     TryParseAssignment(var);
//
//     mCurrentScope->Vars.Insert(var);
//
//     return mCurrentScope->Vars.GetLast();
// }

FoxVar* FoxScript::FindVar(Hash32 hashed_name)
{
    FoxScope* scope = mCurrentScope;

    while (scope) {
        FoxVar* var = scope->FindVarInScope(hashed_name);
        if (var) {
            return var;
        }

        scope = scope->Parent;
    }

    return nullptr;
}

// FoxExternalFunc* FoxConfigScript::FindExternalFunction(FoxHash hashed_name)
// {
//     for (FoxExternalFunc& func : mExternalFuncs) {
//         if (func.HashedName == hashed_name) {
//             return &func;
//         }
//     }

//     return nullptr;
// }

FoxFunction* FoxScript::FindFunction(Hash32 hashed_name)
{
    FoxScope* scope = mCurrentScope;

    while (scope) {
        FoxFunction* var = scope->FindFunctionInScope(hashed_name);
        if (var) {
            return var;
        }

        scope = scope->Parent;
    }

    return nullptr;
}

void FoxScript::PrintFunctionTable(const FoxScope& scope) const
{
    LogInfo("||-------------------------------------||");
    LogInfo("|| Name             | Hash             ||");
    LogInfo("||-------------------------------------||");

    for (const FoxFunction& function : scope.Functions) {
        LogInfo("|| {:16} | {:16} ||", function.Name->GetStr(), function.Name->GetHash());
    }

    LogInfo("||-------------------------------------||");
}

void FoxScript::Execute()
{
    mTokens.PrintDebugInfo();
    // for (const Token& token : mTokens) {
    //     token.Print();
    // }

    mRootBlock = Parse();

    // If there are errors, exit early
    if (mHasErrors || mRootBlock == nullptr) {
        LogInfo("Execute: Returning early...");
        return;
    }

    printf("\n=====\n");

    PrintFunctionTable(*mCurrentScope);

    FoxBytecodeEmitter bc_emitter;
    bc_emitter.BeginEmitting(mRootBlock);

    printf("\n=====\n");

    FoxBytecodePrinter bc_printer(bc_emitter.mBytecode);
    bc_printer.Print();

    printf("\n=====\n");


    ScriptVM vm;

    vm.Start(std::move(bc_emitter.mBytecode));

    gEnginePool->Free(mpFileData);

    FoxAstDestroyer destroyer(mRootBlock);
}

FoxValue FoxScript::ParseValue()
{
    Token& token = GetToken();
    TT token_type = token.Type;
    FoxValue value;

    if (token_type == TT::Identifier) {
        FoxVar* var = FindVar(token.GetHash());

        if (var) {
            value.Type = FoxValue::REF;

            FoxAstVarRef* var_ref = FX_SCRIPT_ALLOC_NODE(FoxAstVarRef);
            var_ref->pName = var->Name;
            var_ref->Scope = var->Scope;

            value.pValueRef = var_ref;

            EatToken(TT::Identifier);

            return value;
        }
        else {
            // We cannot find the definition for the variable, assume that it is an external variable that will be
            // defined during the interpret stage.

            LogError("ERROR: Undefined reference to variable {}", token);

            // printf("Undefined reference to variable \"%.*s\"! (Hash:%u)\n", token.Length, token.Start,
            // token.GetHash());
            EatToken(TT::Identifier);
        }
    }

    switch (token_type) {
    case TT::Integer:
        EatToken(TT::Integer);
        value.Type = FoxValue::INT;
        value.ValueInt = token.ToInt();
        break;
    case TT::Float:
        EatToken(TT::Float);
        value.Type = FoxValue::FLOAT;
        value.ValueFloat = token.ToFloat();
        break;
    case TT::String:
        EatToken(TT::String);
        value.Type = FoxValue::STRING;
        value.ValueString = token.GetHeapStr();
        break;
    default:;
    }

    return value;
}

#define RETURN_IF_NO_TOKENS(rval_)                                                                                     \
    {                                                                                                                  \
        if (mTokenIndex >= mTokens.Size()) {                                                                           \
            return (rval_);                                                                                            \
        }                                                                                                              \
    }

static bool IsTokenTypeLiteral(eTokenType type)
{
    return (type == eTokenType::Integer || type == eTokenType::Float || type == eTokenType::String);
}

FoxAstNode* FoxScript::ParseRhs()
{
    RETURN_IF_NO_TOKENS(nullptr);

    bool has_parameters = false;

    if (mTokenIndex + 1 < mTokens.Size()) {
        TT next_token_type = GetToken(1).Type;
        has_parameters = next_token_type == TT::LParen;

        if (mInCommandMode) {
            has_parameters = next_token_type == TT::Identifier || next_token_type == TT::Integer ||
                             next_token_type == TT::Float || next_token_type == TT::String;
        }
    }

    Token& token = GetToken();

    FoxAstNode* lhs = nullptr;

    if (token.Type == TT::Identifier) {
        if (has_parameters) {
            lhs = ParseFunctionCall();
        }
        else {
            // FoxExternalFunc* external_function = FindExternalFunction(token.GetHash());
            // if (external_function != nullptr) {
            //     lhs = ParseFunctionCall();
            // }

            FoxFunction* function = FindFunction(token.GetHash());
            if (function != nullptr) {
                lhs = ParseFunctionCall();
            }
        }
    }
    if (!lhs) {
        if (IsTokenTypeLiteral(token.Type) || token.Type == TT::Identifier) {
            FoxAstLiteral* literal = FX_SCRIPT_ALLOC_NODE(FoxAstLiteral);

            FoxValue value = ParseValue();
            literal->Value = value;

            lhs = literal;
        }
        else {
            lhs = ParseRhs();
        }
    }


    // FoxAstLiteral* literal = FX_SCRIPT_ALLOC_NODE(FoxAstLiteral);
    // literal->Value = value;

    TT op_type = GetToken(0).Type;
    if (op_type == TT::Plus || op_type == TT::Minus) {
        FoxAstBinop* binop = FX_SCRIPT_ALLOC_NODE(FoxAstBinop);

        binop->pLeft = lhs;
        binop->OpToken = &EatToken(op_type);
        binop->pRight = ParseRhs();

        return binop;
    }

    return lhs;
}

FoxAstAssign* FoxScript::TryParseAssignment(Token* var_name)
{
    if (GetToken().Type != TT::Equals) {
        return nullptr;
    }

    EatToken(TT::Equals);

    FoxAstAssign* node = FX_SCRIPT_ALLOC_NODE(FoxAstAssign);

    FoxAstVarRef* var_ref = FX_SCRIPT_ALLOC_NODE(FoxAstVarRef);
    var_ref->pName = var_name;
    var_ref->Scope = mCurrentScope;
    node->Var = var_ref;

    // node->Value = ParseValue();
    node->Rhs = ParseRhs();

    return node;
}

void FoxScript::DefineExternalVar(const char* type, const char* name, const FoxValue& value)
{
    Token* name_token = FX_SCRIPT_ALLOC_MEMORY(Token, sizeof(Token));
    Token* type_token = FX_SCRIPT_ALLOC_MEMORY(Token, sizeof(Token));

    {
        const uint32 type_len = strlen(type);

        char* type_buffer = FX_SCRIPT_ALLOC_MEMORY(char, (type_len + 1));
        std::strcpy(type_buffer, type);

        type_token->Start = type_buffer;
        type_token->Type = TT::Identifier;
        type_token->Start[type_len] = 0;
        type_token->Length = type_len + 1;
    }

    {
        const uint32 name_len = strlen(name);

        char* name_buffer = FX_SCRIPT_ALLOC_MEMORY(char, (name_len + 1));
        std::strcpy(name_buffer, name);

        name_token->Start = name_buffer;
        name_token->Type = TT::Identifier;
        name_token->Start[name_len] = 0;
        name_token->Length = name_len + 1;
    }

    FoxScope* definition_scope = &mScopes[0];

    FoxVar var(type_token, name_token, definition_scope, true);
    var.Value = value;

    definition_scope->Vars.Insert(var);

    // To prevent the variable data from being deleted here.
    var.Name = nullptr;
    var.Type = nullptr;
}

FoxAstNode* FoxScript::ParseStatementAsCommand(FoxAstBlock* parent_block)
{
    if (mHasErrors) {
        return nullptr;
    }

    RETURN_IF_NO_TOKENS(nullptr);

    // Eat any extraneous semicolons
    while (GetToken().Type == TT::Semicolon) {
        EatToken(TT::Semicolon);
        if (mTokenIndex >= mTokens.Size()) {
            return nullptr;
        }
    }

    // Try to parse as a keyword first
    FoxAstNode* node = TryParseKeyword(parent_block);

    if (node) {
        RETURN_IF_NO_TOKENS(node);

        EatToken(TT::Semicolon);
        return node;
    }

    // Check identifier
    if (mTokenIndex < mTokens.Size() && GetToken().Type == TT::Identifier) {
        TT next_token_type = TT::Unknown;

        if (mTokenIndex + 1 < mTokens.Size()) {
            next_token_type = GetToken(1).Type;
        }

        if (mTokenIndex + 1 < mTokens.Size() && GetToken(1).Type == TT::Equals) {
            Token& assign_name = EatToken(TT::Identifier);
            node = TryParseAssignment(&assign_name);
        }
        // If there is what looks to be a function call, try it
        else {
            // FoxExternalFunc* external_function = FindExternalFunction(GetToken().GetHash());
            // if (external_function != nullptr) {
            //     node = ParseFunctionCall();
            // }
            // else {
            FoxFunction* function = FindFunction(GetToken().GetHash());
            if (function != nullptr) {
                node = ParseFunctionCall();
            }
            // }
        }
        // If there is just a semicolon after an identifier, parse as an rhs to print out later
        if (!node && next_token_type == TT::Semicolon) {
            node = ParseRhs();
        }
    }

    RETURN_IF_NO_TOKENS(node);

    EatToken(TT::Semicolon);

    return node;
}

FoxAstNode* FoxScript::ParseStatement(FoxAstBlock* parent_block)
{
    if (mHasErrors) {
        return nullptr;
    }

    RETURN_IF_NO_TOKENS(nullptr);

    LogInfo("PARSE STMT {}", mTokens.Size());

    if (GetToken().Type == TT::Dollar) {
        EatToken(TT::Dollar);

        mInCommandMode = true;

        FoxAstCommandMode* cmd_mode = FX_SCRIPT_ALLOC_NODE(FoxAstCommandMode);
        cmd_mode->Node = ParseStatementAsCommand(parent_block);

        mInCommandMode = false;

        return cmd_mode;
    }

    while (GetToken().Type == TT::DocComment) {
        FoxAstDocComment* comment = FX_SCRIPT_ALLOC_NODE(FoxAstDocComment);
        comment->Comment = &EatToken(TT::DocComment);
        CurrentDocComments.push_back(comment);

        if (mTokenIndex >= mTokens.Size()) {
            return nullptr;
        }
    }

    // Eat any extraneous semicolons
    while (GetToken().Type == TT::Semicolon) {
        EatToken(TT::Semicolon);

        if (mTokenIndex >= mTokens.Size()) {
            return nullptr;
        }
    }

    FoxAstNode* node = TryParseKeyword(parent_block);

    if (!node && (mTokenIndex < mTokens.Size() && GetToken().Type == TT::Identifier)) {
        if (mTokenIndex + 1 < mTokens.Size() && GetToken(1).Type == TT::LParen) {
            node = ParseFunctionCall();
        }
        else if (mTokenIndex + 1 < mTokens.Size() && GetToken(1).Type == TT::Equals) {
            Token& assign_name = EatToken(TT::Identifier);
            node = TryParseAssignment(&assign_name);
        }
        else {
            GetToken().Print();
        }
    }


    if (!node) {
        return nullptr;
    }

    // Blocks do not require semicolons
    if (node->NodeType == FX_AST_BLOCK || node->NodeType == FX_AST_PROCDECL) {
        return node;
    }

    EatToken(TT::Semicolon);

    return node;
}

FoxAstBlock* FoxScript::ParseBlock()
{
    bool in_command = mInCommandMode;
    mInCommandMode = false;

    FoxAstBlock* block = FX_SCRIPT_ALLOC_NODE(FoxAstBlock);

    EatToken(TT::LBrace);

    while (GetToken().Type != TT::RBrace) {
        FoxAstNode* command = ParseStatement(block);
        if (command == nullptr) {
            break;
        }
        block->Statements.push_back(command);
    }

    EatToken(TT::RBrace);

    mInCommandMode = in_command;
    return block;
}

FoxAstFunctionDecl* FoxScript::ParseProcedureDeclare()
{
    FoxAstFunctionDecl* node = FX_SCRIPT_ALLOC_NODE(FoxAstFunctionDecl);

    if (!CurrentDocComments.empty()) {
        node->DocComments = CurrentDocComments;
        CurrentDocComments.clear();
    }

    // Name of the function
    Token& name = EatToken(TT::Identifier);

    node->Name = &name;

    PushScope();
    EatToken(TT::LParen);

    FoxAstBlock* params = FX_SCRIPT_ALLOC_NODE(FoxAstBlock);

    // Parse the parameter list
    while (GetToken().Type != TT::RParen) {
        params->Statements.push_back(ParseVarDeclare());

        if (GetToken().Type == TT::Comma) {
            EatToken(TT::Comma);
            continue;
        }

        break;
    }

    EatToken(TT::RParen);

    // Parse the return type
    /*if (GetToken().Type != TT::LBrace) {
        FoxAstVarDecl* return_decl = ParseVarDeclare();
        node->ReturnVar = return_decl;
    }*/

    // Check to see if there is a return type provided
    if (GetToken().Type != TT::LBrace) {
        // There is a return type, declare the __ReturnVal__ variable

        // Get the token for the type
        Token& type_token = EatToken(TT::Identifier);

        FoxAstVarDecl* return_decl = InternalVarDeclare(mTokenReturnVar, &type_token);
        node->pReturnVar = return_decl;
    }


    node->Block = ParseBlock();
    PopScope();

    node->Params = params;

    FoxFunction function(&name, mCurrentScope, node->Block, node);
    mCurrentScope->Functions.Insert(function);

    return node;
}

FoxIRRegister IrRegisterFromToken(const Token& token)
{
    if (token.Type != eTokenType::Identifier) {
        return FX_IR_NONE;
    }

    std::string reg = token.GetStr();

    if (reg.starts_with("PARAMREG")) {
        char num = reg[reg.length()];
        switch (num) {
        case '0':
            return FX_IR_PARAMREG0;
        case '1':
            return FX_IR_PARAMREG1;
        case '2':
            return FX_IR_PARAMREG2;
        case '3':
            return FX_IR_PARAMREG3;
        }
    }

    if (reg.starts_with("GW")) {
        char num = reg[reg.length()];
        if ((num - '0') <= 7) {
            return static_cast<FoxIRRegister>(FX_IR_GW0 + (num - '0'));
        }
    }

    if (reg.starts_with("GX")) {
        char num = reg[reg.length()];
        if ((num - '0') <= 3) {
            return static_cast<FoxIRRegister>(FX_IR_GX0 + (num - '0'));
        }
    }

    return FX_IR_NONE;
}

FoxAstFunctionDecl* FoxScript::ParseExtfnDeclare()
{
    FoxAstFunctionDecl* node = FX_SCRIPT_ALLOC_NODE(FoxAstFunctionDecl);

    if (!CurrentDocComments.empty()) {
        node->DocComments = CurrentDocComments;
        CurrentDocComments.clear();
    }

    // Name of the function
    Token& name = EatToken(TT::Identifier);

    node->Name = &name;

    PushScope();
    EatToken(TT::LParen);

    FoxAstBlock* params = FX_SCRIPT_ALLOC_NODE(FoxAstBlock);

    // Parse the parameter list
    while (GetToken().Type != TT::RParen) {
        params->Statements.push_back(ParseVarDeclare());

        if (GetToken().Type == TT::Comma) {
            EatToken(TT::Comma);
            continue;
        }

        break;
    }

    EatToken(TT::RParen);

    // Parse the return type
    /*if (GetToken().Type != TT::LBrace) {
        FoxAstVarDecl* return_decl = ParseVarDeclare();
        node->ReturnVar = return_decl;
    }*/


    // Parse clobber list

    constexpr Hash32 clobber_hash = HashStr32("CLOBBERS");

    // Check to see if there is a return type provided
    if (GetToken().Type != TT::Semicolon && GetToken().GetHash() != clobber_hash) {
        // There is a return type, declare the __ReturnVal__ variable

        // Get the token for the type
        Token& type_token = EatToken(TT::Identifier);

        FoxAstVarDecl* return_decl = InternalVarDeclare(mTokenReturnVar, &type_token);
        node->pReturnVar = return_decl;
    }

    // Check if there is a clobber list provided
    if (GetToken().Type == TT::Identifier && GetToken().GetHash() == clobber_hash) {
        EatToken(TT::Identifier);


        while (1) {
            FoxIRRegister reg = IrRegisterFromToken(EatToken(TT::Identifier));
            node->ClobberList.push_back(reg);

            if (GetToken().Type == TT::Comma) {
                EatToken(TT::Comma);
                continue;
            }

            break;
        }
    }

    PopScope();

    node->Params = params;

    FoxFunction function(&name, mCurrentScope, nullptr, node);
    mCurrentScope->Functions.Insert(function);

    return node;
}

// FoxValue FoxConfigScript::TryCallInternalFunc(FoxHash func_name, std::vector<FoxValue>& params)
//{
//     FoxValue return_value;
//
//     for (const FoxInternalFunc& func : mInternalFuncs) {
//         if (func.HashedName == func_name) {
//             func.Func(params, &return_value);
//             return return_value;
//         }
//     }
//
//     return return_value;
// }

FoxAstFunctionCall* FoxScript::ParseFunctionCall()
{
    FoxAstFunctionCall* node = FX_SCRIPT_ALLOC_NODE(FoxAstFunctionCall);

    Token& name = EatToken(TT::Identifier);

    node->HashedName = name.GetHash();
    node->pFunction = FindFunction(node->HashedName);

    TT end_token_type = TT::Semicolon;

    if (!mInCommandMode) {
        end_token_type = TT::RParen;
    }

    if (!mInCommandMode || GetToken().Type == TT::LParen) {
        EatToken(TT::LParen);
    }

    while (GetToken().Type != end_token_type) {
        FoxAstNode* param = ParseRhs();

        if (param == nullptr) {
            break;
        }

        node->Params.push_back(param);

        TT next_tt = GetToken().Type;

        if (GetToken().Type == TT::Comma) {
            EatToken(TT::Comma);
            continue;
        }

        if (mInCommandMode && (next_tt != TT::RParen && next_tt != TT::Semicolon)) {
            continue;
        }


        break;
    }

    if (!mInCommandMode || GetToken().Type == TT::RParen) {
        EatToken(TT::RParen);
    }

    return node;
}

FoxAstBlock* FoxScript::Parse()
{
    FoxAstBlock* root_block = FX_SCRIPT_ALLOC_NODE(FoxAstBlock);

    FoxAstNode* keyword;
    while ((keyword = ParseStatement(root_block))) {
        root_block->Statements.push_back(keyword);
    }

    /*for (const auto& var : mCurrentScope->Vars) {
        var.Print();
    }*/

    if (mHasErrors) {
        return nullptr;
    }

    FoxAstPrinter printer(root_block);
    printer.Print(root_block);

    return root_block;
}

void FoxAstDestroyer::Do(FoxAstNode* node)
{
    if (node == nullptr) {
        return;
    }

    if (node->NodeType == FX_AST_BLOCK) {
        FoxAstBlock* block = reinterpret_cast<FoxAstBlock*>(node);
        for (FoxAstNode* child : block->Statements) {
            Do(child);
        }

        FX_SCRIPT_FREE(FoxAstBlock, block);
    }
    else if (node->NodeType == FX_AST_PROCDECL) {
        FoxAstFunctionDecl* functiondecl = reinterpret_cast<FoxAstFunctionDecl*>(node);

        for (FoxAstNode* param : functiondecl->Params->Statements) {
            Do(param);
        }

        Do(functiondecl->Block);

        FX_SCRIPT_FREE(FoxAstFunctionDecl, functiondecl);
    }
    else if (node->NodeType == FX_AST_VARDECL) {
        FoxAstVarDecl* vardecl = reinterpret_cast<FoxAstVarDecl*>(node);

        Do(vardecl->Assignment);

        FX_SCRIPT_FREE(FoxAstVarDecl, vardecl);
    }
    else if (node->NodeType == FX_AST_ASSIGN) {
        FoxAstAssign* assign = reinterpret_cast<FoxAstAssign*>(node);

        Do(assign->Rhs);

        FX_SCRIPT_FREE(FoxAstAssign, assign);
    }
    else if (node->NodeType == FX_AST_PROCCALL) {
        FoxAstFunctionCall* functioncall = reinterpret_cast<FoxAstFunctionCall*>(node);

        FX_SCRIPT_FREE(FoxAstFunctionCall, functioncall);
    }
    else if (node->NodeType == FX_AST_LITERAL) {
        FoxAstLiteral* literal = reinterpret_cast<FoxAstLiteral*>(node);

        FX_SCRIPT_FREE(FoxAstLiteral, literal);
    }
    else if (node->NodeType == FX_AST_BINOP) {
        FoxAstBinop* binop = reinterpret_cast<FoxAstBinop*>(node);

        Do(binop->pLeft);
        Do(binop->pRight);

        FX_SCRIPT_FREE(FoxAstBinop, binop);
    }
    else if (node->NodeType == FX_AST_COMMANDMODE) {
        FoxAstCommandMode* command_mode = reinterpret_cast<FoxAstCommandMode*>(node);

        Do(command_mode->Node);

        FX_SCRIPT_FREE(FoxAstCommandMode, command_mode);
    }
    else if (node->NodeType == FX_AST_RETURN) {
        FoxAstReturn* return_node = reinterpret_cast<FoxAstReturn*>(node);

        if (return_node->pRhs) {
            Do(return_node->pRhs);
        }

        FX_SCRIPT_FREE(FoxAstReturn, return_node);
    }
    else {
        LogError("Cannot free unknown node!");
    }
}

} // namespace fx::script
