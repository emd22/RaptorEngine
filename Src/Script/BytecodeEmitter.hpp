#pragma once

#include "ScriptAst.hpp"
#include "ScriptBytecode.hpp"

#include <Core/PagedArray.hpp>
#include <Core/Types.hpp>

namespace fx::script {


////////////////////////////////////////////
// IR Emitter
////////////////////////////////////////////


using VarIndex = uint16;
constexpr VarIndex VarIndexNull = UINT16_MAX;

struct FoxIRFunctionRef
{
    char* Name = nullptr;
    uint32 HashedName = HashNull32;
    uint32 Position = 0;
};


class FoxBytecodeEmitter
{
public:
    FoxBytecodeEmitter() = default;

    void BeginEmitting(FoxAstNode* node);
    void Emit(FoxAstNode* node);

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

    static const char* GetRegisterName(FoxIRRegister reg);

    PagedArray<uint8> mBytecode {};

    enum VarDeclareMode
    {
        DECLARE_DEFAULT,
        DECLARE_NO_EMIT,
    };


private:
    void EmitBlock(FoxAstBlock* block, int params_to_save, bool is_function_body);
    void EmitFunctionDeclaration(FoxAstFunctionDecl* function);
    void EmitFunctionDefinitionsInBlock(FoxAstBlock* block);
    void DoFunctionCall(FoxAstFunctionCall* call);
    FoxBytecodeVarHandle* DoVarDeclare(FoxAstVarDecl* decl, VarDeclareMode mode = DECLARE_DEFAULT);
    void EmitAssign(FoxAstAssign* assign);
    FoxBytecodeVarHandle* DefineAndFetchParam(FoxAstNode* param_decl_node, uint16 index,
                                              bool alloc_stack_space = false);
    FoxBytecodeVarHandle* DefineReturnVar(FoxAstVarDecl* decl);

    void EmitSymbolTable(FoxAstBlock* root);

    uint16 GetSizeOfType(Token* type);

    void DoLoad(uint32 stack_offset, FoxIRRegister output_reg, bool force_absolute = false);
    void DoSaveInt32(uint32 stack_offset, uint32 value, bool force_absolute = false);
    void DoSaveReg32(uint32 stack_offset, FoxIRRegister reg, bool force_absolute = false);

    void EmitPush32(int32 value);
    void EmitPushFloat32(float32 value);
    void EmitPushVar(VarIndex var);

    void EmitPushVarOrLiteral(FoxAstNode* node);
    eFoxType GetVarOrLiteralType(FoxAstNode* node);

    void EmitStackAlloc(uint16 size);

    void EmitPop32(FoxIRRegister output_reg);
    void EmitPopVar(VarIndex var);

    void EmitLoad32(int offset, FoxIRRegister output_reg);
    void EmitLoadAbsolute32(uint32 position, FoxIRRegister output_reg);

    void EmitSave32(int16 offset, uint32 value);
    void EmitSaveReg32(int16 offset, FoxIRRegister reg);

    void EmitSaveAbsolute32(uint32 offset, uint32 value);
    void EmitSaveAbsoluteReg32(uint32 offset, FoxIRRegister reg);

    void EmitJumpRelative(uint16 offset);
    void EmitJumpAbsolute(uint32 position);
    void EmitJumpAbsoluteReg32(FoxIRRegister reg);
    void EmitJumpCallAbsolute(uint32 position);

    void EmitJumpReturnToCaller();
    void EmitJumpReturnToCallerInt32();
    void EmitJumpReturnToCallerFloat32();

    void EmitJumpCallExternal(Hash32 hashed_name);

    void EmitReturn(FoxAstReturn* return_node);

    void EmitVariableSetInt32(uint16 var_index, int32 value);
    void EmitVariableSetFloat32(uint16 var_index, float32 value);
    void EmitVariableSetVar(VarIndex dst, VarIndex src);


    void EmitVariableDefine(uint16 var_index, Hash32 name_hash);
    void EmitVariableIndex(uint16 var_index);

    void EmitVariableCastInt32(VarIndex var_index);
    void EmitVariableCastFloat32(VarIndex var_index);

    void EmitMoveInt32(FoxIRRegister reg, uint32 value);
    void EmitMoveReg32(FoxIRRegister dest_reg, FoxIRRegister src_reg);

    void EmitParamsStart();
    void EmitType(eFoxType type);

    uint32 EmitDataString(char* str, uint16 length);

    void EmitBinop(FoxAstBinop* binop);

    void EmitRhs(FoxAstNode* rhs, RhsMode mode, FoxBytecodeVarHandle* handle);

    FoxIRRegister EmitRhsToRegister(FoxAstNode* rhs, FoxIRRegister dest_register, bool auto_register = false);
    // FoxIRRegister EmitRhs(FoxAstNode* rhs, RhsMode mode, FoxBytecodeVarHandle* handle
    FoxIRRegister EmitLiteralString(FoxAstLiteral* literal, RhsMode mode, FoxBytecodeVarHandle* handle);

    void EmitMarker(BcSpecMarker spec);

    void WriteOp(uint8 base_op, uint8 spec_op);
    void Write16(uint16 value);
    void Write32(uint32 value);

    FoxIRRegister FindFreeReg32();
    FoxIRRegister FindFreeReg64();

    FoxBytecodeVarHandle* FindVarHandle(Hash32 hashed_name);
    VarIndex FindVarInScope(Hash32 hashed_name);

    FoxBytecodeFunctionHandle* FindFunctionHandle(Hash32 hashed_name);

    void PrintBytecode();

    void MarkRegisterUsed(FoxIRRegister reg);
    void MarkRegisterFree(FoxIRRegister reg);

    bool DoesNodeBranch(FoxAstNode* node);

    void MarkVariablesAsClobbered(FoxIRRegister start_reg, FoxIRRegister end_reg);

public:
    PagedArray<FoxBytecodeVarHandle> VarHandles;
    std::vector<FoxBytecodeFunctionHandle> FunctionHandles;

private:
    uint32 mRegsInUse = 0;

    int64 mStackOffset = 0;
    uint32 mStackSize = 0;

    uint16 mVarsInScope = 0;
    uint16 mScopeIndex = 0;

    FoxAstFunctionDecl* mpCurrentFunctionBody = nullptr;

    bool mEntryPointEmitted = false;
};


class FoxBytecodePrinter
{
public:
    FoxBytecodePrinter(PagedArray<uint8>& bytecode)
    {
        mBytecode = bytecode;
        mBytecode.bDoNotDestroy = true;
    }

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
    void DoMove(char* s, uint8 op_base, uint8 op_spec);
    void DoMarker(char* s, uint8 op_base, uint8 op_spec);
    void DoVariable(char* s, uint8 op_base, uint8 op_spec);

    char* ReadString(char* buffer, uint32 buffer_size);

private:
    uint32 mBytecodeIndex = 0;
    PagedArray<uint8> mBytecode;
};

} // namespace fx::script
