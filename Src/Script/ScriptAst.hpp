#pragma once

#include <Core/Types.hpp>
#include <Util/Tokenizer.hpp>

namespace fx::script {

struct FoxAstVarRef;
struct FoxScope;
struct FoxFunction;


enum FoxIRRegister : uint8
{
    FX_IR_PARAMREG0,
    FX_IR_PARAMREG1,
    FX_IR_PARAMREG2,
    FX_IR_PARAMREG3,

    /* General Purpose (32 bit) registers */
    FX_IR_GW0,
    FX_IR_GW1,
    FX_IR_GW2,
    FX_IR_GW3,
    FX_IR_GW4,
    FX_IR_GW5,
    FX_IR_GW6,
    FX_IR_GW7,

    /* General Purpose (64 bit) registers */
    FX_IR_GX0,
    FX_IR_GX1,
    FX_IR_GX2,
    FX_IR_GX3,


    FX_IR_REG_RETURN_VALUE,

    /* Stack pointer */
    FX_IR_SP,

    FX_IR_NONE
};


struct FoxValue
{
    static const FoxValue scNone;

    enum eValueType : uint16
    {
        NONETYPE = 0x00,
        INT = 0x01,
        FLOAT = 0x02,
        STRING = 0x04,
        VEC3 = 0x08,
        REF = 0x10
    };

    eValueType Type = NONETYPE;

    union
    {
        int32 ValueInt = 0;
        float32 ValueFloat;
        float32 ValueVec3[3];
        char* ValueString;

        FoxAstVarRef* pValueRef;
    };

    FoxValue() {}

    explicit FoxValue(int value) : Type(eValueType::INT), ValueInt(value) {}
    explicit FoxValue(float value) : Type(eValueType::FLOAT), ValueFloat(value) {}

    static FoxValue NumberFromRaw(eValueType type, uint32 raw_value)
    {
        FoxValue value = FoxValue::scNone;

        switch (type) {
        case eValueType::INT:
            value.ValueInt = std::bit_cast<int32>(raw_value);
            value.Type = type;
            break;

        case eValueType::FLOAT:
            value.ValueFloat = std::bit_cast<float32>(raw_value);
            value.Type = type;
            break;
        default:
            break;
        }
        return value;
    }

    FoxValue(const FoxValue& other)
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
            pValueRef = other.pValueRef;
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
            printf("Ref, %p]\n", pValueRef);
        }
    }

    /////////////////////////////////////
    // Value set functions
    /////////////////////////////////////

    template <typename T>
    void Set(T value);

    template <>
    void Set<int32>(int32 value)
    {
        Type = eValueType::INT;
        ValueInt = value;
    }

    template <>
    void Set<float32>(float32 value)
    {
        Type = eValueType::FLOAT;
        ValueFloat = value;
    }

    /////////////////////////////////////
    // Value get functions
    /////////////////////////////////////

    template <typename T>
    T Get() const;

    template <>
    int32 Get<int32>() const
    {
        return ValueInt;
    }

    template <>
    float32 Get<float32>() const
    {
        return ValueFloat;
    }


    inline bool IsNumber() { return (Type == INT || Type == FLOAT); }

    inline bool IsRef() { return (Type == REF); }
};

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

    Token* Name = nullptr;
    Token* pType = nullptr;
    FoxAstAssign* Assignment = nullptr;

    /// Ignore the scope that the variable is declared in, force it to be global.
    bool DefineAsGlobal = false;
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

    FoxValue::eValueType ReturnType = FoxValue::eValueType::NONETYPE;

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

    FoxValue::eValueType GetReturnType() const;

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
    FoxValue::eValueType Type = FoxValue::INT;
    int64 Offset = 0;

    FoxIRRegister Register = FX_IR_NONE;

    uint16 VarIndexInScope = 0;

    uint16 SizeOnStack = 4;
    uint32 ScopeIndex = 0;
};

struct FoxBytecodeFunctionHandle
{
    Hash32 HashedName = HashNull32;
    uint32 BytecodeIndex = 0;
};

} // namespace fx::script

template <>
struct std::formatter<fx::script::FoxValue>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const fx::script::FoxValue& obj, std::format_context& ctx) const
    {
        using VT = fx::script::FoxValue::eValueType;

        if (obj.Type == VT::NONETYPE) {
            return std::format_to(ctx.out(), "Null");
        }
        else if (obj.Type == VT::INT) {
            return std::format_to(ctx.out(), "{}", obj.ValueInt);
        }
        else if (obj.Type == VT::FLOAT) {
            return std::format_to(ctx.out(), "{}", obj.ValueFloat);
        }
        else if (obj.Type == VT::STRING) {
            return std::format_to(ctx.out(), "{}", obj.ValueString);
        }
        else if (obj.Type == VT::REF) {
            return std::format_to(ctx.out(), "{:p}", reinterpret_cast<void*>(obj.pValueRef));
        }

        return std::format_to(ctx.out(), "Unknown");
    }
};
