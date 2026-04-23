#include "BytecodeEmitter.hpp"

#include <Core/Log.hpp>
#include <Util/Tokenizer.hpp>

#define BC_PRINT_OP(fmt_, ...) fx::Log<fx::eLogChannel::None>(fmt_, ##__VA_ARGS__)


namespace fx::script {

static constexpr uint16 ReverseInt16(uint16 value) { return (value >> 8) | (value << 8); }

///////////////////////////////////////////////
// IR Asm Emitter
///////////////////////////////////////////////

#pragma region IrEmitter

#define MARK_REGISTER_USED(regn_)                                                                                      \
    {                                                                                                                  \
        MarkRegisterUsed(regn_);                                                                                       \
    }
#define MARK_REGISTER_FREE(regn_)                                                                                      \
    {                                                                                                                  \
        MarkRegisterFree(regn_);                                                                                       \
    }

using TT = eTokenType;

void FoxBytecodeEmitter::BeginEmitting(FoxAstNode* node)
{
    mStackSize = 1024;

    /*mStack = new uint8[1024];
    mStackStart = mStack;*/

    mBytecode.Create(4096);
    VarHandles.Create(64);

    Emit(node);

    PrintBytecode();
}

#define RETURN_IF_NO_NODE(node_)                                                                                       \
    if ((node_) == nullptr) {                                                                                          \
        return;                                                                                                        \
    }

#define RETURN_VALUE_IF_NO_NODE(node_, value_)                                                                         \
    if ((node_) == nullptr) {                                                                                          \
        return (value_);                                                                                               \
    }

void FoxBytecodeEmitter::EmitReturn(FoxAstReturn* return_node)
{
    // No return value provided, emit the non-value return. Returning from a typed function should be enforced at the
    // parsing stage!
    if (!return_node->pRhs) {
        EmitJumpReturnToCaller();
    }

    // Is there a return value provided?
    if (return_node->pRhs) {
        FoxAstNode* return_rhs = return_node->pRhs;

        // Check to see if its a literal
        if (return_rhs->NodeType == FX_AST_LITERAL) {
            FoxAstLiteral* literal = reinterpret_cast<FoxAstLiteral*>(return_rhs);

            EmitPushVarOrLiteral(literal);
            EmitJumpReturnToCallerValue();
        }
        else if (return_rhs->NodeType == FX_AST_PROCCALL) {
            FoxAstFunctionCall* call_node = reinterpret_cast<FoxAstFunctionCall*>(return_rhs);

            if (call_node->GetReturnType() == nullptr) {
                LogError("EmitReturn: Function called in return statement does not return a value.");
                EmitJumpReturnToCaller();
                return;
            }

            DoFunctionCall(call_node);

            // Function call returns a value and is pushed onto the stack.
            // Continue this value to the next caller.
            EmitJumpReturnToCallerValue();
        }
        else {
            LogError("EmitReturn: Return value type is not implemented!");
            EmitJumpReturnToCaller();
        }
    }
}


void FoxBytecodeEmitter::Emit(FoxAstNode* node)
{
    RETURN_IF_NO_NODE(node);

    if (node->NodeType == FX_AST_BLOCK) {
        return EmitBlock(reinterpret_cast<FoxAstBlock*>(node), 0, false);
    }
    else if (node->NodeType == FX_AST_PROCDECL) {
        return EmitFunction(reinterpret_cast<FoxAstFunctionDecl*>(node));
    }
    else if (node->NodeType == FX_AST_PROCCALL) {
        return DoFunctionCall(reinterpret_cast<FoxAstFunctionCall*>(node));
    }
    else if (node->NodeType == FX_AST_ASSIGN) {
        return EmitAssign(reinterpret_cast<FoxAstAssign*>(node));
    }
    else if (node->NodeType == FX_AST_VARDECL) {
        DoVarDeclare(reinterpret_cast<FoxAstVarDecl*>(node));
        return;
    }
    else if (node->NodeType == FX_AST_RETURN) {
        EmitReturn(reinterpret_cast<FoxAstReturn*>(node));
        return;
    }
}

FoxBytecodeVarHandle* FoxBytecodeEmitter::FindVarHandle(Hash32 hashed_name)
{
    for (FoxBytecodeVarHandle& handle : VarHandles) {
        if (handle.HashedName == hashed_name) {
            return &handle;
        }
    }
    return nullptr;
}

VarIndex FoxBytecodeEmitter::FindVarInScope(Hash32 hashed_name)
{
    // TODO: add back scope checks here
    for (const FoxBytecodeVarHandle& handle : VarHandles) {
        if (handle.HashedName == hashed_name) {
            return handle.VarIndexInScope;
        }
    }

    return VarIndexNull;
}

FoxBytecodeFunctionHandle* FoxBytecodeEmitter::FindFunctionHandle(Hash32 hashed_name)
{
    for (FoxBytecodeFunctionHandle& handle : FunctionHandles) {
        if (handle.HashedName == hashed_name) {
            return &handle;
        }
    }
    return nullptr;
}


FoxIRRegister FoxBytecodeEmitter::FindFreeReg32()
{
    for (int register_index = FX_IR_GW0; register_index <= FX_IR_GW7; register_index++) {
        uint32 gp_r = (1 << register_index);

        if (!(mRegsInUse & gp_r)) {
            return static_cast<FoxIRRegister>(register_index);
        }
    }

    return FX_IR_GW6;
}

FoxIRRegister FoxBytecodeEmitter::FindFreeReg64()
{
    for (int register_index = FX_IR_GX0; register_index <= FX_IR_GX3; register_index++) {
        uint32 gp_r = (1 << register_index);

        if (!(mRegsInUse & gp_r)) {
            return static_cast<FoxIRRegister>(register_index);
        }
    }

    return FX_IR_GX3;
}

const char* FoxBytecodeEmitter::GetRegisterName(FoxIRRegister reg)
{
    switch (reg) {
    case FX_IR_PARAMREG0:
        return "PARAMREG0";
    case FX_IR_PARAMREG1:
        return "PARAMREG1";
    case FX_IR_PARAMREG2:
        return "PARAMREG2";
    case FX_IR_PARAMREG3:
        return "PARAMREG3";
    case FX_IR_GW0:
        return "GW0";
    case FX_IR_GW1:
        return "GW1";
    case FX_IR_GW2:
        return "GW2";
    case FX_IR_GW3:
        return "GW3";
    case FX_IR_GW4:
        return "GW4";
    case FX_IR_GW5:
        return "GW5";
    case FX_IR_GW6:
        return "GW6";
    case FX_IR_GW7:
        return "GW7";
    case FX_IR_GX0:
        return "GX0";
    case FX_IR_GX1:
        return "GX1";
    case FX_IR_GX2:
        return "GX2";
    case FX_IR_GX3:
        return "GX3";
    case FX_IR_SP:
        return "SP";
    case FX_IR_REG_RETURN_VALUE:
        return "RETVAL";
    default:;
    };

    return "NONE";
}

void FoxBytecodeEmitter::Write16(uint16 value)
{
    mBytecode.Insert(static_cast<uint8>(value >> 8));
    mBytecode.Insert(static_cast<uint8>(value));
}

void FoxBytecodeEmitter::Write32(uint32 value)
{
    Write16(static_cast<uint16>(value >> 16));
    Write16(static_cast<uint16>(value));
}

void FoxBytecodeEmitter::WriteOp(uint8 op_base, uint8 op_spec)
{
    mBytecode.Insert(op_base);
    mBytecode.Insert(op_spec);
}

using IRRhsMode = FoxBytecodeEmitter::RhsMode;

#define MARK_REGISTER_USED(regn_)                                                                                      \
    {                                                                                                                  \
        MarkRegisterUsed(regn_);                                                                                       \
    }
#define MARK_REGISTER_FREE(regn_)                                                                                      \
    {                                                                                                                  \
        MarkRegisterFree(regn_);                                                                                       \
    }

void FoxBytecodeEmitter::MarkRegisterUsed(FoxIRRegister reg)
{
    uint16 register_flag = (1 << reg);
    mRegsInUse = static_cast<uint32>(uint16(mRegsInUse) | register_flag);
}

void FoxBytecodeEmitter::MarkRegisterFree(FoxIRRegister reg)
{
    uint16 register_flag = (1 << reg);
    mRegsInUse = static_cast<uint32>(uint16(mRegsInUse) & (~register_flag));
}

void FoxBytecodeEmitter::EmitSave32(int16 offset, uint32 value)
{
    // SAVE32 [i16 offset] [i32]
    WriteOp(BcBase_Save, BcSpecSave_Int32);

    Write16(offset);
    Write32(value);
}

void FoxBytecodeEmitter::EmitSaveReg32(int16 offset, FoxIRRegister reg)
{
    // SAVE32r [i16 offset] [%r32]
    WriteOp(BcBase_Save, BcSpecSave_Reg32);

    Write16(offset);
    Write16(reg);
}


void FoxBytecodeEmitter::EmitSaveAbsolute32(uint32 position, uint32 value)
{
    // SAVE32a [i32 offset] [i32]
    WriteOp(BcBase_Save, BcSpecSave_AbsoluteInt32);

    Write32(position);
    Write32(value);
}

void FoxBytecodeEmitter::EmitSaveAbsoluteReg32(uint32 position, FoxIRRegister reg)
{
    // SAVE32r [i32 offset] [%r32]
    WriteOp(BcBase_Save, BcSpecSave_AbsoluteReg32);

    Write32(position);
    Write16(reg);
}

void FoxBytecodeEmitter::EmitPush32(uint32 value)
{
    // PUSH32 [i32]
    WriteOp(BcBase_Push, BcSpecPush_Int32);
    Write32(value);

    mStackOffset += 4;
}

void FoxBytecodeEmitter::EmitPush32r(FoxIRRegister reg)
{
    // PUSH32r [%r32]
    WriteOp(BcBase_Push, BcSpecPush_Reg32);
    Write16(reg);

    mStackOffset += 4;
}

void FoxBytecodeEmitter::EmitPushVar(VarIndex var)
{
    // VPUSH [%var]
    WriteOp(BcBase_Push, BcSpecPush_Var);
    Write16(var);

    mStackOffset += 4;
}

void FoxBytecodeEmitter::EmitPushVarOrLiteral(FoxAstNode* node)
{
    if (node->NodeType == FX_AST_LITERAL) {
        FoxAstLiteral* literal = static_cast<FoxAstLiteral*>(node);

        if (literal->Value.Type == FoxValue::REF) {
            EmitPushVar(FindVarInScope(literal->Value.pValueRef->pName->GetHash()));
        }
        else {
            EmitPush32(literal->Value.ValueInt);
        }
    }
    else {
        LogError("EmitPushVarOrLiteral: Unknown node type");
    }
}

void FoxBytecodeEmitter::EmitStackAlloc(uint16 size)
{
    // SALLOC [u16]

    WriteOp(BcBase_Push, BcSpecPush_StackAlloc);
    Write16(size);

    mStackOffset += size;
}


void FoxBytecodeEmitter::EmitPop32(FoxIRRegister output_reg)
{
    // POP32 [%r32]
    // WriteOp(BcBase_Pop, (BcSpecPop_Int32 << 4) | (output_reg & 0x0F));

    // mStackOffset -= 4;
}

void FoxBytecodeEmitter::EmitPopVar(VarIndex var)
{
    // VPOP [%var]
    WriteOp(BcBase_Pop, BcSpecPop_Variable_Int32);
    Write16(var);

    mStackOffset -= 4;
}


void FoxBytecodeEmitter::EmitLoad32(int offset, FoxIRRegister output_reg)
{
    // LOAD [i16] [%r32]
    WriteOp(BcBase_Load, (BcSpecLoad_Int32 << 4) | (output_reg & 0x0F));
    Write16(static_cast<uint16>(offset));
}

void FoxBytecodeEmitter::EmitLoadAbsolute32(uint32 position, FoxIRRegister output_reg)
{
    // LOADA [i32] [%r32]
    WriteOp(BcBase_Load, (BcSpecLoad_AbsoluteInt32 << 4) | (output_reg & 0x0F));
    Write32(position);
}

void FoxBytecodeEmitter::EmitJumpRelative(uint16 offset)
{
    WriteOp(BcBase_Jump, BcSpecJump_Relative);
    Write16(offset);
}

void FoxBytecodeEmitter::EmitJumpAbsolute(uint32 position)
{
    WriteOp(BcBase_Jump, BcSpecJump_Absolute);
    Write32(position);
}


void FoxBytecodeEmitter::EmitJumpAbsoluteReg32(FoxIRRegister reg)
{
    WriteOp(BcBase_Jump, BcSpecJump_AbsoluteReg32);
    Write16(reg);
}

void FoxBytecodeEmitter::EmitJumpCallAbsolute(uint32 position)
{
    WriteOp(BcBase_Jump, BcSpecJump_CallAbsolute);
    Write32(position);
}


void FoxBytecodeEmitter::EmitJumpCallExternal(Hash32 hashed_name)
{
    WriteOp(BcBase_Jump, BcSpecJump_CallExternal);
    Write32(hashed_name);
}

void FoxBytecodeEmitter::EmitJumpReturnToCaller() { WriteOp(BcBase_Jump, BcSpecJump_ReturnToCaller); }

void FoxBytecodeEmitter::EmitJumpReturnToCallerValue() { WriteOp(BcBase_Jump, BcSpecJump_ReturnToCaller_Value); }

void FoxBytecodeEmitter::EmitJumpReturnToCallerInt32(int32 value)
{
    WriteOp(BcBase_Jump, BcSpecJump_ReturnToCaller_Int32);
    Write32(value);
}

void FoxBytecodeEmitter::EmitMoveInt32(FoxIRRegister reg, uint32 value)
{
    WriteOp(BcBase_Move, (BcSpecMove_Int32 << 4) | (reg & 0x0F));
    Write32(value);
}

void FoxBytecodeEmitter::EmitMoveReg32(FoxIRRegister dest_reg, FoxIRRegister src_reg)
{
    // Ignore if there is no work to do
    if (dest_reg == src_reg) {
        return;
    }

    WriteOp(BcBase_Move, (BcSpecMove_Reg32 << 4) | (dest_reg & 0x0F));
    Write16(src_reg);
}

void FoxBytecodeEmitter::EmitVariableSetInt32(uint16 var_index, int32 value)
{
    WriteOp(BcBase_Variable, BcSpecVariable_Set_Int32);
    Write16(var_index);
    Write32(value);
}

void FoxBytecodeEmitter::EmitVariableSetReg32(uint16 var_index, FoxIRRegister reg)
{
    WriteOp(BcBase_Variable, BcSpecVariable_Set_Reg32);
    Write16(var_index);
    Write16(reg);
}


void FoxBytecodeEmitter::EmitVariableSetVar(VarIndex dst, VarIndex src)
{
    WriteOp(BcBase_Variable, BcSpecVariable_Set_Var);
    Write16(dst);
    Write16(src);
}


void FoxBytecodeEmitter::EmitVariableGetInt32(uint16 var_index, FoxIRRegister dest_reg)
{
    WriteOp(BcBase_Variable, BcSpecVariable_Get_Int32);
    Write16(var_index);
    Write16(dest_reg);
}

void FoxBytecodeEmitter::EmitVariableDefineInt32(uint16 var_index, Hash32 name_hash)
{
    WriteOp(BcBase_Variable, BcSpecVariable_Define_Int32);
    Write16(var_index);
    Write32(name_hash);
}

void FoxBytecodeEmitter::EmitVariableIndex(uint16 var_index) { Write16(var_index); }

void FoxBytecodeEmitter::EmitParamsStart() { WriteOp(BcBase_Marker, BcSpecMarker_ParamsBegin); }

void FoxBytecodeEmitter::EmitType(FoxValue::eValueType type)
{
    BcSpecType op_type = BcSpecType_Int;

    if (type == FoxValue::STRING) {
        op_type = BcSpecType_String;
    }

    WriteOp(BcBase_Type, op_type);
}

uint32 FoxBytecodeEmitter::EmitDataString(char* str, uint16 length)
{
    // WriteOp(BcBase_Data, BcSpecData_String);

    uint32 start_index = mBytecode.Size();
    uint16 final_length = length;

    // If the length is not a factor of 2 (sizeof uint16) then add a byte of padding
    if ((final_length & 0x01) != 0) {
        ++final_length;
    }
    // The string is a factor of two, but there isn't going to be a null byte included. Pad with a whole uint16 to
    // preserve alignment.
    else {
        final_length += sizeof(uint16);
    }

    Write16(final_length);

    printf("String[Len=%02X %02X]: ", final_length >> 8, final_length & 0xFF);

    for (int i = 0; i < final_length; i++) {
        if (i >= length) {
            printf("%02X ", 0);

            mBytecode.Insert(0);
            continue;
        }

        printf("%02X ", str[i]);

        mBytecode.Insert(str[i]);
    }

    printf("\n");


    return start_index;
}


void FoxBytecodeEmitter::EmitBinop(FoxAstBinop* binop, FoxBytecodeVarHandle* handle)
{
    // Push LHS and RHS to stack
    EmitPushVarOrLiteral(binop->pLeft);
    EmitPushVarOrLiteral(binop->pRight);

    if (binop->OpToken->Type == TT::Plus) {
        WriteOp(BcBase_Arith, BcSpecArith_Add);
    }
}

FoxIRRegister FoxBytecodeEmitter::EmitVarFetch(FoxAstVarRef* ref, RhsMode mode)
{
    FoxBytecodeVarHandle* var_handle = FindVarHandle(ref->pName->GetHash());

    // bool force_absolute_load = false;

    // If the variable is from a previous scope, load it from an absolute address. local offsets
    // will change depending on where they are called.
    // if (var_handle->ScopeIndex < mScopeIndex) {
    //     force_absolute_load = true;
    // }

    if (!var_handle) {
        LogError("Could not find var handle!");
        return FX_IR_GW0;
    }

    if (var_handle->Register != FX_IR_NONE) {
        return var_handle->Register;
    }

    FoxIRRegister reg = FindFreeReg32();

    MARK_REGISTER_USED(reg);

    var_handle->Register = reg;

    // DoLoad(var_handle->Offset, reg, force_absolute_load);
    EmitVariableGetInt32(var_handle->VarIndexInScope, reg);

    if (mode == RhsMode::RHS_FETCH_TO_REGISTER) {
        return reg;
    }

    // If we are just copying the variable to this new variable, we can free the register after
    // we push to the stack.
    if (mode == RhsMode::RHS_DEFINE_IN_MEMORY) {
        if (var_handle->Type == FoxValue::STRING) {
            EmitType(var_handle->Type);
        }

        // Save the value register to the new variable

        EmitVariableSetReg32(var_handle->VarIndexInScope, reg);
        // EmitPush32r(reg);
        MARK_REGISTER_FREE(reg);

        return FX_IR_GW0;
    }

    return reg;
}


uint16 FoxBytecodeEmitter::GetSizeOfType(Token* token)
{
    const Hash32 type_hash = token->GetHash();

    constexpr Hash32 type_int_hash = HashStr32("int");
    constexpr Hash32 type_float_hash = HashStr32("float");

    if (type_hash == type_int_hash) {
        return sizeof(int32);
    }
    else if (type_hash == type_float_hash) {
        return sizeof(float32);
    }
    else {
        LogError("GetSizeOfType: Unknown type");
    }

    return 0;
}


void FoxBytecodeEmitter::DoLoad(uint32 stack_offset, FoxIRRegister output_reg, bool force_absolute)
{
    if (stack_offset < 0xFFFE && !force_absolute) {
        // Relative load

        // Calculate the relative index to the current stack offset
        int input_offset = -(static_cast<int>(mStackOffset) - static_cast<int>(stack_offset));

        EmitLoad32(input_offset, output_reg);
    }
    else {
        // Absolute load
        EmitLoadAbsolute32(stack_offset, output_reg);
    }
}

void FoxBytecodeEmitter::DoSaveInt32(uint32 stack_offset, uint32 value, bool force_absolute)
{
    if (stack_offset < 0xFFFE && !force_absolute) {
        // Relative save

        // Calculate the relative index to the current stack offset
        int input_offset = -(static_cast<int>(mStackOffset) - static_cast<int>(stack_offset));

        EmitSave32(input_offset, value);
    }
    else {
        // Absolute save
        EmitSaveAbsolute32(stack_offset, value);
    }
}

void FoxBytecodeEmitter::DoSaveReg32(uint32 stack_offset, FoxIRRegister reg, bool force_absolute)
{
    if (stack_offset < 0xFFFE && !force_absolute) {
        // Relative save

        // Calculate the relative index to the current stack offset
        int input_offset = -(static_cast<int>(mStackOffset) - static_cast<int>(stack_offset));

        EmitSaveReg32(input_offset, reg);
    }
    else {
        // Absolute save
        EmitSaveAbsoluteReg32(stack_offset, reg);
    }
}

void FoxBytecodeEmitter::EmitAssign(FoxAstAssign* assign)
{
    FoxBytecodeVarHandle* var_handle = FindVarHandle(assign->Var->pName->GetHash());
    if (var_handle == nullptr) {
        LogError("Var '{:.{}}' does not exist!", assign->Var->pName->Start, assign->Var->pName->Length);
        return;
    }

    // bool force_absolute_save = false;

    // if (var_handle->ScopeIndex < mScopeIndex) {
    //     force_absolute_save = true;
    // }

    // int output_offset = -(static_cast<int>(mStackOffset) - static_cast<int>(var_handle->Offset));

    if (!var_handle) {
        LogError("Could not find var handle to assign to!");
        return;
    }

    EmitRhs(assign->Rhs, RhsMode::RHS_ASSIGN_TO_HANDLE, var_handle);
}

FoxIRRegister FoxBytecodeEmitter::EmitLiteralInt(FoxAstLiteral* literal, RhsMode mode, FoxBytecodeVarHandle* handle)
{
    // If this is on variable definition, push the value to the stack.
    if (mode == RhsMode::RHS_DEFINE_IN_MEMORY) {
        EmitPush32(literal->Value.ValueInt);

        return FX_IR_GW3;
    }

    // If this is as a literal, push the value to the stack and pop onto the target register.
    else if (mode == RhsMode::RHS_FETCH_TO_REGISTER) {
        // EmitPush32(literal->Value.ValueInt);

        FoxIRRegister output_reg = FindFreeReg32();
        // EmitPop32(output_reg);

        EmitMoveInt32(output_reg, literal->Value.ValueInt);

        // Mark the output register as used to store it
        MARK_REGISTER_USED(output_reg);

        return output_reg;
    }

    else if (mode == RhsMode::RHS_ASSIGN_TO_HANDLE) {
        // const bool force_absolute_save = (handle->ScopeIndex < mScopeIndex);
        // DoSaveInt32(handle->Offset, literal->Value.ValueInt, force_absolute_save);
        EmitVariableSetInt32(handle->VarIndexInScope, literal->Value.ValueInt);

        return FX_IR_GW3;
    }

    return FX_IR_GW3;
}


FoxIRRegister FoxBytecodeEmitter::EmitLiteralString(FoxAstLiteral* literal, RhsMode mode, FoxBytecodeVarHandle* handle)
{
    const uint32 string_length = strlen(literal->Value.ValueString);

    // Emit the length and string data
    const uint32 string_position = EmitDataString(literal->Value.ValueString, string_length);

    // local string some_value = "Some String";
    if (mode == RhsMode::RHS_DEFINE_IN_MEMORY) {
        // Push the location and mark it as a pointer to a string
        EmitType(FoxValue::STRING);
        EmitPush32(string_position);

        return FX_IR_GW3;
    }

    // some_function("Some String")
    else if (mode == RhsMode::RHS_FETCH_TO_REGISTER) {
        // Push the location for the string and pop it back to a register.
        EmitType(FoxValue::STRING);

        // Push the string position
        // EmitPush32(string_position);

        // Find a register to output to and write the index
        FoxIRRegister output_reg = FindFreeReg32();
        // EmitPop32(output_reg);

        EmitMoveInt32(output_reg, string_position);

        // Mark the output register as used to store it
        MARK_REGISTER_USED(output_reg);

        return output_reg;
    }

    // some_previous_value = "Some String";
    else if (mode == RhsMode::RHS_ASSIGN_TO_HANDLE) {
        const bool force_absolute_save = (handle->ScopeIndex < mScopeIndex);

        DoSaveInt32(handle->Offset, string_position, force_absolute_save);
        handle->Type = FoxValue::STRING;

        return FX_IR_GW3;
    }

    return FX_IR_GW3;
}

void FoxBytecodeEmitter::EmitMarker(BcSpecMarker spec) { WriteOp(BcBase_Marker, spec); }


FoxIRRegister FoxBytecodeEmitter::EmitRhsToRegister(FoxAstNode* rhs, FoxIRRegister dest_register, bool auto_register)
{
    if (auto_register) {
        dest_register = FindFreeReg32();
    }

    MarkRegisterUsed(dest_register);

    if (rhs->NodeType == FX_AST_LITERAL) {
        FoxAstLiteral* literal = reinterpret_cast<FoxAstLiteral*>(rhs);

        // Move the integer directly into the register
        if (literal->Value.Type == FoxValue::INT) {
            EmitMoveInt32(dest_register, literal->Value.ValueInt);
            return dest_register;
        }

        else if (literal->Value.Type == FoxValue::REF) {
            Hash32 var_name_hash = literal->Value.pValueRef->pName->GetHash();
            FoxBytecodeVarHandle* var_handle = FindVarHandle(var_name_hash);

            if (!var_handle) {
                LogError("Could not find variable handle with hash {}!", var_name_hash);
                return dest_register;
            }

            // If the variable is already loaded into a register and we automatically select the register,
            // return the register that the value is currently loaded into.
            if (var_handle->Register != FX_IR_NONE && auto_register) {
                MarkRegisterFree(dest_register);

                return var_handle->Register;
            }

            // If the variable is already loaded into a register and we want it in `dest_register`, move it into
            // the destination register.
            if (var_handle->Register != FX_IR_NONE) {
                EmitMoveReg32(dest_register, var_handle->Register);
                return dest_register;
            }

            // The variable is not already loaded, so we can load it from the stack into our destination.
            EmitVariableGetInt32(var_handle->VarIndexInScope, dest_register);

            var_handle->Register = dest_register;

            return dest_register;
        }
    }

    else if (rhs->NodeType == FX_AST_BINOP) {
        EmitBinop(static_cast<FoxAstBinop*>(rhs), nullptr);

        return dest_register;
    }
    else if (rhs->NodeType == FX_AST_PROCCALL) {
        DoFunctionCall(static_cast<FoxAstFunctionCall*>(rhs));

        // Move return value into our destination register
        EmitMoveReg32(dest_register, FX_IR_REG_RETURN_VALUE);

        return dest_register;
    }

    LogWarning("No instructions emitted for RHS");

    MarkRegisterFree(dest_register);

    return FX_IR_NONE;
}

void FoxBytecodeEmitter::EmitRhs(FoxAstNode* rhs, FoxBytecodeEmitter::RhsMode mode, FoxBytecodeVarHandle* handle)
{
    if (rhs->NodeType == FX_AST_LITERAL) {
        FoxAstLiteral* literal = static_cast<FoxAstLiteral*>(rhs);

        if (literal->Value.Type == FoxValue::INT) {
            EmitLiteralInt(literal, mode, handle);
        }
        else if (literal->Value.Type == FoxValue::STRING) {
            EmitLiteralString(literal, mode, handle);
        }
        else if (literal->Value.Type == FoxValue::REF) {
            EmitVariableSetVar(handle->VarIndexInScope, FindVarInScope(literal->Value.pValueRef->pName->GetHash()));
        }
    }
    else if (rhs->NodeType == FX_AST_PROCCALL || rhs->NodeType == FX_AST_BINOP) {
        if (rhs->NodeType == FX_AST_BINOP) {
            EmitBinop(reinterpret_cast<FoxAstBinop*>(rhs), handle);
        }

        else if (rhs->NodeType == FX_AST_PROCCALL) {
            DoFunctionCall(reinterpret_cast<FoxAstFunctionCall*>(rhs));
        }

        if (mode == IRRhsMode::RHS_ASSIGN_TO_HANDLE) {
            EmitPopVar(handle->VarIndexInScope);
        }
    }
}

FoxBytecodeVarHandle* FoxBytecodeEmitter::DoVarDeclare(FoxAstVarDecl* decl, VarDeclareMode mode)
{
    RETURN_VALUE_IF_NO_NODE(decl, nullptr);

    // const uint16 size_of_type = static_cast<uint16>(sizeof(int32));

    const Hash32 type_int = HashStr32("int");
    const Hash32 type_string = HashStr32("string");

    Hash32 decl_hash = decl->Name->GetHash();
    Hash32 type_hash = decl->pType->GetHash();

    FoxValue::eValueType value_type = FoxValue::INT;

    switch (type_hash) {
    case type_int:
        value_type = FoxValue::INT;
        break;
    case type_string:
        value_type = FoxValue::STRING;
        break;
    };

    const uint16 size_of_type = GetSizeOfType(decl->pType);

    FoxBytecodeVarHandle handle {
        .HashedName = decl_hash,
        .Type = value_type, // Just int for now
        .Offset = (mStackOffset),
        .VarIndexInScope = mVarsInScope,
        .SizeOnStack = size_of_type,
        .ScopeIndex = mScopeIndex,
    };

    // FoxLogDebug("DEFINING PARAM");

    // mVarsInScope++;

    VarHandles.Insert(handle);

    // FoxBytecodeVarHandle* inserted_handle = &VarHandles[VarHandles.Size() - 1];

    FoxBytecodeVarHandle* var_handle = FindVarHandle(decl_hash);

    if (var_handle == nullptr) {
        // printf("!!! Could not find var handle!\n");
        LogError("Could not find var handle!");
        return nullptr;
    }

    if (mode == DECLARE_NO_EMIT) {
        // Do not emit any values
        return var_handle;
    }


    if (decl->Assignment) {
        FoxAstNode* rhs = decl->Assignment->Rhs;

        EmitRhs(rhs, RhsMode::RHS_ASSIGN_TO_HANDLE, var_handle);
    }

    return var_handle;
}

void FoxBytecodeEmitter::DoFunctionCall(FoxAstFunctionCall* call)
{
    RETURN_IF_NO_NODE(call);

    FoxBytecodeFunctionHandle* handle = FindFunctionHandle(call->HashedName);

    std::vector<uint32> call_locations;
    call_locations.reserve(8);

    EmitParamsStart();

    uint32 precall_regs_in_use = mRegsInUse;
    int32 parameter_index = 0;

    // Fetch all parameters into registers
    for (FoxAstNode* param : call->Params) {
        EmitPushVarOrLiteral(param);
        parameter_index++;
    }

    EmitJumpCallAbsolute(handle->HashedName);

    // Free all of the parameter used registers
    mRegsInUse = precall_regs_in_use;
}

void FoxBytecodeEmitter::MarkVariablesAsClobbered(FoxIRRegister start_reg, FoxIRRegister end_reg)
{
    for (FoxBytecodeVarHandle& var_handle : VarHandles) {
        if (var_handle.Register >= start_reg && var_handle.Register <= end_reg) {
            var_handle.Register = FX_IR_NONE;
        }
    }
}

FoxBytecodeVarHandle* FoxBytecodeEmitter::DefineAndFetchParam(FoxAstNode* param_decl_node, uint16 index,
                                                              bool alloc_stack_space)
{
    if (param_decl_node->NodeType != FX_AST_VARDECL) {
        LogError("DefineAndFetchParam: Param node type is not vardecl!");
        return nullptr;
    }

    FoxAstVarDecl* var_decl_node = reinterpret_cast<FoxAstVarDecl*>(param_decl_node);

    // Emit variable without emitting pushes or pops
    FoxBytecodeVarHandle* handle = DoVarDeclare(var_decl_node, DECLARE_NO_EMIT);
    LogDebug("DEFINING PARAMETER {}", var_decl_node->Name->GetStr());

    uint32 parameter_var_index = mVarsInScope++;

    EmitVariableDefineInt32(parameter_var_index, var_decl_node->Name->GetHash());

    if (!handle) {
        LogError("DefineAndFetchParam: Could not define and fetch param!");
        return nullptr;
    }

    EmitPopVar(parameter_var_index);

    return handle;
}

FoxBytecodeVarHandle* FoxBytecodeEmitter::DefineReturnVar(FoxAstVarDecl* decl)
{
    RETURN_VALUE_IF_NO_NODE(decl, nullptr);

    return DoVarDeclare(decl);
}

void FoxBytecodeEmitter::EmitFunctionDefinitionsInBlock(FoxAstBlock* block)
{
    if (!block) {
        return;
    }

    for (FoxAstNode* stmt : block->Statements) {
        if (stmt->NodeType == FX_AST_PROCDECL) {
            EmitFunction(reinterpret_cast<FoxAstFunctionDecl*>(stmt));
        }
    }
}

void FoxBytecodeEmitter::EmitFunction(FoxAstFunctionDecl* function)
{
    RETURN_IF_NO_NODE(function);

    // EmitFunctionDefinitionsInBlock(function->Block);

    ++mScopeIndex;

    // Store the bytecode offset before the function is emitted

    const size_t start_of_function = mBytecode.Size();
    printf("Start of function %zu\n", start_of_function);

    // Emit the jump instruction, we will update the jump position after emitting all of the code inside the block
    // EmitJumpRelative(0);

    // const uint32 initial_stack_offset = mStackOffset;

    // const size_t header_jump_start_index = start_of_function + sizeof(uint16);

    size_t start_var_handle_count = VarHandles.Size();

    // Offset for the pushed return address
    mStackOffset += 4;

    // Emit the body of the function
    {
        if (function->Name) {
            if (function->Block) {
                EmitMarker(BcSpecMarker_Proc);
            }
            // There is no block attached, so we will assume for now it is defined externally.
            else {
                EmitMarker(BcSpecMarker_ExternalProc);
            }

            // EmitMarker(BcSpecMarker_FunctionName);
            LogDebug("Data Name: {:.{}}\n", function->Name->Start, function->Name->Length);
            EmitDataString(function->Name->Start, function->Name->Length);
        }

        int parameter_index = 0;

        for (FoxAstNode* param_decl_node : function->Params->Statements) {
            DefineAndFetchParam(param_decl_node, parameter_index, (function->Block != nullptr));

            parameter_index++;
        }

        EmitBlock(function->Block, parameter_index, true);

        // Check to see if there has been a return statement in the function

        if (function->Block) {
            bool block_has_return = false;

            for (FoxAstNode* statement : function->Block->Statements) {
                if (statement->NodeType == FX_AST_RETURN) {
                    block_has_return = true;
                    break;
                }
            }

            // There is no return statement in the function's block, add a return statement
            if (!block_has_return) {
                EmitJumpReturnToCaller();

                // FoxIRRegister result_register = FindFreeReg32();

                // MARK_REGISTER_USED(result_register);

                // if (return_var != nullptr) {
                //     DoLoad(return_var->Offset, result_register);
                // }
            }


            // End of procedure, end of scope
            EmitMarker(BcSpecMarker_ProcEnd);
        }
    }

    // Return offset back to pre-call
    mStackOffset -= 4;

    // const size_t end_of_function = mBytecode.Size();
    // const uint16 distance_to_function = static_cast<uint16>(end_of_function - (start_of_function)-4);

    // Update the jump to the end of the function
    // mBytecode[header_jump_start_index] = static_cast<uint8>(distance_to_function >> 8);
    // mBytecode[header_jump_start_index + 1] = static_cast<uint8>((distance_to_function & 0xFF));

    FoxBytecodeFunctionHandle function_handle { .HashedName = function->Name->GetHash(),
                                                .BytecodeIndex = static_cast<uint32>(start_of_function) };

    const size_t number_of_scope_var_handles = VarHandles.Size() - start_var_handle_count;
    printf("Number of var handles to remove: %zu\n", number_of_scope_var_handles);

    FunctionHandles.push_back(function_handle);

    --mScopeIndex;

    // Delete the variables on the stack
    for (int i = 0; i < number_of_scope_var_handles; i++) {
        FoxBytecodeVarHandle* var = VarHandles.RemoveLast();
        assert(var->SizeOnStack == 4);
        mStackOffset -= var->SizeOnStack;
    }

    mVarsInScope = 0;
}

void FoxBytecodeEmitter::EmitBlock(FoxAstBlock* block, int params_to_save, bool is_function_body)
{
    RETURN_IF_NO_NODE(block);

    mVarsInScope = params_to_save;

    bool does_block_branch = false;

    // For each var declared in the block, write a stack allocation in the frame header
    for (FoxAstNode* node : block->Statements) {
        if (!does_block_branch && DoesNodeBranch(node)) {
            does_block_branch = true;
        }

        // Ignore function definitions when emitting the block statements as they are handled elsewhere!
        if (node->NodeType == FX_AST_PROCDECL) {
            continue;
        }
        else if (node->NodeType == FX_AST_VARDECL) {
            FoxAstVarDecl* var_decl = reinterpret_cast<FoxAstVarDecl*>(node);

            uint32 stack_index = mStackOffset;

            EmitVariableDefineInt32(mVarsInScope, var_decl->Name->GetHash());
            // EmitStackAlloc(GetSizeOfType(var_decl->Type));

            FoxBytecodeVarHandle var_handle {
                .HashedName = var_decl->Name->GetHash(),
                .Type = FoxValue::INT,
                .Offset = stack_index,
                .VarIndexInScope = mVarsInScope,
                .ScopeIndex = mScopeIndex,
            };

            VarHandles.Insert(var_handle);

            ++mVarsInScope;
        }
    }

    if (does_block_branch) {
        EmitMarker(BcSpecMarker_FunctionBranches);
    }

    if (!is_function_body) {
        // After the stack allocations, mark the start of the frame.
        EmitMarker(BcSpecMarker_FrameBegin);
    }

    // Emit all nodes inside of the scope
    for (FoxAstNode* node : block->Statements) {
        Emit(node);
    }

    // Clear scope
    mVarsInScope = 0;

    if (!is_function_body) {
        EmitMarker(BcSpecMarker_FrameEnd);
    }
}

void FoxBytecodeEmitter::PrintBytecode()
{
    const size_t size = mBytecode.Size();
    for (int i = 0; i < 25; i++) {
        printf("%02d ", i);
    }
    printf("\n");
    for (int i = 0; i < 25; i++) {
        printf("---");
    }
    printf("\n");

    for (size_t i = 0; i < size; i++) {
        printf("%02X ", mBytecode[i]);

        if (i > 0 && ((i + 1) % 25) == 0) {
            printf("\n");
        }
    }
    printf("\n");
}

bool FoxBytecodeEmitter::DoesNodeBranch(FoxAstNode* node)
{
    if (node == nullptr) {
        return false;
    }

    if (node->NodeType == FX_AST_PROCCALL) {
        return true;
    }

    else if (node->NodeType == FX_AST_BLOCK) {
        FoxAstBlock* block = reinterpret_cast<FoxAstBlock*>(node);
        for (FoxAstNode* stmt : block->Statements) {
            if (DoesNodeBranch(stmt)) {
                return true;
            }
        }

        return false;
    }

    else if (node->NodeType == FX_AST_VARDECL) {
        FoxAstVarDecl* vardecl = reinterpret_cast<FoxAstVarDecl*>(node);

        return DoesNodeBranch(vardecl->Assignment);
    }
    else if (node->NodeType == FX_AST_BINOP) {
        FoxAstBinop* binop = reinterpret_cast<FoxAstBinop*>(node);
        return (DoesNodeBranch(binop->pLeft) || DoesNodeBranch(binop->pRight));
    }

    else if (node->NodeType == FX_AST_ASSIGN) {
        FoxAstAssign* assign = reinterpret_cast<FoxAstAssign*>(node);

        return DoesNodeBranch(assign->Rhs);
    }

    return false;
}

#pragma endregion IrEmitter


/////////////////////////////////////
// IR Printer
/////////////////////////////////////

uint16 FoxBytecodePrinter::Read16()
{
    uint8 lo = mBytecode[mBytecodeIndex++];
    uint8 hi = mBytecode[mBytecodeIndex++];

    return ((static_cast<uint16>(lo) << 8) | hi);
}
uint16 FoxBytecodePrinter::Read16Rev()
{
    uint8 lo = mBytecode[mBytecodeIndex++];
    uint8 hi = mBytecode[mBytecodeIndex++];

    return ((static_cast<uint16>(hi) << 8) | lo);
}

uint32 FoxBytecodePrinter::Read32()
{
    uint16 lo = Read16();
    uint16 hi = Read16();

    return ((static_cast<uint32>(lo) << 16) | hi);
}


void FoxBytecodePrinter::DoLoad(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == BcSpecLoad_Int32) {
        int16 offset = Read16();
        BC_PRINT_OP("load [i32] {}, {}", offset,
                    FoxBytecodeEmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg)));
    }
    else if (op_spec == BcSpecLoad_AbsoluteInt32) {
        uint32 offset = Read32();
        BC_PRINT_OP("loada [i32] {}, {}", offset,
                    FoxBytecodeEmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg)));
    }
}

void FoxBytecodePrinter::DoPush(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecPush_Int32) {
        uint32 value = Read32();
        BC_PRINT_OP("PUSH [int32] {}", value);
    }
    else if (op_spec == BcSpecPush_Var) {
        VarIndex var = Read16();
        BC_PRINT_OP("VPUSH [int32] ${}", var);
    }
    // else if (op_spec == BcSpecPush_StackAlloc) {
    //     uint16 size = Read16();
    //     BC_PRINT_OP("salloc {}", size);
    // }
}

void FoxBytecodePrinter::DoPop(char* s, uint8 op_base, uint8 op_spec_raw)
{
    if (op_spec_raw == BcSpecPop_Variable_Int32) {
        VarIndex variable_index = Read16();
        BC_PRINT_OP("VPOP [int32] ${}", variable_index);
    }


    // uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    // uint8 op_reg = (op_spec_raw & 0x0F);

    // if (op_spec == BcSpecPop_Int32) {
    //     BC_PRINT_OP("pop [i32] {}", FoxBytecodeEmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg)));
    // }
}

void FoxBytecodePrinter::DoArith(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecArith_Add) {
        BC_PRINT_OP("ADD [int32]");
    }
}

void FoxBytecodePrinter::DoSave(char* s, uint8 op_base, uint8 op_spec)
{
    // Save a imm32 into an offset in the stack
    if (op_spec == BcSpecSave_Int32) {
        const int16 offset = Read16();
        const uint32 value = Read32();

        BC_PRINT_OP("save [i32] {}, {}", offset, value);
    }

    // Save a register into an offset in the stack
    else if (op_spec == BcSpecSave_Reg32) {
        const int16 offset = Read16();
        uint16 reg = Read16();

        BC_PRINT_OP("save [r32] {}, {}", offset, FoxBytecodeEmitter::GetRegisterName(static_cast<FoxIRRegister>(reg)));
    }
    else if (op_spec == BcSpecSave_AbsoluteInt32) {
        const uint32 offset = Read32();
        const uint32 value = Read32();

        BC_PRINT_OP("savea [i32] {}, {}", offset, value);
    }
    else if (op_spec == BcSpecSave_AbsoluteReg32) {
        const uint32 offset = Read32();
        uint16 reg = Read16();

        BC_PRINT_OP("savea [r32] {}, {}", offset, FoxBytecodeEmitter::GetRegisterName(static_cast<FoxIRRegister>(reg)));
    }
}

void FoxBytecodePrinter::DoJump(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecJump_Relative) {
        uint16 offset = Read16();
        BC_PRINT_OP("jmpr {}", offset);
    }
    else if (op_spec == BcSpecJump_Absolute) {
        uint32 position = Read32();
        BC_PRINT_OP("jmpa {}", position);
    }
    else if (op_spec == BcSpecJump_AbsoluteReg32) {
        uint16 reg = Read16();
        BC_PRINT_OP("jmpar {}", FoxBytecodeEmitter::GetRegisterName(static_cast<FoxIRRegister>(reg)));
    }
    else if (op_spec == BcSpecJump_CallAbsolute) {
        uint32 position = Read32();
        BC_PRINT_OP("calla {}", position);
    }
    else if (op_spec == BcSpecJump_ReturnToCaller) {
        BC_PRINT_OP("RET");
    }
    else if (op_spec == BcSpecJump_ReturnToCaller_Value) {
        BC_PRINT_OP("VRET");
    }
    else if (op_spec == BcSpecJump_ReturnToCaller_Int32) {
        int32 value = Read32();
        BC_PRINT_OP("ret [i32] {}", value);
    }
    else if (op_spec == BcSpecJump_CallExternal) {
        uint32 hashed_name = Read32();
        BC_PRINT_OP("callext {}", hashed_name);
    }
}


void FoxBytecodePrinter::DoData(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecData_String) {
        uint16 length = Read16();
        char* data_str = reinterpret_cast<char*>(malloc(length));
        uint16* data_str16 = reinterpret_cast<uint16*>(data_str);

        uint32 bytecode_end = mBytecodeIndex + length;
        int data_index = 0;
        while (mBytecodeIndex < bytecode_end) {
            uint16 value16 = Read16();
            data_str16[data_index++] = ((value16 << 8) | (value16 >> 8));
        }

        BC_PRINT_OP("datastr {}, {:.{}}", length, data_str, length);

        free(data_str);
    }
}

void FoxBytecodePrinter::DoType(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecType_Int) {
        BC_PRINT_OP("type int");
    }
    else if (op_spec == BcSpecType_String) {
        BC_PRINT_OP("type str");
    }
}

void FoxBytecodePrinter::DoMove(char* s, uint8 op_base, uint8 op_spec_raw)
{
    uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    uint8 op_reg = (op_spec_raw & 0x0F);

    if (op_spec == BcSpecMove_Int32) {
        uint32 value = Read32();
        BC_PRINT_OP("move [i32] {}, {}", FoxBytecodeEmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg)),
                    value);
    }
    else if (op_spec == BcSpecMove_Reg32) {
        const char* dest_reg = FoxBytecodeEmitter::GetRegisterName(static_cast<FoxIRRegister>(op_reg));
        const char* src_reg = FoxBytecodeEmitter::GetRegisterName(static_cast<FoxIRRegister>(Read16()));

        BC_PRINT_OP("move [r32] {}, {}", dest_reg, src_reg);
    }
}

char* FoxBytecodePrinter::ReadString(char* buffer, uint32 buffer_size)
{
    uint32 string_length = Read16();

    if (string_length > buffer_size) {
        LogWarning("ReadString: String length is greater than the read buffer size! ({} > {})", string_length,
                   buffer_size);
        string_length = buffer_size;
    }

    uint16* u16_buffer = reinterpret_cast<uint16*>(buffer);
    for (int index = 0; index < string_length; index += sizeof(uint16)) {
        (*u16_buffer) = Read16Rev();
        u16_buffer++;
    }

    return buffer;
}

void FoxBytecodePrinter::DoMarker(char* s, uint8 op_base, uint8 op_spec)
{
    constexpr int cTempBufferSize = 256;

    char temp_buffer[cTempBufferSize];

    if (op_spec == BcSpecMarker_FrameBegin) {
        BC_PRINT_OP("@FrameBegin");
    }
    else if (op_spec == BcSpecMarker_FrameEnd) {
        BC_PRINT_OP("@FrameEnd");
    }
    else if (op_spec == BcSpecMarker_ParamsBegin) {
        BC_PRINT_OP("@Params");
    }
    else if (op_spec == BcSpecMarker_ParamRegBlockBegin) {
        BC_PRINT_OP("@ParamRegBlockBegin");
    }
    else if (op_spec == BcSpecMarker_ParamRegBlockEnd) {
        BC_PRINT_OP("@ParamRegBlockEnd");
    }
    else if (op_spec == BcSpecMarker_EntryPoint) {
        BC_PRINT_OP("@Entry");
    }
    else if (op_spec == BcSpecMarker_FunctionBranches) {
        BC_PRINT_OP("@Branches");
    }
    else if (op_spec == BcSpecMarker_FunctionName) {
        char name_buffer[256];
        uint32 name_length = Read16();

        int name_index = 0;
        for (name_index = 0; name_index < name_length; name_index += 2) {
            *(reinterpret_cast<uint16*>(&name_buffer[name_index])) = ReverseInt16(Read16());
        }

        BC_PRINT_OP("@FunctionName {:.{}}", name_buffer, name_length);
    }
    else if (op_spec == BcSpecMarker_ExtFn) {
        BC_PRINT_OP("@ExtFn");
    }
    else if (op_spec == BcSpecMarker_Proc) {
        BC_PRINT_OP("\nPROC {}", ReadString(temp_buffer, cTempBufferSize));
    }
    else if (op_spec == BcSpecMarker_ProcEnd) {
        BC_PRINT_OP("PROC END");
    }
    else if (op_spec == BcSpecMarker_ExternalProc) {
        BC_PRINT_OP("\nEXTERNAL PROC {}", ReadString(temp_buffer, cTempBufferSize));
    }
}


void FoxBytecodePrinter::DoVariable(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecVariable_Get_Int32) {
        uint16 var_index = Read16();
        FoxIRRegister dest_reg = static_cast<FoxIRRegister>(Read16());
        BC_PRINT_OP("vget [i32] ${}, {}", var_index, FoxBytecodeEmitter::GetRegisterName(dest_reg));
    }
    else if (op_spec == BcSpecVariable_Set_Int32) {
        uint16 var_index = Read16();
        uint32 value = Read32();
        BC_PRINT_OP("VSET [int32] ${}, {}", var_index, value);
    }
    else if (op_spec == BcSpecVariable_Set_Reg32) {
        uint16 var_index = Read16();
        FoxIRRegister reg = static_cast<FoxIRRegister>(Read16());
        BC_PRINT_OP("vset [r32] ${}, {}", var_index, FoxBytecodeEmitter::GetRegisterName(reg));
    }
    else if (op_spec == BcSpecVariable_Set_Var) {
        VarIndex dst_index = Read16();
        VarIndex src_index = Read16();

        BC_PRINT_OP("VSET ${}, ${}", dst_index, src_index);
    }
    else if (op_spec == BcSpecVariable_Define_Int32) {
        uint16 var_index = Read16();
        Hash32 name_hash = Read32();
        BC_PRINT_OP("VDEFINE [int32] {} AS ${}", name_hash, var_index);
    }
}


void FoxBytecodePrinter::Print()
{
    while (mBytecodeIndex < mBytecode.Size()) {
        PrintOp();
    }
}

void FoxBytecodePrinter::PrintOp()
{
    // uint32 bc_index = mBytecodeIndex;

    uint16 op_full = Read16();

    const uint8 op_base = static_cast<uint8>(op_full >> 8);
    const uint8 op_spec = static_cast<uint8>(op_full & 0xFF);

    char s[128];

    switch (op_base) {
    case BcBase_Push:
        DoPush(s, op_base, op_spec);
        break;
    case BcBase_Pop:
        DoPop(s, op_base, op_spec);
        break;
    case BcBase_Load:
        DoLoad(s, op_base, op_spec);
        break;
    case BcBase_Arith:
        DoArith(s, op_base, op_spec);
        break;
    case BcBase_Jump:
        DoJump(s, op_base, op_spec);
        break;
    case BcBase_Save:
        DoSave(s, op_base, op_spec);
        break;
    case BcBase_Data:
        DoData(s, op_base, op_spec);
        break;
    case BcBase_Type:
        DoType(s, op_base, op_spec);
        break;
    case BcBase_Move:
        DoMove(s, op_base, op_spec);
        break;
    case BcBase_Marker:
        DoMarker(s, op_base, op_spec);
        break;
    case BcBase_Variable:
        DoVariable(s, op_base, op_spec);
        break;
    }
}

} // namespace fx::script
