#pragma once

#include <vector>
#include <Util/FxTokenizer.hpp>
#include <Core/MemPool/FxMPPagedArray.hpp>


#include <functional>

struct FxAstVarRef;
struct FxScriptScope;
struct FxScriptAction;

struct FxScriptValue
{
    enum ValueType : uint16
    {
        NONETYPE = 0x00,
        INT = 0x01,
        FLOAT = 0x02,
        STRING = 0x04,
        VEC3 = 0x08,
        REF = 0x10
    };

    ValueType Type = NONETYPE;

    union
    {
        int ValueInt = 0;
        float ValueFloat;
        float ValueVec3[3];
        char* ValueString;

        FxAstVarRef* ValueRef;
    };

    FxScriptValue()
    {
    }

    explicit FxScriptValue(ValueType type, int value)
        : Type(type), ValueInt(value)
    {
    }

    explicit FxScriptValue(ValueType type, float value)
        : Type(type), ValueFloat(value)
    {
    }

    FxScriptValue(const FxScriptValue& other)
    {
        Type = other.Type;
        if (other.Type == INT) {
            ValueInt = other.ValueInt;
        }
        else if (other.Type == FLOAT) {
            ValueFloat = other.ValueFloat;
        }
        else if (other.Type == STRING) {
            ValueString = other.ValueString;
        }
        else if (other.Type == REF) {
            ValueRef = other.ValueRef;
        }
    }

    void Print() const
    {
        printf("[Value: ");
        if (Type == NONETYPE) {
            printf("Null]\n");
        }
        else if (Type == INT) {
            printf("Int, %d]\n", ValueInt);
        }
        else if (Type == FLOAT) {
            printf("Float, %f]\n", ValueFloat);
        }
        else if (Type == STRING) {
            printf("String, %s]\n", ValueString);
        }
        else if (Type == REF) {
            printf("Ref, %p]\n", ValueRef);
        }
    }

    inline bool IsNumber()
    {
        return (Type == INT || Type == FLOAT);
    }

    inline bool IsRef()
    {
        return (Type == REF);
    }
};

enum FxAstType
{
    FX_AST_LITERAL,
    //FX_AST_NAME,

    FX_AST_BINOP,
    FX_AST_UNARYOP,
    FX_AST_BLOCK,

    // Variables
    FX_AST_VARREF,
    FX_AST_VARDECL,
    FX_AST_ASSIGN,

    // Actions
    FX_AST_ACTIONDECL,
    FX_AST_ACTIONCALL,
    FX_AST_RETURN,
};

struct FxAstNode
{
    FxAstType NodeType;
};


struct FxAstLiteral : public FxAstNode
{
    FxAstLiteral()
    {
        this->NodeType = FX_AST_LITERAL;
    }

    //FxTokenizer::Token* Token = nullptr;
    FxScriptValue Value;
};

struct FxAstBinop : public FxAstNode
{
    FxAstBinop()
    {
        this->NodeType = FX_AST_BINOP;
    }

    FxTokenizer::Token* OpToken = nullptr;
    FxAstNode* Left = nullptr;
    FxAstNode* Right = nullptr;
};

struct FxAstBlock : public FxAstNode
{
    FxAstBlock()
    {
        this->NodeType = FX_AST_BLOCK;
    }

    std::vector<FxAstNode*> Statements;
};

struct FxAstVarRef : public FxAstNode
{
    FxAstVarRef()
    {
        this->NodeType = FX_AST_VARREF;
    }

    FxTokenizer::Token* Name = nullptr;
    FxScriptScope* Scope = nullptr;
};

struct FxAstAssign : public FxAstNode
{
    FxAstAssign()
    {
        this->NodeType = FX_AST_ASSIGN;
    }

    FxAstVarRef* Var = nullptr;
    //FxScriptValue Value;
    FxAstNode* Rhs = nullptr;
};

struct FxAstVarDecl : public FxAstNode
{
    FxAstVarDecl()
    {
        this->NodeType = FX_AST_VARDECL;
    }

    FxTokenizer::Token* Name = nullptr;
    FxTokenizer::Token* Type = nullptr;
    FxAstAssign* Assignment = nullptr;

    /// Ignore the scope that the variable is declared in, force it to be global.
    bool DefineAsGlobal = false;
};

struct FxAstActionDecl : public FxAstNode
{
    FxAstActionDecl()
    {
        this->NodeType = FX_AST_ACTIONDECL;
    }

    FxTokenizer::Token* Name = nullptr;
    FxAstVarDecl* ReturnVar = nullptr;
    FxAstBlock* Params = nullptr;
    FxAstBlock* Block = nullptr;
};

struct FxAstActionCall : public FxAstNode
{
    FxAstActionCall()
    {
        this->NodeType = FX_AST_ACTIONCALL;
    }

    FxScriptAction* Action = nullptr;
    FxHash HashedName = 0;
    std::vector<FxAstNode*> Params{}; // FxAstLiteral or FxAstVarRef
};

struct FxAstReturn : public FxAstNode
{
    FxAstReturn()
    {
        this->NodeType = FX_AST_RETURN;
    }
};

/**
 * @brief Data is accessible from a label, such as a variable or an action.
 */
struct FxScriptLabelledData
{
    FxHash HashedName = 0;
    FxTokenizer::Token* Name = nullptr;

    FxScriptScope* Scope = nullptr;
};

struct FxScriptAction : public FxScriptLabelledData
{
    FxScriptAction(FxTokenizer::Token* name, FxScriptScope* scope, FxAstBlock* block, FxAstActionDecl* declaration)
    {
        HashedName = name->GetHash();
        Name = name;
        Scope = scope;
        Block = block;
        Declaration = declaration;
    }

    FxAstActionDecl* Declaration = nullptr;
    FxAstBlock* Block = nullptr;
};

struct FxScriptVar : public FxScriptLabelledData
{
    FxTokenizer::Token* Type = nullptr;
    FxScriptValue Value;

    bool IsExternal = false;

    void Print() const
    {
        printf("[Var] Type: %.*s, Name: %.*s (Hash:%u)", Type->Length, Type->Start, Name->Length, Name->Start, Name->GetHash());
        Value.Print();
    }

    FxScriptVar()
    {
    }

    FxScriptVar(FxTokenizer::Token* type, FxTokenizer::Token* name, FxScriptScope* scope, bool is_external = false)
        : Type(type)
    {
        this->HashedName = name->GetHash();
        this->Name = name;
        this->Scope = scope;
        IsExternal = is_external;
    }

    FxScriptVar(const FxScriptVar& other)
    {
        HashedName = other.HashedName;
        Type = other.Type;
        Name = other.Name;
        Value = other.Value;
        IsExternal = other.IsExternal;
    }

    ~FxScriptVar()
    {
        if (!IsExternal) {
            return;
        }

        // Free tokens allocated by external variables
        if (Type && Type->Start) {
            FxMemPool::Free<char>(Type->Start);
        }

        if (this->Name && this->Name->Start) {
            FxMemPool::Free<char>(this->Name->Start);
        }
    }
};

struct FxScriptExternalFunc
{
    using FuncType = std::function<void (std::vector<FxScriptValue>& params, FxScriptValue* return_value)>;

    FxHash HashedName = 0;
    FuncType Function = nullptr;

    std::vector<FxScriptValue::ValueType> ParameterTypes;
    bool IsVariadic = false;
};

struct FxScriptScope
{
    FxMPPagedArray<FxScriptVar> Vars;
    FxMPPagedArray<FxScriptAction> Actions;

    FxScriptScope* Parent = nullptr;

    // This points to the return value for the current scope. If an action returns a value,
    // this will be set to the variable that holds its value. This is interpreter only.
    FxScriptVar* ReturnVar = nullptr;

    void PrintAllVarsInScope()
    {
        puts("\n=== SCOPE ===");
        for (FxScriptVar& var : Vars) {
            var.Print();
        }
    }

    FxScriptVar* FindVarInScope(FxHash hashed_name)
    {
        return FindInScope<FxScriptVar>(hashed_name, Vars);
    }

    FxScriptAction* FindActionInScope(FxHash hashed_name)
    {
        return FindInScope<FxScriptAction>(hashed_name, Actions);
    }

    template <typename T> requires std::is_base_of_v<FxScriptLabelledData, T>
    T* FindInScope(FxHash hashed_name, const FxMPPagedArray<T>& buffer)
    {
        for (T& var : buffer) {
            if (var.HashedName == hashed_name) {
                return &var;
            }
        }

        return nullptr;
    }
};

class FxScriptInterpreter;

class FxConfigScript
{
    using Token = FxTokenizer::Token;
    using TT = FxTokenizer::TokenType;
public:
    void LoadFile(const char* path);

    void PushScope();
    void PopScope();

    FxScriptVar* FindVar(FxHash hashed_name);

    FxScriptAction* FindAction(FxHash hashed_name);

    /**
     * @brief
     * @return If there is a keyword present
     */
    FxAstNode* TryParseKeyword();
    //bool TryParseAssignment(FxScriptVar& dest);
    FxAstAssign* TryParseAssignment(FxTokenizer::Token* var_name);

    FxScriptValue ParseValue();

    FxAstActionDecl* ParseActionDeclare();

    //void ParseDoCall();

    FxAstNode* ParseRhs();
    FxAstActionCall* ParseActionCall();

    //FxScriptVar& ParseVarDeclare();
    FxAstVarDecl* ParseVarDeclare(FxScriptScope* scope = nullptr);

    FxAstBlock* ParseBlock();
    FxAstNode* ParseCommand();

    FxAstBlock* Parse();

    void Execute(FxScriptInterpreter& interpreter);

    Token& GetToken(int offset = 0);
    Token& EatToken(TT token_type);

    void DefineExternalVar(const char* type, const char* name, const FxScriptValue& value);

private:
    template <typename T> requires std::is_base_of_v<FxScriptLabelledData, T>
    T* FindLabelledData(FxHash hashed_name, FxMPPagedArray<T>& buffer)
    {
        FxScriptScope* scope = mCurrentScope;

        while (scope) {
            T* var = scope->FindInScope<T>(hashed_name, buffer);
            if (var) {
                return var;
            }

            scope = scope->Parent;
        }

        return nullptr;
    }

private:
    FxMPPagedArray<FxScriptScope> mScopes;
    FxScriptScope* mCurrentScope;

    bool mHasErrors = false;

    char* mFileData;
    std::vector<Token> mTokens;
    uint32 mTokenIndex = 0;
};

////////////////////////////////////////////////
// Script Interpreter
////////////////////////////////////////////////
class FxScriptInterpreter
{
public:
    FxScriptInterpreter();

    void Interpret();

    void PushScope();
    void PopScope();

    FxScriptVar* FindVar(FxHash hashed_name);
    FxScriptAction* FindAction(FxHash hashed_name);

    /**
     * @brief Evaluates and gets the immediate value if `value` is a reference, or returns the value if it is already immediate.
     * @param value The value to query from
     * @return the immediate(literal) value
     */
    const FxScriptValue& GetImmediateValue(const FxScriptValue& value);

    // void DefineExternalVar(const char* type, const char* name, const FxScriptValue& value);
    void RegisterExternalFunc(FxHash func_name, std::vector<FxScriptValue::ValueType> param_types, FxScriptExternalFunc::FuncType func, bool is_variadic);

private:
    friend class FxConfigScript;
    void Create(FxAstBlock* root_block);

    void Visit(FxAstNode* node);

    FxScriptValue VisitExternalCall(FxAstActionCall* call, FxScriptExternalFunc& func);
    FxScriptValue VisitActionCall(FxAstActionCall* call);
    void VisitAssignment(FxAstAssign* assign);
    FxScriptValue VisitRhs(FxAstNode* node);

    bool CheckExternalCallArgs(FxAstActionCall* call, FxScriptExternalFunc& func);

    void DefineDefaultExternalFunctions();

private:
    FxAstNode* mRootBlock = nullptr;

    std::vector<FxScriptExternalFunc> mExternalFuncs;

    FxMPPagedArray<FxScriptScope> mScopes;
    FxScriptScope* mCurrentScope = nullptr;
};

class FxAstPrinter
{
public:
    FxAstPrinter(FxAstBlock* root_block)
        : mRootBlock(root_block)
    {
    }

    void Print(FxAstNode* node, int depth = 0)
    {
        if (node == nullptr) {
            return;
        }

        for (int i = 0; i < depth; i++) {
            putchar(' ');
            putchar(' ');
        }

        if (node->NodeType == FX_AST_BLOCK) {
            puts("[BLOCK]");

            FxAstBlock* block = reinterpret_cast<FxAstBlock*>(node);
            for (FxAstNode* child : block->Statements) {
                Print(child, depth + 1);
            }
            return;
        }
        else if (node->NodeType == FX_AST_ACTIONDECL) {
            FxAstActionDecl* actiondecl = reinterpret_cast<FxAstActionDecl*>(node);
            printf("[ACTIONDECL] ");
            actiondecl->Name->Print();

            Print(actiondecl->Block, depth + 1);
        }
        else if (node->NodeType == FX_AST_VARDECL) {
            FxAstVarDecl* vardecl = reinterpret_cast<FxAstVarDecl*>(node);

            printf("[VARDECL] ");
            vardecl->Name->Print();

            Print(vardecl->Assignment, depth + 1);
        }
        else if (node->NodeType == FX_AST_ASSIGN) {
            FxAstAssign* assign = reinterpret_cast<FxAstAssign*>(node);

            printf("[ASSIGN] ");
            assign->Var->Name->Print();
            //assign->Value.Print();
            Print(assign->Rhs, depth + 1);
        }
        else if (node->NodeType == FX_AST_ACTIONCALL) {
            FxAstActionCall* actioncall = reinterpret_cast<FxAstActionCall*>(node);

            printf("[ACTIONCALL] ");
            if (actioncall->Action == nullptr) {
                printf("{defined externally}");
            }
            else {
                actioncall->Action->Name->Print(true);
            }
            printf(" (%zu params)\n", actioncall->Params.size());
        }
        else if (node->NodeType == FX_AST_LITERAL) {
            FxAstLiteral* literal = reinterpret_cast<FxAstLiteral*>(node);

            printf("[LITERAL] ");
            literal->Value.Print();
        }
        else if (node->NodeType == FX_AST_BINOP) {
            FxAstBinop* binop = reinterpret_cast<FxAstBinop*>(node);
            printf("[BINOP] ");
            binop->OpToken->Print();

            Print(binop->Left, depth + 1);
            Print(binop->Right, depth + 1);
        }
        else {
            puts("[UNKNOWN]");
        }
        //else if (node->NodeType == FX_AST_)
    }

public:
    FxAstBlock* mRootBlock = nullptr;
};
