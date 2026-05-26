#include "FoxParser.hpp"

#include "FoxBytecodeCompiler.hpp"
// #include "FoxCGArm64.hpp"
#include "FoxVM.hpp"

#include <Core/Defer.hpp>
#include <Core/File.hpp>
#include <Core/Log.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Engine.hpp>
#include <Util/Tokenizer.hpp>

namespace fx::script {

#define FX_SCRIPT_ALLOC_NODE(type_)          gScriptMemPool->Alloc<type_>(sizeof(type_))
#define FX_SCRIPT_ALLOC_MEMORY(type_, size_) gScriptMemPool->Alloc<type_>(size_)
#define FX_SCRIPT_FREE(type_, ptr_)          gScriptMemPool->Free<type_>(ptr_);

void FoxParser::Init(PagedArray<Token>&& tokens)
{
    mTokens = std::move(tokens);

    mScopes.Create(8);
    mCurrentScope = mScopes.Insert();
    mCurrentScope->Vars.Create(FX_SCRIPT_SCOPE_GLOBAL_VARS_START_SIZE);
    mCurrentScope->Functions.Create(FX_SCRIPT_SCOPE_GLOBAL_ACTIONS_START_SIZE);
}

Token& FoxParser::GetToken(int offset)
{
    const uint32 idx = mTokenIndex + offset;
    assert(idx >= 0 && idx <= mTokens.Size());
    return mTokens[idx];
}

Token& FoxParser::EatToken(TT token_type)
{
    Token& token = GetToken();
    if (token.Type != token_type) {
        ParseError("{}:{}: Unexpected token type {} when expecting {}!", token.FileLine, token.FileColumn,
                   Token::GetTypeName(token.Type), Token::GetTypeName(token_type));
    }
    ++mTokenIndex;
    return token;
}

Token* FoxParser::CreateTokenFromString(eTokenType type, const char* text)
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

FoxAstNode* FoxParser::TryParseKeyword(FoxAstBlock* parent_block, bool* ignore_semicolon)
{
    if (mTokenIndex >= mTokens.Size()) {
        return nullptr;
    }

    Token& tk = GetToken();
    Hash32 hash = tk.GetHash();

    bool discard;
    if (ignore_semicolon == nullptr) {
        ignore_semicolon = &discard;
    }

    // proc? [name] ( < [arg type] [arg name] ...> ) [return type]? { <statements...> }
    constexpr Hash32 kw_proc = HashStr32("proc");

    constexpr Hash32 kw_extfn = HashStr32("extproc");

    // local [type] [name] <?assignment> ;
    constexpr Hash32 kw_local = HashStr32("local");

    // global [type] [name] <?assignment> ;
    constexpr Hash32 kw_global = HashStr32("global");

    // return ;
    constexpr Hash32 kw_return = HashStr32("return");

    // help [name of function] ;
    constexpr Hash32 kw_help = HashStr32("help");

    constexpr Hash32 kw_if = HashStr32("if");

    switch (hash) {
    case kw_proc:
        EatToken(TT::Identifier);
        // ParseFunctionDeclare();
        return ParseFunctionDeclare();

    case kw_extfn:
        EatToken(TT::Identifier);
        // ParseFunctionDeclare();
        return ParseExtfnDeclare();

    case kw_local:
        EatToken(TT::Identifier);
        return ParseVarDeclare();

    case kw_global:
        EatToken(TT::Identifier);
        return ParseVarDeclare(&mScopes[0]);

    case kw_if:
        EatToken(TT::Identifier);
        (*ignore_semicolon) = true;
        return ParseIfStatement(parent_block);

    case kw_return: {
        EatToken(TT::Identifier);

        FoxAstNode* return_rhs = nullptr;

        if (GetToken().Type != TT::Semicolon) {
            // There is a value that follows, get the value
            return_rhs = ParseRhs();
        }

        FoxAstReturn* ret = FX_SCRIPT_ALLOC_NODE(FoxAstReturn);
        ret->pRhs = return_rhs;
        parent_block->bHasExplicitReturn = true;

        return ret;
    }
    }

    return nullptr;
}

void FoxParser::PushScope()
{
    FoxScope* current = mCurrentScope;

    FoxScope* new_scope = mScopes.Insert();
    new_scope->Parent = current;
    new_scope->Vars.Create(FX_SCRIPT_SCOPE_LOCAL_VARS_START_SIZE);
    new_scope->Functions.Create(FX_SCRIPT_SCOPE_LOCAL_ACTIONS_START_SIZE);

    mCurrentScope = new_scope;
}

void FoxParser::PopScope()
{
    FoxScope* new_scope = mCurrentScope->Parent;
    mScopes.RemoveLast();

    assert(new_scope == &mScopes.GetLast());

    mCurrentScope = new_scope;
}

FoxAstVarDecl* FoxParser::InternalVarDeclare(Token* name_token, Token* type_token, FoxScope* scope)
{
    if (scope == nullptr) {
        scope = mCurrentScope;
    }

    // Allocate the declaration node
    FoxAstVarDecl* node = FX_SCRIPT_ALLOC_NODE(FoxAstVarDecl);

    node->pNameToken = name_token;
    node->pTypeToken = type_token;
    node->Type = FoxStringToType(type_token);

    node->bDefineAsGlobal = (scope == &mScopes[0]);

    // Push the variable to the scope
    FoxVar var { name_token, type_token, scope };
    scope->Vars.Insert(var);

    return node;
}

FoxAstVarDecl* FoxParser::ParseVarDeclare(FoxScope* scope)
{
    if (scope == nullptr) {
        scope = mCurrentScope;
    }

    Token& type = EatToken(TT::Identifier);
    Token& name = EatToken(TT::Identifier);

    FoxAstVarDecl* node = FX_SCRIPT_ALLOC_NODE(FoxAstVarDecl);

    node->pNameToken = &name;
    node->pTypeToken = &type;
    node->Type = FoxStringToType(node->pTypeToken);

    node->bDefineAsGlobal = (scope == &mScopes[0]);

    FoxVar var { &type, &name, scope };

    node->pAssignment = TryParseAssignment(node->pNameToken);
    /*if (node->Assignment) {
        var.Value = node->Assignment->Value;
    }*/
    scope->Vars.Insert(var);

    return node;
}


FoxVar* FoxParser::FindVar(Hash32 hashed_name)
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

FoxFunction* FoxParser::FindFunction(Hash32 hashed_name)
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

void FoxParser::PrintFunctionTable(const FoxScope& scope) const
{
    LogInfo("||-------------------------------------||");
    LogInfo("|| Name             | Hash             ||");
    LogInfo("||-------------------------------------||");

    for (const FoxFunction& function : scope.Functions) {
        LogInfo("|| {:16} | {:16} ||", function.Name->GetStr(), function.Name->GetHash());
    }

    LogInfo("||-------------------------------------||");
}


FoxValue FoxParser::ParseValue()
{
    Token& token = GetToken();
    TT token_type = token.Type;
    FoxValue value;

    if (token_type == TT::Identifier) {
        FoxVar* var = FindVar(token.GetHash());

        if (var) {
            value.Type = eFoxType::REF;

            FoxAstVarRef* var_ref = FX_SCRIPT_ALLOC_NODE(FoxAstVarRef);
            var_ref->pName = var->Name;
            var_ref->pScope = var->Scope;

            value.pValueRef = var_ref;

            EatToken(TT::Identifier);

            return value;
        }
        else {
            // We cannot find the definition for the variable, assume that it is an external variable that will be
            // defined during the interpret stage.

            ParseError("Undefined reference to variable {}", token);

            // printf("Undefined reference to variable \"%.*s\"! (Hash:%u)\n", token.Length, token.Start,
            // token.GetHash());
            EatToken(TT::Identifier);
        }
    }

    switch (token_type) {
    case TT::Integer:
        EatToken(TT::Integer);
        value.Type = eFoxType::INT;
        value.ValueInt = token.ToInt();
        break;
    case TT::Float:
        EatToken(TT::Float);
        value.Type = eFoxType::FLOAT;
        value.ValueFloat = token.ToFloat();
        break;
    case TT::String:
        EatToken(TT::String);
        value.Type = eFoxType::STRING;
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

FoxAstNode* FoxParser::ParseAddExpr()
{
    FoxAstNode* lhs = ParseMulExpr();
    Token& token = GetToken(0);

    while (GetToken(0).Type == eTokenType::Plus || GetToken(0).Type == eTokenType::Minus) {
        FoxAstBinop* binop = FX_SCRIPT_ALLOC_NODE(FoxAstBinop);
        binop->OpToken = &EatToken(GetToken(0).Type);
        binop->pLeft = lhs;
        binop->pRight = ParseMulExpr();

        lhs = binop;
    }

    return lhs;
}


FoxAstNode* FoxParser::ParseMulExpr()
{
    FoxAstNode* lhs = ParseTerm();
    Token& token = GetToken(0);

    while (GetToken(0).Type == eTokenType::Asterisk) {
        FoxAstBinop* binop = FX_SCRIPT_ALLOC_NODE(FoxAstBinop);
        binop->OpToken = &EatToken(eTokenType::Asterisk);
        binop->pLeft = lhs;
        binop->pRight = ParseTerm();

        lhs = binop;
    }

    return lhs;
}

FoxAstNode* FoxParser::ParseTerm()
{
    FoxAstNode* lhs = nullptr;
    Token& token = GetToken(0);

    if (IsTokenTypeLiteral(token.Type) || token.Type == TT::Identifier) {
        bool has_paramlist = false;

        if (mTokenIndex + 1 < mTokens.Size()) {
            TT next_token_type = GetToken(1).Type;
            has_paramlist = next_token_type == TT::LParen;
        }

        if (token.Type == TT::Identifier && has_paramlist) {
            // If there is a lparen after this, we can assume that this is a function call
            return ParseFunctionCall();
        }

        FoxAstLiteral* literal = FX_SCRIPT_ALLOC_NODE(FoxAstLiteral);

        FoxValue value = ParseValue();
        literal->Value = value;

        return literal;
    }

    return ParseRhs();
}

FoxAstNode* FoxParser::ParseRhs()
{
    RETURN_IF_NO_TOKENS(nullptr);

    bool has_paramlist = false;

    if (mTokenIndex + 1 < mTokens.Size()) {
        TT next_token_type = GetToken(1).Type;
        has_paramlist = next_token_type == TT::LParen;
    }

    Token& token = GetToken();

    FoxAstNode* lhs = nullptr;

    if (token.Type == TT::Identifier) {
        // If there is a lparen after this, we can assume that this is a function call
        if (has_paramlist) {
            lhs = ParseFunctionCall();
        }
        else {
            FoxFunction* function = FindFunction(token.GetHash());
            if (function != nullptr) {
                lhs = ParseFunctionCall();
            }
        }
    }

    if (!lhs) {
        TT op_type = GetToken(1).Type;

        if (Token::IsTypeEqualityCheck(op_type)) {
            FoxAstBinop* binop = FX_SCRIPT_ALLOC_NODE(FoxAstBinop);

            binop->pLeft = ParseTerm();
            binop->OpToken = &EatToken(op_type);
            binop->pRight = ParseRhs();

            return binop;
        }

        return ParseAddExpr();
    }


    return lhs;
}

FoxAstAssign* FoxParser::TryParseAssignment(Token* var_name)
{
    if (GetToken().Type != TT::Equals) {
        return nullptr;
    }

    EatToken(TT::Equals);

    FoxAstAssign* node = FX_SCRIPT_ALLOC_NODE(FoxAstAssign);

    FoxAstVarRef* var_ref = FX_SCRIPT_ALLOC_NODE(FoxAstVarRef);
    var_ref->pName = var_name;
    var_ref->pScope = mCurrentScope;
    node->pLhs = var_ref;


    // node->Value = ParseValue();
    node->pRhs = ParseRhs();

    return node;
}

void FoxParser::DefineExternalVar(const char* type, const char* name, const FoxValue& value)
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


FoxAstNode* FoxParser::ParseGlobalDefinitions()
{
    if (GetToken().Type == TT::Identifier && GetToken(1).Type == TT::LParen) {
        return ParseFunctionDeclare();
    }

    return nullptr;
}

FoxAstNode* FoxParser::ParseStatement(FoxAstBlock* parent_block)
{
    if (bHasErrors) {
        return nullptr;
    }

    RETURN_IF_NO_TOKENS(nullptr);

    if (GetToken().Type == TT::LBrace) {
        return ParseBlock();
    }

    // Eat any extraneous semicolons
    while (GetToken().Type == TT::Semicolon) {
        EatToken(TT::Semicolon);

        if (mTokenIndex >= mTokens.Size()) {
            return nullptr;
        }
    }

    if (IsGlobalScope()) {
        FoxAstNode* global_def = ParseGlobalDefinitions();
        if (global_def != nullptr) {
            return global_def;
        }
    }

    bool ignore_semicolon = false;
    FoxAstNode* node = TryParseKeyword(parent_block, &ignore_semicolon);

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


    if (!ignore_semicolon || GetToken().Type == TT::Semicolon) {
        EatToken(TT::Semicolon);
    }

    return node;
}

FoxAstBlock* FoxParser::ParseBlock()
{
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

    return block;
}

FoxAstFunctionDecl* FoxParser::ParseFunctionDeclare()
{
    FoxAstFunctionDecl* node = FX_SCRIPT_ALLOC_NODE(FoxAstFunctionDecl);

    // Name of the function
    Token& name = EatToken(TT::Identifier);

    node->pNameToken = &name;

    PushScope();
    EatToken(TT::LParen);

    FoxAstBlock* params = FX_SCRIPT_ALLOC_NODE(FoxAstBlock);

    // Parse the parameter list
    while (GetToken().Type != TT::RParen) {
        // Parse variadic arg
        if (GetToken().Type == TT::Dot && GetToken(1).Type == TT::Dot && GetToken(2).Type == TT::Dot) {
            node->bIsVariadic = true;

            EatToken(TT::Dot);
            EatToken(TT::Dot);
            EatToken(TT::Dot);

            break;
        }

        params->Statements.push_back(ParseVarDeclare());

        if (GetToken().Type == TT::Comma) {
            EatToken(TT::Comma);
            continue;
        }

        break;
    }

    EatToken(TT::RParen);

    // Check to see if there is a return type provided
    if (GetToken().Type != TT::LBrace) {
        node->ReturnType = FoxStringToType(&EatToken(TT::Identifier));

        // FoxAstVarDecl* return_decl = InternalVarDeclare(mTokenReturnVar, &type_token);
        // node->pReturnVar = return_decl;
    }


    node->pBlock = ParseBlock();
    PopScope();

    node->pParams = params;

    FoxFunction function(&name, mCurrentScope, node->pBlock, node);
    mCurrentScope->Functions.Insert(function);

    return node;
}

FoxAstFunctionDecl* FoxParser::ParseExtfnDeclare()
{
    FoxAstFunctionDecl* node = FX_SCRIPT_ALLOC_NODE(FoxAstFunctionDecl);

    // Name of the function
    Token& name = EatToken(TT::Identifier);

    node->pNameToken = &name;

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
    if (GetToken().Type != TT::Semicolon) {
        node->ReturnType = FoxStringToType(&EatToken(TT::Identifier));
    }

    PopScope();

    node->pParams = params;

    FoxFunction function(&name, mCurrentScope, nullptr, node);
    mCurrentScope->Functions.Insert(function);

    node->bIsExternal = true;

    return node;
}

FoxAstFunctionCall* FoxParser::ParseFunctionCall()
{
    FoxAstFunctionCall* node = FX_SCRIPT_ALLOC_NODE(FoxAstFunctionCall);

    Token& name = EatToken(TT::Identifier);

    node->HashedName = name.GetHash();
    node->pFunction = FindFunction(node->HashedName);

    if (node->pFunction == nullptr) {
        mFunctionFixups.emplace_back(node, node->HashedName);
    }

    EatToken(TT::LParen);

    while (GetToken().Type != TT::RParen) {
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

        break;
    }

    EatToken(TT::RParen);

    return node;
}

FoxAstIf* FoxParser::ParseIfStatement(FoxAstBlock* parent_block)
{
    // if ( [CONDITION] ) [BLOCK]

    FoxAstIf* if_node = FX_SCRIPT_ALLOC_NODE(FoxAstIf);

    EatToken(TT::LParen);
    if_node->pCondition = ParseRhs();
    EatToken(TT::RParen);

    if_node->pBlock = ParseStatement(parent_block);

    static constexpr Hash32 kw_else = HashStr32("else");

    if (GetToken().GetHash() == kw_else) {
        EatToken(TT::Identifier);
        if_node->pElseBlock = ParseStatement(parent_block);
    }

    return if_node;
}

void FoxParser::FixupFunctionCalls()
{
    for (const FoxFunctionFixup& fix : mFunctionFixups) {
        fix.pCall->pFunction = FindFunction(fix.FunctionName);
    }
}

FoxAstBlock* FoxParser::Parse()
{
    FoxAstBlock* root_block = FX_SCRIPT_ALLOC_NODE(FoxAstBlock);

    FoxAstNode* keyword;
    while ((keyword = ParseStatement(root_block))) {
        root_block->Statements.push_back(keyword);
    }

    FixupFunctionCalls();

    if (bHasErrors) {
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
        FoxAstBlock* block = static_cast<FoxAstBlock*>(node);
        for (FoxAstNode* child : block->Statements) {
            Do(child);
        }

        FX_SCRIPT_FREE(FoxAstBlock, block);
    }
    else if (node->NodeType == FX_AST_PROCDECL) {
        FoxAstFunctionDecl* functiondecl = static_cast<FoxAstFunctionDecl*>(node);

        for (FoxAstNode* param : functiondecl->pParams->Statements) {
            Do(param);
        }

        Do(functiondecl->pBlock);

        FX_SCRIPT_FREE(FoxAstFunctionDecl, functiondecl);
    }
    else if (node->NodeType == FX_AST_VARDECL) {
        FoxAstVarDecl* vardecl = static_cast<FoxAstVarDecl*>(node);

        Do(vardecl->pAssignment);

        FX_SCRIPT_FREE(FoxAstVarDecl, vardecl);
    }
    else if (node->NodeType == FX_AST_ASSIGN) {
        FoxAstAssign* assign = static_cast<FoxAstAssign*>(node);

        Do(assign->pRhs);

        FX_SCRIPT_FREE(FoxAstAssign, assign);
    }
    else if (node->NodeType == FX_AST_PROCCALL) {
        FoxAstFunctionCall* functioncall = static_cast<FoxAstFunctionCall*>(node);

        FX_SCRIPT_FREE(FoxAstFunctionCall, functioncall);
    }
    else if (node->NodeType == FX_AST_LITERAL) {
        FoxAstLiteral* literal = static_cast<FoxAstLiteral*>(node);

        FX_SCRIPT_FREE(FoxAstLiteral, literal);
    }
    else if (node->NodeType == FX_AST_BINOP) {
        FoxAstBinop* binop = static_cast<FoxAstBinop*>(node);

        Do(binop->pLeft);
        Do(binop->pRight);

        FX_SCRIPT_FREE(FoxAstBinop, binop);
    }
    else if (node->NodeType == FX_AST_RETURN) {
        FoxAstReturn* return_node = static_cast<FoxAstReturn*>(node);

        if (return_node->pRhs) {
            Do(return_node->pRhs);
        }

        FX_SCRIPT_FREE(FoxAstReturn, return_node);
    }

    else if (node->NodeType == FX_AST_IF) {
        FoxAstIf* if_node = static_cast<FoxAstIf*>(node);

        Do(if_node->pCondition);
        Do(if_node->pBlock);
        Do(if_node->pElseBlock);

        FX_SCRIPT_FREE(FoxAstIf, if_node);
    }
    else {
        LogError(LC_SCRIPT, "Cannot free unknown node!");
    }
}

} // namespace fx::script
