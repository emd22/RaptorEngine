#pragma once

#include "FoxAst.hpp"
#include "FoxBytecode.hpp"

#include <Core/PagedArray.hpp>
#include <Core/Ref.hpp>
#include <Core/Types.hpp>

namespace fx::script {


////////////////////////////////////////////
// IR Emitter
////////////////////////////////////////////


enum class eFoxConditionResult
{
    Less,
    LessEqual,
    Equal,
    NotEqual,
    GreaterEqual,
    Greater,
};

using VarIndex = uint16;
constexpr VarIndex VarIndexNull = UINT16_MAX;

struct FoxIRFunctionRef
{
    char* Name = nullptr;
    uint32 HashedName = HashNull32;
    uint32 Position = 0;
};


class FoxBytecodeCompiler
{
public:
    enum RhsMode
    {
        RHS_FETCH_TO_REGISTER,

        /**
         * @brief Pushes the value to the stack, assuming that the value does not exist yet.
         */
        RHS_DEFINE_IN_MEMORY,

        RHS_ASSIGN_TO_HANDLE,

        RHS_NO_OPERATION,
    };


    enum VarDeclareMode
    {
        DECLARE_DEFAULT,
        DO_NOT_ALLOW_ASSIGNMENT,
    };

public:
    FoxBytecodeCompiler() = default;

    SizedArray<uint8> Compile(FoxAstNode* root_node);
    void EmitNode(FoxAstNode* node);

    void PrintBytecode();
    bool HasErrors() const { return mErrorCount > 0; }

    ~FoxBytecodeCompiler() = default;

private:
    void EmitBlock(FoxAstBlock* block, int params_to_save, bool is_function_body);
    void EmitFunctionDeclaration(FoxAstFunctionDecl* function);
    void EmitFunctionDefinitionsInBlock(FoxAstBlock* block);
    void DoFunctionCall(FoxAstFunctionCall* call);
    FoxBytecodeVarHandle* DoVarDeclare(FoxAstVarDecl* decl, VarDeclareMode mode = DECLARE_DEFAULT);
    void EmitAssign(FoxAstAssign* assign);
    FoxBytecodeVarHandle* DefineParam(FoxAstNode* param_decl_node);
    FoxBytecodeVarHandle* DefineReturnVar(FoxAstVarDecl* decl);

    void EmitSymbolTable(FoxAstBlock* root);

    template <typename... TTypes>
    void CompileError(const char* fmt, TTypes&&... args)
    {
        LogError(fmt, args...);
        ++mErrorCount;
    }

    uint16 GetSizeOfType(Token* type);

    void DoSaveInt32(uint32 stack_offset, uint32 value, bool force_absolute = false);

    void EmitPush32(int32 value);
    void EmitPushFloat32(float32 value);
    void EmitPushVar(VarIndex var);

    void EmitPushVarOrLiteral(FoxAstNode* node);
    eFoxType GetVarOrLiteralType(FoxAstNode* node);

    void EmitStackAlloc(uint16 size);

    void EmitPopVar(VarIndex var);


    void EmitSave32(int16 offset, uint32 value);

    void EmitSaveAbsolute32(uint32 offset, uint32 value);

    void EmitJumpRelative(uint16 offset);
    void EmitJumpConditional(uint16 offset, eFoxConditionResult cond);
    void EmitJumpAbsolute(uint32 position);
    void EmitJumpCallAbsolute(uint32 position);

    void EmitJumpReturnToCaller();
    void EmitJumpReturnToCallerInt32();
    void EmitJumpReturnToCallerFloat32();

    void EmitJumpCallExternal(Hash32 hashed_name);

    void EmitReturn(FoxAstReturn* return_node);
    void EmitIfStatement(FoxAstIf* if_node);
    eFoxConditionResult EmitPushConditionResult(FoxAstNode* condition_node);

    void EmitVariableSetInt32(uint16 var_index, int32 value);
    void EmitVariableSetFloat32(uint16 var_index, float32 value);
    void EmitVariableSetVar(VarIndex dst, VarIndex src);

    void EmitVariableDefine(uint16 var_index, Hash32 name_hash, bool is_global);
    void EmitVariableIndex(uint16 var_index);

    void EmitVariableCastInt32(VarIndex var_index);
    void EmitVariableCastFloat32(VarIndex var_index);

    void EmitCompare();
    void EmitCompareNotZero();

    void EmitParamsStart();
    void EmitType(eFoxType type);

    uint32 EmitDataString(char* str, uint16 length);

    void EmitBinop(FoxAstBinop* binop);

    void EmitRhs(FoxAstNode* rhs, RhsMode mode, FoxBytecodeVarHandle* handle);

    void EmitMarker(BcSpecMarker spec);

    void Fixup16(uint32 addr, uint16 value);
    void Fixup32(uint32 addr, uint32 value);

    void WriteOp(uint8 base_op, uint8 spec_op);
    void Write16(uint16 value);
    void Write32(uint32 value);

    FoxBytecodeVarHandle* FindVarHandle(Hash32 hashed_name);
    VarIndex FindVarInScope(Hash32 hashed_name);

    FoxBytecodeFunctionHandle* FindFunctionHandle(Hash32 hashed_name);


    bool DoesNodeBranch(FoxAstNode* node);


public:
    PagedArray<FoxBytecodeVarHandle> VarHandles;
    std::vector<FoxBytecodeFunctionHandle> FunctionHandles;
    PagedArray<uint8> mBytecode {};

private:
    uint16 mVariableIndex = 0;

    uint32 mErrorCount = 0;

    FoxAstFunctionDecl* mpCurrentFunctionBody = nullptr;

    bool mEntryPointEmitted = false;
};


class FoxBytecodePrinter
{
public:
    FoxBytecodePrinter(const SizedArray<uint8>& bytecode) { mpBytecode = Slice(bytecode); }

    void PrintSymbolTable();
    void Print();
    void PrintOp();


private:
    uint16 Read16();
    uint32 Read32();

    uint16 Read16Rev();

    void DoPush(char* s, uint8 op_base, uint8 op_spec);
    void DoPop(char* s, uint8 op_base, uint8 op_spec);
    void DoLoad(char* s, uint8 op_base, uint8 op_spec);
    void DoArith(char* s, uint8 op_base, uint8 op_spec);
    void DoSave(char* s, uint8 op_base, uint8 op_spec);
    void DoJump(char* s, uint8 op_base, uint8 op_spec);
    void DoData(char* s, uint8 op_base, uint8 op_spec);
    void DoType(char* s, uint8 op_base, uint8 op_spec);
    void DoMarker(char* s, uint8 op_base, uint8 op_spec);
    void DoVariable(char* s, uint8 op_base, uint8 op_spec);
    void DoCompare(char* s, uint8 op_base, uint8 op_spec);

    char* ReadString(char* buffer, uint32 buffer_size);

private:
    uint32 mBytecodeIndex = 0;
    Slice<uint8> mpBytecode { nullptr };
};

} // namespace fx::script
