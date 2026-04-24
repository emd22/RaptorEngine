#pragma once

#include "FoxValue.hpp"

#include <Core/Types.hpp>
#include <Util/Tokenizer.hpp>

namespace fx {


namespace script {

struct FoxScope;
struct FoxFunction;

enum FoxAstType
{
    FX_AST_LITERAL,
    // FX_AST_NAME,

    FX_AST_BINOP,
    FX_AST_UNARYOP,
    FX_AST_BLOCK,

    // Variables
    FX_AST_VARREF,
    FX_AST_VARDECL,
    FX_AST_ASSIGN,

    // Functions
    FX_AST_PROCDECL,
    FX_AST_PROCCALL,
    FX_AST_RETURN,

    FX_AST_DOCCOMMENT,

    FX_AST_COMMANDMODE,
};

struct FoxAstNode
{
    FoxAstType NodeType;
};


struct FoxAstLiteral : public FoxAstNode
{
    FoxAstLiteral() { this->NodeType = FX_AST_LITERAL; }

    // FoxTokenizer::Token* Token = nullptr;
    FoxValue Value;
};

struct FoxAstBinop : public FoxAstNode
{
    FoxAstBinop() { this->NodeType = FX_AST_BINOP; }

    Token* OpToken = nullptr;
    FoxAstNode* pLeft = nullptr;
    FoxAstNode* pRight = nullptr;
};

struct FoxAstBlock : public FoxAstNode
{
    FoxAstBlock() { this->NodeType = FX_AST_BLOCK; }

    std::vector<FoxAstNode*> Statements;
};

struct FoxAstVarRef : public FoxAstNode
{
    FoxAstVarRef() { this->NodeType = FX_AST_VARREF; }

    Token* pName = nullptr;
    FoxScope* Scope = nullptr;
};

struct FoxAstAssign : public FoxAstNode
{
    FoxAstAssign() { this->NodeType = FX_AST_ASSIGN; }

    FoxAstVarRef* Var = nullptr;
    // FoxValue Value;
    FoxAstNode* Rhs = nullptr;
};

struct FoxAstVarDecl : public FoxAstNode
{
    FoxAstVarDecl() { this->NodeType = FX_AST_VARDECL; }

    Token* pNameToken = nullptr;
    Token* pTypeToken = nullptr;
    eFoxType Type = eFoxType::INT;

    FoxAstAssign* pAssignment = nullptr;

    /// Ignore the scope that the variable is declared in, force it to be global.
    bool bDefineAsGlobal = false;
};

struct FoxAstDocComment : public FoxAstNode
{
    FoxAstDocComment() { this->NodeType = FX_AST_DOCCOMMENT; }

    Token* Comment;
};

struct FoxAstFunctionDecl : public FoxAstNode
{
    FoxAstFunctionDecl() { this->NodeType = FX_AST_PROCDECL; }

    Token* pNameToken = nullptr;
    FoxAstBlock* pParams = nullptr;
    FoxAstBlock* pBlock = nullptr;

    bool bIsExternal = false;

    eFoxType ReturnType = eFoxType::NONETYPE;

    uint32 SymbolTableOffset = 0;

    std::vector<FoxAstDocComment*> DocComments;
};

struct FoxAstCommandMode : public FoxAstNode
{
    FoxAstCommandMode() { this->NodeType = FX_AST_COMMANDMODE; }

    FoxAstNode* Node = nullptr;
};

struct FoxAstFunctionCall : public FoxAstNode
{
    FoxAstFunctionCall() { this->NodeType = FX_AST_PROCCALL; }

    eFoxType GetReturnType() const;

    FoxFunction* pFunction = nullptr;
    Hash32 HashedName = HashNull32;
    std::vector<FoxAstNode*> Params {}; // FoxAstLiteral or FoxAstVarRef
};

struct FoxAstReturn : public FoxAstNode
{
    FoxAstReturn() { this->NodeType = FX_AST_RETURN; }

    FoxAstNode* pRhs = nullptr;
};


//////////////////////////////////
// Script AST Printer
//////////////////////////////////

class FoxAstPrinter
{
public:
    FoxAstPrinter(FoxAstBlock* root_block)
    //: mRootBlock(root_block)
    {
    }

    void Print(FoxAstNode* node, int depth = 0);

public:
    // FoxAstBlock* mRootBlock = nullptr;
};


//////////////////////////////////
// Script AST Destroyer
//////////////////////////////////

class FoxAstDestroyer
{
public:
    FoxAstDestroyer(FoxAstBlock* root_block)
    //: mRootBlock(root_block)
    {
        Do(root_block);
    }

    void Do(FoxAstNode* node);

public:
    // FoxAstBlock* mRootBlock = nullptr;
};

struct FoxBytecodeVarHandle
{
    Hash32 HashedName = HashNull32;
    eFoxType Type = eFoxType::INT;
    int64 Offset = 0;

    uint16 VarIndexInScope = 0;

    uint16 SizeOnStack = 4;
    uint32 ScopeIndex = 0;
};

struct FoxBytecodeFunctionHandle
{
    Hash32 HashedName = HashNull32;
    uint32 BytecodeIndex = 0;
};

} // namespace script

} // namespace fx
