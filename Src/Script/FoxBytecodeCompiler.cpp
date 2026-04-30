#include "FoxBytecodeCompiler.hpp"

#include "FoxVariable.hpp"

#include <Core/ArrayUtil.hpp>
#include <Core/Log.hpp>
#include <Core/PagedArray.hpp>
#include <Util/Tokenizer.hpp>

#define BC_PRINT_OP(fmt_, ...) fx::Log<fx::eLogChannel::None>(fmt_, ##__VA_ARGS__)

namespace fx::script {

static constexpr uint16 ReverseInt16(uint16 value) { return (value >> 8) | (value << 8); }


#define MARK_REGISTER_USED(regn_)                                                                                      \
    {                                                                                                                  \
        MarkRegisterUsed(regn_);                                                                                       \
    }
#define MARK_REGISTER_FREE(regn_)                                                                                      \
    {                                                                                                                  \
        MarkRegisterFree(regn_);                                                                                       \
    }

using TT = eTokenType;


SizedArray<uint8> FoxBytecodeCompiler::Compile(FoxAstNode* root)
{
    mBytecode.Create(4096);
    VarHandles.Create(64);

    Assert(root->NodeType == FX_AST_BLOCK);
    EmitSymbolTable(static_cast<FoxAstBlock*>(root));

    EmitNode(root);

    PrintBytecode();

    return ArrayUtil::LinearizePagedArray(mBytecode);
}

#define RETURN_IF_NO_NODE(node_)                                                                                       \
    if ((node_) == nullptr) {                                                                                          \
        return;                                                                                                        \
    }

#define RETURN_VALUE_IF_NO_NODE(node_, value_)                                                                         \
    if ((node_) == nullptr) {                                                                                          \
        return (value_);                                                                                               \
    }

void FoxBytecodeCompiler::EmitReturn(FoxAstReturn* return_node)
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

        if (return_rhs->NodeType == FX_AST_PROCCALL) {
            FoxAstFunctionCall* call_node = reinterpret_cast<FoxAstFunctionCall*>(return_rhs);

            if (call_node->GetReturnType() == eFoxType::NONETYPE) {
                CompileError("EmitReturn: Cannot return a result that is void. The function called in this return "
                             "statement does not return a value.");
                EmitJumpReturnToCaller();
                return;
            }

            DoFunctionCall(call_node);

            eFoxType return_type = mpCurrentFunctionBody ? mpCurrentFunctionBody->ReturnType : eFoxType::INT;

            // Function call returns a value and is pushed onto the stack.
            // Continue this value to the next caller.
            if (return_type == eFoxType::INT) {
                EmitJumpReturnToCallerInt32();
            }
            else if (return_type == eFoxType::FLOAT) {
                EmitJumpReturnToCallerFloat32();
            }
            else {
                CompileError("Unknown return type {}!", static_cast<uint32>(return_type));
            }
        }
        else {
            eFoxType return_type = mpCurrentFunctionBody ? mpCurrentFunctionBody->ReturnType : eFoxType::INT;

            EmitPushVarOrLiteral(return_rhs);
            LogInfo("Return type: {}", static_cast<int32>(return_type));

            if (return_type == eFoxType::INT) {
                EmitJumpReturnToCallerInt32();
            }
            else if (return_type == eFoxType::FLOAT) {
                EmitJumpReturnToCallerFloat32();
            }
            else {
                CompileError("Unknown return type {}!", static_cast<uint32>(return_type));
            }
        }
    }
}


eFoxConditionResult FoxBytecodeCompiler::EmitPushConditionResult(FoxAstNode* cond)
{
    if (cond->NodeType == FoxAstType::FX_AST_BINOP) {
        FoxAstBinop* binop = static_cast<FoxAstBinop*>(cond);
        EmitPushVarOrLiteral(binop->pLeft);
        EmitPushVarOrLiteral(binop->pRight);
        // Compare the values, push to stack
        EmitCompare();

        switch (binop->OpToken->Type) {
        case eTokenType::Equality:
            return eFoxConditionResult::Equal;
        default:
            break;
        }

        return eFoxConditionResult::Equal;
    }
    else if (cond->NodeType == FoxAstType::FX_AST_LITERAL) {
        EmitPushVarOrLiteral(static_cast<FoxAstLiteral*>(cond));
        EmitCompareNotZero();

        return eFoxConditionResult::Equal;
    }

    return eFoxConditionResult::Literal;
}


void FoxBytecodeCompiler::EmitIfStatement(FoxAstIf* if_node)
{
    eFoxConditionResult cond_result = EmitPushConditionResult(if_node->pCondition);

    switch (cond_result) {
    case eFoxConditionResult::Equal:
        EmitJumpEqual(sizeof(uint16));
        break;
    default:
        break;
    }

    EmitJumpRelative(0);

    uint32 original_offset = mBytecode.Size();
    uint32 fixup_addr = mBytecode.Size() - sizeof(uint16);

    EmitBlock(if_node->pBlock, 0, false);

    Fixup16(fixup_addr, mBytecode.Size() - original_offset);

    if (if_node->pElseBlock) {
        EmitBlock(if_node->pElseBlock, 0, false);
    }
}


void FoxBytecodeCompiler::EmitNode(FoxAstNode* node)
{
    RETURN_IF_NO_NODE(node);

    if (node->NodeType == FX_AST_BLOCK) {
        return EmitBlock(static_cast<FoxAstBlock*>(node), 0, false);
    }
    else if (node->NodeType == FX_AST_PROCDECL) {
        return EmitFunctionDeclaration(static_cast<FoxAstFunctionDecl*>(node));
    }
    else if (node->NodeType == FX_AST_PROCCALL) {
        return DoFunctionCall(static_cast<FoxAstFunctionCall*>(node));
    }
    else if (node->NodeType == FX_AST_ASSIGN) {
        return EmitAssign(static_cast<FoxAstAssign*>(node));
    }
    else if (node->NodeType == FX_AST_VARDECL) {
        DoVarDeclare(static_cast<FoxAstVarDecl*>(node));
        return;
    }
    else if (node->NodeType == FX_AST_IF) {
        EmitIfStatement(static_cast<FoxAstIf*>(node));
        return;
    }
    else if (node->NodeType == FX_AST_RETURN) {
        EmitReturn(static_cast<FoxAstReturn*>(node));
        return;
    }
}

FoxBytecodeVarHandle* FoxBytecodeCompiler::FindVarHandle(Hash32 hashed_name)
{
    for (FoxBytecodeVarHandle& handle : VarHandles) {
        if (handle.HashedName == hashed_name) {
            return &handle;
        }
    }
    return nullptr;
}

VarIndex FoxBytecodeCompiler::FindVarInScope(Hash32 hashed_name)
{
    // TODO: add back scope checks here
    for (const FoxBytecodeVarHandle& handle : VarHandles) {
        if (handle.HashedName == hashed_name) {
            return handle.VariableIndex;
        }
    }

    return VarIndexNull;
}

FoxBytecodeFunctionHandle* FoxBytecodeCompiler::FindFunctionHandle(Hash32 hashed_name)
{
    for (FoxBytecodeFunctionHandle& handle : FunctionHandles) {
        if (handle.HashedName == hashed_name) {
            return &handle;
        }
    }
    return nullptr;
}


void FoxBytecodeCompiler::Write16(uint16 value)
{
    mBytecode.Insert(static_cast<uint8>(value >> 8));
    mBytecode.Insert(static_cast<uint8>(value));
}

void FoxBytecodeCompiler::Write32(uint32 value)
{
    Write16(static_cast<uint16>(value >> 16));
    Write16(static_cast<uint16>(value));
}

void FoxBytecodeCompiler::WriteOp(uint8 op_base, uint8 op_spec)
{
    mBytecode.Insert(op_base);
    mBytecode.Insert(op_spec);
}

using IRRhsMode = FoxBytecodeCompiler::RhsMode;

#define MARK_REGISTER_USED(regn_)                                                                                      \
    {                                                                                                                  \
        MarkRegisterUsed(regn_);                                                                                       \
    }
#define MARK_REGISTER_FREE(regn_)                                                                                      \
    {                                                                                                                  \
        MarkRegisterFree(regn_);                                                                                       \
    }


void FoxBytecodeCompiler::EmitSave32(int16 offset, uint32 value)
{
    // SAVE32 [i16 offset] [i32]
    WriteOp(BcBase_Save, BcSpecSave_Int32);

    Write16(offset);
    Write32(value);
}

void FoxBytecodeCompiler::EmitSaveAbsolute32(uint32 position, uint32 value)
{
    // SAVE32a [i32 offset] [i32]
    WriteOp(BcBase_Save, BcSpecSave_AbsoluteInt32);

    Write32(position);
    Write32(value);
}


void FoxBytecodeCompiler::EmitPush32(int32 value)
{
    // PUSH32 [i32]
    WriteOp(BcBase_Push, BcSpecPush_Int32);
    Write32(std::bit_cast<uint32>(value));
}

void FoxBytecodeCompiler::EmitPushFloat32(float32 value)
{
    // PUSH32F [f32]
    WriteOp(BcBase_Push, BcSpecPush_Float32);
    Write32(std::bit_cast<uint32>(value));
}

void FoxBytecodeCompiler::EmitPushVar(VarIndex var)
{
    // VPUSH [%var]
    WriteOp(BcBase_Push, BcSpecPush_Var);
    Write16(var);
}

void FoxBytecodeCompiler::EmitPushVarOrLiteral(FoxAstNode* node)
{
    if (node->NodeType == FX_AST_LITERAL) {
        FoxAstLiteral* literal = static_cast<FoxAstLiteral*>(node);

        if (literal->Value.Type == eFoxType::INT) {
            EmitPush32(literal->Value.ValueInt);
        }
        else if (literal->Value.Type == eFoxType::REF) {
            EmitPushVar(FindVarInScope(literal->Value.pValueRef->pName->GetHash()));
        }
        else if (literal->Value.Type == eFoxType::FLOAT) {
            EmitPushFloat32(literal->Value.ValueFloat);
        }
    }
    else if (node->NodeType == FX_AST_BINOP) {
        EmitBinop(static_cast<FoxAstBinop*>(node));
    }
    else if (node->NodeType == FX_AST_PROCCALL) {
        DoFunctionCall(static_cast<FoxAstFunctionCall*>(node));
    }
    else {
        CompileError("EmitPushVarOrLiteral: Unknown node type");
    }
}

eFoxType FoxBytecodeCompiler::GetVarOrLiteralType(FoxAstNode* node)
{
    if (node->NodeType == FX_AST_LITERAL) {
        FoxAstLiteral* literal = static_cast<FoxAstLiteral*>(node);

        if (literal->Value.Type == eFoxType::REF) {
            FoxBytecodeVarHandle* vhandle = FindVarHandle(literal->Value.pValueRef->pName->GetHash());

            if (vhandle) {
                LogInfo("Handle type: {}", static_cast<int32>(vhandle->Type));
                return vhandle->Type;
            }

            return eFoxType::NONETYPE;
        }

        return literal->Value.Type;
    }
    else if (node->NodeType == FX_AST_BINOP) {
        FoxAstBinop* binop = static_cast<FoxAstBinop*>(node);
        return GetVarOrLiteralType(binop->pLeft);
    }
    else if (node->NodeType == FX_AST_PROCCALL) {
        FoxAstFunctionCall* fcall = static_cast<FoxAstFunctionCall*>(node);
        return fcall->GetReturnType();
    }
    else {
        CompileError("EmitPushVarOrLiteral: Unknown node type");
    }

    return eFoxType::NONETYPE;
}


void FoxBytecodeCompiler::EmitStackAlloc(uint16 size)
{
    // SALLOC [u16]

    WriteOp(BcBase_Push, BcSpecPush_StackAlloc);
    Write16(size);
}


void FoxBytecodeCompiler::EmitPopVar(VarIndex var)
{
    // VPOP [%var]
    WriteOp(BcBase_Pop, BcSpecPop_Variable_Int32);
    Write16(var);
}


void FoxBytecodeCompiler::EmitJumpRelative(uint16 offset)
{
    WriteOp(BcBase_Jump, BcSpecJump_Relative);
    Write16(offset);
}

void FoxBytecodeCompiler::EmitJumpEqual(uint16 offset)
{
    WriteOp(BcBase_Jump, BcSpecJump_Equal);
    Write16(offset);
}

void FoxBytecodeCompiler::EmitJumpAbsolute(uint32 position)
{
    WriteOp(BcBase_Jump, BcSpecJump_Absolute);
    Write32(position);
}


void FoxBytecodeCompiler::EmitJumpCallAbsolute(uint32 position)
{
    WriteOp(BcBase_Jump, BcSpecJump_CallAbsolute);
    Write32(position);
}


void FoxBytecodeCompiler::EmitJumpCallExternal(Hash32 hashed_name)
{
    WriteOp(BcBase_Jump, BcSpecJump_CallExternal);
    Write32(hashed_name);
}

void FoxBytecodeCompiler::EmitJumpReturnToCaller() { WriteOp(BcBase_Jump, BcSpecJump_ReturnToCaller); }

void FoxBytecodeCompiler::EmitJumpReturnToCallerInt32() { WriteOp(BcBase_Jump, BcSpecJump_ReturnToCaller_Int32); }
void FoxBytecodeCompiler::EmitJumpReturnToCallerFloat32() { WriteOp(BcBase_Jump, BcSpecJump_ReturnToCaller_Float32); }


void FoxBytecodeCompiler::EmitVariableSetInt32(uint16 var_index, int32 value)
{
    WriteOp(BcBase_Variable, BcSpecVariable_Set_Int32);
    Write16(var_index);
    Write32(value);
}

void FoxBytecodeCompiler::EmitVariableSetFloat32(uint16 var_index, float32 value)
{
    WriteOp(BcBase_Variable, BcSpecVariable_Set_Float32);
    Write16(var_index);
    Write32(std::bit_cast<uint32>(value));
}


void FoxBytecodeCompiler::EmitVariableSetVar(VarIndex dst, VarIndex src)
{
    WriteOp(BcBase_Variable, BcSpecVariable_Set_Var);
    Write16(dst);
    Write16(src);
}


void FoxBytecodeCompiler::EmitVariableDefine(uint16 var_index, Hash32 name_hash, bool is_global)
{
    if (is_global) {
        WriteOp(BcBase_Variable, BcSpecVariable_DefineGlobal);
    }
    else {
        WriteOp(BcBase_Variable, BcSpecVariable_Define);
    }

    Write16(var_index);
    Write32(name_hash);
}


void FoxBytecodeCompiler::EmitVariableIndex(uint16 var_index) { Write16(var_index); }

void FoxBytecodeCompiler::EmitVariableCastInt32(VarIndex var_index)
{
    WriteOp(BcBase_Variable, BcSpecVariable_Cast_Int32);
}

void FoxBytecodeCompiler::EmitVariableCastFloat32(VarIndex var_index)
{
    WriteOp(BcBase_Variable, BcSpecVariable_Cast_Float32);
}

void FoxBytecodeCompiler::EmitCompare() { WriteOp(BcBase_Compare, BcSpecCompare_Default); }
void FoxBytecodeCompiler::EmitCompareNotZero() { WriteOp(BcBase_Compare, BcSpecCompare_NotZero); }

void FoxBytecodeCompiler::EmitParamsStart() {}

void FoxBytecodeCompiler::EmitType(eFoxType type)
{
    BcSpecType op_type = BcSpecType_Int;

    if (type == eFoxType::STRING) {
        op_type = BcSpecType_String;
    }

    WriteOp(BcBase_Type, op_type);
}

uint32 FoxBytecodeCompiler::EmitDataString(char* str, uint16 length)
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

    for (int i = 0; i < final_length; i++) {
        if (i >= length) {
            mBytecode.Insert(0);
            continue;
        }

        mBytecode.Insert(str[i]);
    }

    return start_index;
}


void FoxBytecodeCompiler::EmitBinop(FoxAstBinop* binop)
{
    eFoxType result_type = GetVarOrLiteralType(binop->pLeft);

    // Push LHS and RHS to stack
    EmitPushVarOrLiteral(binop->pLeft);
    EmitPushVarOrLiteral(binop->pRight);

    if (binop->OpToken->Type == TT::Plus) {
        WriteOp(BcBase_Arith, (result_type == eFoxType::INT) ? BcSpecArith_Add : BcSpecArith_Add_Float32);
    }
}


uint16 FoxBytecodeCompiler::GetSizeOfType(Token* token)
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
        CompileError("GetSizeOfType: Unknown type");
    }

    return 0;
}


void FoxBytecodeCompiler::DoSaveInt32(uint32 stack_offset, uint32 value, bool force_absolute)
{
    if (stack_offset < 0xFFFE && !force_absolute) {
        // Relative save
    }
    else {
        // Absolute save
        EmitSaveAbsolute32(stack_offset, value);
    }
}


void FoxBytecodeCompiler::EmitAssign(FoxAstAssign* assign)
{
    FoxBytecodeVarHandle* var_handle = FindVarHandle(assign->Var->pName->GetHash());
    if (var_handle == nullptr) {
        CompileError("Var '{:.{}}' does not exist!", assign->Var->pName->Start, assign->Var->pName->Length);
        return;
    }

    // bool force_absolute_save = false;

    // if (var_handle->ScopeIndex < mScopeIndex) {
    //     force_absolute_save = true;
    // }

    // int output_offset = -(static_cast<int>(mStackOffset) - static_cast<int>(var_handle->Offset));

    if (!var_handle) {
        CompileError("Could not find var handle to assign to!");
        return;
    }

    EmitRhs(assign->Rhs, RhsMode::RHS_ASSIGN_TO_HANDLE, var_handle);
}

void FoxBytecodeCompiler::EmitMarker(BcSpecMarker spec) { WriteOp(BcBase_Marker, spec); }

void FoxBytecodeCompiler::Fixup16(uint32 addr, uint16 value)
{
    mBytecode[addr] = static_cast<uint8>(value >> 8);
    mBytecode[addr + 1] = static_cast<uint8>(value);
}

void FoxBytecodeCompiler::Fixup32(uint32 addr, uint32 value)
{
    mBytecode[addr] = static_cast<uint8>(value >> 24);
    mBytecode[addr + 1] = static_cast<uint8>(value >> 16);
    mBytecode[addr + 2] = static_cast<uint8>(value >> 8);
    mBytecode[addr + 3] = static_cast<uint8>(value);
}


void FoxBytecodeCompiler::EmitRhs(FoxAstNode* rhs, FoxBytecodeCompiler::RhsMode mode, FoxBytecodeVarHandle* handle)
{
    if (rhs->NodeType == FX_AST_LITERAL) {
        FoxAstLiteral* literal = static_cast<FoxAstLiteral*>(rhs);

        if (literal->Value.Type == eFoxType::INT) {
            EmitVariableSetInt32(handle->VariableIndex, literal->Value.ValueInt);
        }
        else if (literal->Value.Type == eFoxType::FLOAT) {
            EmitVariableSetFloat32(handle->VariableIndex, literal->Value.ValueFloat);
        }
        else if (literal->Value.Type == eFoxType::REF) {
            EmitVariableSetVar(handle->VariableIndex, FindVarInScope(literal->Value.pValueRef->pName->GetHash()));
        }
    }
    else if (rhs->NodeType == FX_AST_PROCCALL || rhs->NodeType == FX_AST_BINOP) {
        if (rhs->NodeType == FX_AST_BINOP) {
            EmitBinop(reinterpret_cast<FoxAstBinop*>(rhs));
        }

        else if (rhs->NodeType == FX_AST_PROCCALL) {
            DoFunctionCall(reinterpret_cast<FoxAstFunctionCall*>(rhs));
        }

        if (mode == FoxBytecodeCompiler::RhsMode::RHS_ASSIGN_TO_HANDLE) {
            EmitPopVar(handle->VariableIndex);
        }
    }
}

FoxBytecodeVarHandle* FoxBytecodeCompiler::DoVarDeclare(FoxAstVarDecl* decl, VarDeclareMode mode)
{
    RETURN_VALUE_IF_NO_NODE(decl, nullptr);


    const Hash32 type_int = HashStr32("int");
    const Hash32 type_float = HashStr32("float");
    const Hash32 type_string = HashStr32("string");

    Hash32 decl_hash = decl->pNameToken->GetHash();
    Hash32 type_hash = decl->pTypeToken->GetHash();

    eFoxType value_type = eFoxType::INT;

    switch (type_hash) {
    case type_int:
        value_type = eFoxType::INT;
        break;
    case type_float:
        value_type = eFoxType::FLOAT;
        break;
    case type_string:
        value_type = eFoxType::STRING;
        break;
    };

    const uint16 size_of_type = GetSizeOfType(decl->pTypeToken);

    EmitVariableDefine(mVariableIndex, decl->pNameToken->GetHash(), decl->bDefineAsGlobal);

    FoxBytecodeVarHandle handle {
        .HashedName = decl_hash,
        .Type = value_type, // Just int for now
        .Offset = 0,
        .VariableIndex = mVariableIndex,
        .SizeOnStack = size_of_type,
    };

    VarHandles.Insert(handle);
    ++mVariableIndex;

    FoxBytecodeVarHandle* var_handle = FindVarHandle(decl_hash);

    if (var_handle == nullptr) {
        CompileError("Could not find variable handle!");
        return nullptr;
    }

    if (mode == DO_NOT_ALLOW_ASSIGNMENT) {
        return var_handle;
    }

    if (decl->pAssignment) {
        FoxAstNode* rhs = decl->pAssignment->Rhs;

        EmitRhs(rhs, RhsMode::RHS_ASSIGN_TO_HANDLE, var_handle);
    }

    return var_handle;
}

void FoxBytecodeCompiler::DoFunctionCall(FoxAstFunctionCall* call)
{
    RETURN_IF_NO_NODE(call);

    FoxBytecodeFunctionHandle* handle = FindFunctionHandle(call->HashedName);

    if (handle == nullptr) {
        CompileError("DoFunctionCall: Undefined reference to function (Hash={})", call->HashedName);
        return;
    }

    EmitParamsStart();

    int32 parameter_index = 0;

    // Fetch all parameters into registers
    for (FoxAstNode* param : call->Params) {
        EmitPushVarOrLiteral(param);
        parameter_index++;
    }


    // If the function is externally defined, emit external jump instead
    if (handle->BytecodeIndex == UINT32_MAX) {
        EmitJumpCallExternal(handle->HashedName);
        return;
    }

    EmitJumpCallAbsolute(handle->HashedName);
}


FoxBytecodeVarHandle* FoxBytecodeCompiler::DefineParam(FoxAstNode* param_decl_node)
{
    if (param_decl_node->NodeType != FX_AST_VARDECL) {
        CompileError("DefineParam: Param node type is not vardecl!");
        return nullptr;
    }

    FoxAstVarDecl* var_decl_node = reinterpret_cast<FoxAstVarDecl*>(param_decl_node);

    // Emit variable without emitting pushes or pops
    FoxBytecodeVarHandle* handle = DoVarDeclare(var_decl_node, DO_NOT_ALLOW_ASSIGNMENT);
    uint32 parameter_var_index = mVariableIndex++;

    EmitVariableDefine(parameter_var_index, var_decl_node->pNameToken->GetHash(), false);

    if (!handle) {
        CompileError("DefineParam: Could not define and fetch param!");
        return nullptr;
    }

    // EmitPopVar(parameter_var_index);

    return handle;
}

FoxBytecodeVarHandle* FoxBytecodeCompiler::DefineReturnVar(FoxAstVarDecl* decl)
{
    RETURN_VALUE_IF_NO_NODE(decl, nullptr);

    return DoVarDeclare(decl);
}

void FoxBytecodeCompiler::EmitFunctionDefinitionsInBlock(FoxAstBlock* block)
{
    if (!block) {
        return;
    }

    for (FoxAstNode* stmt : block->Statements) {
        if (stmt->NodeType == FX_AST_PROCDECL) {
            EmitFunctionDeclaration(reinterpret_cast<FoxAstFunctionDecl*>(stmt));
        }
    }
}

void FoxBytecodeCompiler::EmitSymbolTable(FoxAstBlock* root)
{
    uint32 num_symbols = 0;
    for (FoxAstNode* stmt : root->Statements) {
        if (stmt->NodeType == FX_AST_PROCDECL) {
            ++num_symbols;
        }
    }

    Write32(num_symbols);

    for (FoxAstNode* stmt : root->Statements) {
        if (stmt->NodeType == FX_AST_PROCDECL) {
            FoxAstFunctionDecl* proc_decl = static_cast<FoxAstFunctionDecl*>(stmt);

            EmitDataString(proc_decl->pNameToken->Start, proc_decl->pNameToken->Length);

            proc_decl->SymbolTableOffset = mBytecode.Size();

            // Externally defined
            if (proc_decl->bIsExternal) {
                Write32(UINT32_MAX);
            }
            else {
                // Write a temporary address (will be updated after bytecode is written)
                Write32(0);
            }
        }
    }
}

void FoxBytecodeCompiler::EmitFunctionDeclaration(FoxAstFunctionDecl* function)
{
    RETURN_IF_NO_NODE(function);

    // Do not write any bytecode for an externally defined function. This will be handled by the VM (symbol table
    // offsets are set to UINT32_MAX).
    if (function->bIsExternal) {
        FoxBytecodeFunctionHandle function_handle {
            .HashedName = function->pNameToken->GetHash(),
            .BytecodeIndex = UINT32_MAX,
        };

        FunctionHandles.push_back(function_handle);

        return;
    }

    // Store the bytecode offset before the function is emitted
    const uint32 start_of_function = mBytecode.Size();

    mpCurrentFunctionBody = function;

    // Emit the body of the function
    {
        if (function->pNameToken) {
            if (function->pBlock) {
                EmitMarker(BcSpecMarker_Proc);
            }

            uint32 sym_offset = function->SymbolTableOffset;
            uint32 bc_offset = mBytecode.Size();

            // Overwrite the offset in the symbol table
            // mBytecode[sym_offset] = static_cast<uint8>(bc_offset >> 24);
            // mBytecode[sym_offset + 1] = static_cast<uint8>(bc_offset >> 16);
            // mBytecode[sym_offset + 2] = static_cast<uint8>(bc_offset >> 8);
            // mBytecode[sym_offset + 3] = static_cast<uint8>(bc_offset);

            Fixup32(sym_offset, bc_offset);
        }

        int32 parameter_index = 0;

        uint32 num_parameters = function->pParams->Statements.size();

        for (parameter_index = 0; parameter_index < num_parameters; parameter_index++) {
            FoxAstNode* param_decl_node = function->pParams->Statements[(num_parameters - parameter_index - 1)];
            DefineParam(param_decl_node);
        }
        // Pop values into parameter variables in reverse order
        for (parameter_index = 0; parameter_index < num_parameters; parameter_index++) {
            EmitPopVar(parameter_index);
        }

        EmitBlock(function->pBlock, parameter_index, true);

        // Check to see if there has been a return statement in the function

        if (function->pBlock) {
            bool block_has_return = false;

            for (FoxAstNode* statement : function->pBlock->Statements) {
                if (statement->NodeType == FX_AST_RETURN) {
                    block_has_return = true;
                    break;
                }
            }

            // There is no return statement in the function's block, add a void return statement
            if (!block_has_return) {
                EmitJumpReturnToCaller();
            }

            // End of procedure, end of scope
            EmitMarker(BcSpecMarker_ProcEnd);
        }
    }

    FoxBytecodeFunctionHandle function_handle { .HashedName = function->pNameToken->GetHash(),
                                                .BytecodeIndex = static_cast<uint32>(start_of_function) };

    FunctionHandles.push_back(function_handle);
    mpCurrentFunctionBody = nullptr;
}

void FoxBytecodeCompiler::EmitBlock(FoxAstBlock* block, int params_to_save, bool is_function_body)
{
    RETURN_IF_NO_NODE(block);

    uint16 var_index_before_scope = mVariableIndex;

    if (!is_function_body) {
        // After the stack allocations, mark the start of the frame.
        EmitMarker(BcSpecMarker_FrameBegin);
    }

    // Emit all nodes inside of the scope
    for (FoxAstNode* node : block->Statements) {
        EmitNode(node);
    }

    // Clear all of the scope variables
    mVariableIndex = var_index_before_scope;

    if (!is_function_body) {
        EmitMarker(BcSpecMarker_FrameEnd);
    }
}

void FoxBytecodeCompiler::PrintBytecode()
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

bool FoxBytecodeCompiler::DoesNodeBranch(FoxAstNode* node)
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

        return DoesNodeBranch(vardecl->pAssignment);
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


/////////////////////////////////////
// Bytecode Printer
/////////////////////////////////////

uint16 FoxBytecodePrinter::Read16()
{
    uint8 lo = mpBytecode[mBytecodeIndex++];
    uint8 hi = mpBytecode[mBytecodeIndex++];

    return ((static_cast<uint16>(lo) << 8) | hi);
}
uint16 FoxBytecodePrinter::Read16Rev()
{
    uint8 lo = mpBytecode[mBytecodeIndex++];
    uint8 hi = mpBytecode[mBytecodeIndex++];

    return ((static_cast<uint16>(hi) << 8) | lo);
}

uint32 FoxBytecodePrinter::Read32()
{
    uint16 lo = Read16();
    uint16 hi = Read16();

    return ((static_cast<uint32>(lo) << 16) | hi);
}


void FoxBytecodePrinter::DoPush(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecPush_Int32) {
        uint32 value = Read32();
        BC_PRINT_OP("PUSH [int32] {}", value);
    }
    else if (op_spec == BcSpecPush_Float32) {
        float32 value = std::bit_cast<float32>(Read32());
        BC_PRINT_OP("PUSHF [float32] {:f}", value);
    }
    else if (op_spec == BcSpecPush_Var) {
        VarIndex var = Read16();
        BC_PRINT_OP("VPUSH ${}", var);
    }
}

void FoxBytecodePrinter::DoPop(char* s, uint8 op_base, uint8 op_spec_raw)
{
    if (op_spec_raw == BcSpecPop_Variable_Int32) {
        VarIndex variable_index = Read16();
        BC_PRINT_OP("VPOP [int32] ${}", variable_index);
    }
    else if (op_spec_raw == BcSpecPop_Variable_Float32) {
        VarIndex variable_index = Read16();
        BC_PRINT_OP("VPOPF [float32] ${}", variable_index);
    }
}

void FoxBytecodePrinter::DoArith(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecArith_Add) {
        BC_PRINT_OP("ADD [int32]");
    }
    else if (op_spec == BcSpecArith_Add_Float32) {
        BC_PRINT_OP("ADDF [float32]");
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

    else if (op_spec == BcSpecSave_AbsoluteInt32) {
        const uint32 offset = Read32();
        const uint32 value = Read32();

        BC_PRINT_OP("savea [i32] {}, {}", offset, value);
    }
}

void FoxBytecodePrinter::DoJump(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecJump_Relative) {
        uint16 offset = Read16();
        BC_PRINT_OP("JMPR {}", offset);
    }
    else if (op_spec == BcSpecJump_Equal) {
        uint16 offset = Read16();
        BC_PRINT_OP("JMPEQ {}", offset);
    }
    else if (op_spec == BcSpecJump_Absolute) {
        uint32 position = Read32();
        BC_PRINT_OP("jmpa {}", position);
    }
    else if (op_spec == BcSpecJump_CallAbsolute) {
        uint32 position = Read32();
        BC_PRINT_OP("CALLA {}", position);
    }
    else if (op_spec == BcSpecJump_ReturnToCaller) {
        BC_PRINT_OP("RET");
    }
    else if (op_spec == BcSpecJump_ReturnToCaller_Int32) {
        BC_PRINT_OP("VRET [int32]");
    }
    else if (op_spec == BcSpecJump_ReturnToCaller_Float32) {
        BC_PRINT_OP("VRETF [float32]");
    }
    else if (op_spec == BcSpecJump_CallExternal) {
        uint32 hashed_name = Read32();
        BC_PRINT_OP("CALLEXT {}", hashed_name);
    }
}


void FoxBytecodePrinter::DoData(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecData_String) {
        uint16 length = Read16();
        char* data_str = static_cast<char*>(malloc(length));
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
    else if (op_spec == BcSpecType_Float) {
        BC_PRINT_OP("type float");
    }
    else if (op_spec == BcSpecType_String) {
        BC_PRINT_OP("type str");
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
        BC_PRINT_OP("\nPROC // Off={}", mBytecodeIndex);
    }
    else if (op_spec == BcSpecMarker_ProcEnd) {
        BC_PRINT_OP("PROC END\n");
    }
}


void FoxBytecodePrinter::DoVariable(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecVariable_Set_Int32) {
        uint16 var_index = Read16();
        uint32 value = Read32();
        BC_PRINT_OP("VSET [int32] ${}, {}", var_index, value);
    }
    else if (op_spec == BcSpecVariable_Set_Float32) {
        uint16 var_index = Read16();
        float32 value = std::bit_cast<float32>(Read32());
        BC_PRINT_OP("VSET [float32] ${}, {:f}", var_index, value);
    }
    else if (op_spec == BcSpecVariable_Set_Var) {
        VarIndex dst_index = Read16();
        VarIndex src_index = Read16();

        BC_PRINT_OP("VSET ${}, ${}", dst_index, src_index);
    }
    else if (op_spec == BcSpecVariable_Define) {
        uint16 var_index = Read16();
        Hash32 name_hash = Read32();
        BC_PRINT_OP("VDEFINE {} AS ${}", name_hash, var_index);
    }
    else if (op_spec == BcSpecVariable_Define_Float32) {
        uint16 var_index = Read16();
        Hash32 name_hash = Read32();
        BC_PRINT_OP("VDEFINEF [float32] {} AS ${}", name_hash, var_index);
    }

    else if (op_spec == BcSpecVariable_DefineGlobal) {
        uint16 var_index = Read16();
        Hash32 name_hash = Read32();
        BC_PRINT_OP("VGLOBAL {} AS ${}", name_hash, var_index);
    }
    else if (op_spec == BcSpecVariable_DefineGlobal_Float32) {
        uint16 var_index = Read16();
        Hash32 name_hash = Read32();
        BC_PRINT_OP("VGLOBALF [float32] {} AS ${}", name_hash, var_index);
    }

    else if (op_spec == BcSpecVariable_Cast_Int32) {
        uint16 var_index = Read16();
        BC_PRINT_OP("VCAST [int32] {}", var_index);
    }

    else if (op_spec == BcSpecVariable_Cast_Float32) {
        uint16 var_index = Read16();
        BC_PRINT_OP("VCASTF [float32] {}", var_index);
    }
}

void FoxBytecodePrinter::DoCompare(char* s, uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecCompare_Default) {
        BC_PRINT_OP("CMP");
    }
    else if (op_spec == BcSpecCompare_NotZero) {
        BC_PRINT_OP("CMPNZ");
    }
}


void FoxBytecodePrinter::PrintSymbolTable()
{
    uint32 num_symbols = Read32();

    constexpr uint32 cTempBufferSize = 512;
    char buffer[cTempBufferSize];

    LogInfo("--- Symbols ---");


    for (uint32 index = 0; index < num_symbols; index++) {
        ReadString(buffer, cTempBufferSize);
        uint32 offset = Read32();

        if (offset == UINT32_MAX) {
            LogInfo("{} -> EXTERNAL", buffer);
        }
        else {
            LogInfo("{} -> {}", buffer, offset);
        }
    }

    LogInfo("----------------");
}

void FoxBytecodePrinter::Print()
{
    PrintSymbolTable();

    while (mBytecodeIndex < mpBytecode.Size) {
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
    case BcBase_Marker:
        DoMarker(s, op_base, op_spec);
        break;
    case BcBase_Variable:
        DoVariable(s, op_base, op_spec);
        break;
    case BcBase_Compare:
        DoCompare(s, op_base, op_spec);
        break;
    }
}

} // namespace fx::script
