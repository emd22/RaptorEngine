#pragma once

#include "FoxValue.hpp"

#include <Core/Path.hpp>
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
    FX_AST_MODULECALL,
    FX_AST_RETURN,

    FX_AST_MODULELOAD,

    FX_AST_DOCCOMMENT,

    FX_AST_COMMANDMODE,

    FX_AST_IF,
};

struct FoxAstNode
{
    FoxAstType NodeType;
};


struct FoxAstLiteral : public FoxAstNode
{
    FoxAstLiteral() { this->NodeType = FX_AST_LITERAL; }

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

    bool bHasExplicitReturn = false;
};


struct FoxAstVarRef : public FoxAstNode
{
    FoxAstVarRef() { this->NodeType = FX_AST_VARREF; }

    Hash32 GetNameHash() const
    {
        if (pName == nullptr) {
            return HashNull32;
        }

        return pName->GetHash();
    }

    Token* pName = nullptr;
    FoxScope* pScope = nullptr;
};

struct FoxAstAssign : public FoxAstNode
{
    FoxAstAssign() { this->NodeType = FX_AST_ASSIGN; }

    FoxAstVarRef* pLhs = nullptr;
    FoxAstNode* pRhs = nullptr;
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
    bool bIsPointer = false;
};

struct FoxAstFunctionDecl : public FoxAstNode
{
    FoxAstFunctionDecl() { this->NodeType = FX_AST_PROCDECL; }

    bool IsForwardDeclaration() const { return pBlock == nullptr && pNameToken != nullptr; }
    bool IsDefinition() const { return pBlock != nullptr; }

    Token* pNameToken = nullptr;
    Token* pReturnTypeToken = nullptr;
    FoxAstBlock* pParams = nullptr;
    FoxAstBlock* pBlock = nullptr;

    bool bIsVariadic = false;
    bool bIsExternal = false;

    eFoxType ReturnType = eFoxType::NONETYPE;

    uint32 SymbolTableOffset = 0;
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
    bool HasReturnType() const;

    FoxFunction* pFunction = nullptr;
    Hash32 HashedName = HashNull32;
    std::vector<FoxAstNode*> Params {}; // FoxAstLiteral or FoxAstVarRef
};

struct FoxAstModuleLoad : public FoxAstNode
{
    FoxAstModuleLoad() { this->NodeType = FX_AST_MODULELOAD; }

    Token* pAlias = nullptr;
    Token* pModulePath = nullptr;

    uint32 ModuleIndex = 0;
};


struct FoxAstModuleCall : public FoxAstNode
{
    FoxAstModuleCall() { this->NodeType = FX_AST_MODULECALL; }

    FoxAstModuleLoad* pModuleLoad = nullptr;
    FoxAstFunctionCall* pFunctionCall = nullptr;
};


struct FoxAstReturn : public FoxAstNode
{
    FoxAstReturn() { this->NodeType = FX_AST_RETURN; }

    FoxAstNode* pRhs = nullptr;
};

struct FoxAstIf : public FoxAstNode
{
    FoxAstIf() { this->NodeType = FX_AST_IF; }

    FoxAstNode* pCondition = nullptr;
    FoxAstNode* pBlock = nullptr;
    FoxAstNode* pElseBlock = nullptr;
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
    FoxAstDestroyer() = default;
    FoxAstDestroyer(FoxAstBlock* root_block) { Do(root_block); }

    void Do(FoxAstNode* node);

public:
    // FoxAstBlock* mRootBlock = nullptr;
};

} // namespace script

} // namespace fx
