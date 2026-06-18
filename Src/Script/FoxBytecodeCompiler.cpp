#include "FoxBytecodeCompiler.hpp"

#include "FoxVariable.hpp"

#include <Core/ArrayUtil.hpp>
#include <Core/Log.hpp>
#include <Core/PagedArray.hpp>
#include <Util/Tokenizer.hpp>

#define BC_PRINT_OP(fmt_, ...) fx::Log<fx::eLogSeverity::None>(fmt_, ##__VA_ARGS__)

namespace fx::script {

static constexpr uint32 scBytecodePageSize = 4096;

static constexpr uint16 ReverseInt16(uint16 value) { return (value >> 8) | (value << 8); }

using TT = eTokenType;

SizedArray<uint8> FoxBytecodeCompiler::Compile(FoxAstNode* root)
{
    mBytecode.Create(scBytecodePageSize);
    LocalVarHandles.InitCapacity(64);
    Strings.Create(32);

    Assert(root->NodeType == FX_AST_BLOCK);

    EmitSymbolTable(static_cast<FoxAstBlock*>(root));
    EmitNode(root);
    EmitStrings();

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
            FoxAstFunctionCall* call_node = static_cast<FoxAstFunctionCall*>(return_rhs);

            eFoxType return_type = call_node->GetReturnType();
            eFoxType builtin_call_type = DoBuiltin(call_node);

            if (return_type == eFoxType::NONETYPE) {
                return_type = builtin_call_type;
            }

            if (return_type == eFoxType::NONETYPE) {
                CompileError("EmitReturn: Cannot return a result that is void. The function called in this return "
                             "statement does not return a value.");
                EmitJumpReturnToCaller();
                return;
            }

            if (builtin_call_type == eFoxType::NONETYPE) {
                EmitFunctionCall(call_node, true);
            }

            eFoxType caller_return_type = mpCurrentFunctionBody ? mpCurrentFunctionBody->ReturnType : eFoxType::INT;
            if (caller_return_type != return_type) {
                CompileError("Cannot return type '{}' in a function that returns '{}'", return_type,
                             caller_return_type);

                // No need to return here
            }

            // Function call returns a value and is pushed onto the stack.
            // Continue this value to the next caller.
            if (caller_return_type == eFoxType::INT) {
                EmitJumpReturnToCallerInt32();
            }
            else if (caller_return_type == eFoxType::FLOAT) {
                EmitJumpReturnToCallerFloat32();
            }
            else if (caller_return_type == eFoxType::STRING) {
                EmitJumpReturnToCallerString();
            }
            else {
                CompileError("Unknown return type {}!", static_cast<uint32>(caller_return_type));
            }
        }
        else {
            eFoxType return_type = mpCurrentFunctionBody ? mpCurrentFunctionBody->ReturnType : eFoxType::INT;

            EmitPushVarOrLiteral(return_rhs);

            if (return_type == eFoxType::INT) {
                EmitJumpReturnToCallerInt32();
            }
            else if (return_type == eFoxType::FLOAT) {
                EmitJumpReturnToCallerFloat32();
            }
            else if (return_type == eFoxType::STRING) {
                EmitJumpReturnToCallerString();
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
        case eTokenType::NotEqual:
            return eFoxConditionResult::NotEqual;
        case eTokenType::LessThan:
            return eFoxConditionResult::Less;
        case eTokenType::LessEqual:
            return eFoxConditionResult::LessEqual;
        case eTokenType::GreaterThan:
            return eFoxConditionResult::Greater;
        case eTokenType::GreaterEqual:
            return eFoxConditionResult::GreaterEqual;
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

    return eFoxConditionResult::Equal;
}


void FoxBytecodeCompiler::EmitIfStatement(FoxAstIf* if_node)
{
    eFoxConditionResult cond_result = EmitPushConditionResult(if_node->pCondition);
    // Jump over else block jump if the condition is true
    EmitJumpConditional(sizeof(uint16), cond_result);

    // Else block jump
    EmitJumpRelative(0);

    uint32 original_offset = mBytecode.Size();
    uint32 fixup_addr = mBytecode.Size() - sizeof(uint16);

    EmitNode(if_node->pBlock);

    // Fixup else block address after emitting if block
    Fixup16(fixup_addr, mBytecode.Size() - original_offset);

    if (if_node->pElseBlock) {
        EmitNode(if_node->pElseBlock);
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
        FoxAstFunctionCall* call = static_cast<FoxAstFunctionCall*>(node);
        if (DoBuiltin(call) != eFoxType::NONETYPE) {
            return;
        }

        return EmitFunctionCall(call, false);
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

FoxBytecodeVarHandle* FoxBytecodeCompiler::FindGlobalHandle(Hash32 hashed_name) const
{
    for (FoxBytecodeVarHandle& handle : GlobalHandles) {
        if (handle.HashedName == hashed_name) {
            return &handle;
        }
    }

    return nullptr;
}


FoxBytecodeVarHandle* FoxBytecodeCompiler::FindVarHandle(Hash32 hashed_name) const
{
    for (FoxBytecodeVarHandle& handle : LocalVarHandles) {
        if (handle.HashedName == hashed_name) {
            return &handle;
        }
    }
    return nullptr;
}

VarIndex FoxBytecodeCompiler::FindVarInScope(Hash32 hashed_name) const
{
    // TODO: add back scope checks here
    for (const FoxBytecodeVarHandle& handle : LocalVarHandles) {
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

void FoxBytecodeCompiler::EmitPushString(uint32 value)
{
    // PUSH32F [f32]
    WriteOp(BcBase_Push, BcSpecPush_String);
    Write32(value);
}

void FoxBytecodeCompiler::EmitPushVar(VarIndex var)
{
    // VPUSH [%var]
    WriteOp(BcBase_Push, BcSpecPush_Var);
    Write16(var);
}

void FoxBytecodeCompiler::EmitPushReturnAddr() { WriteOp(BcBase_Push, BcSpecPush_ReturnAddr); }

eFoxType FoxBytecodeCompiler::EmitPushVarOrLiteral(FoxAstNode* node)
{
    if (node->NodeType == FX_AST_LITERAL) {
        FoxAstLiteral* literal = static_cast<FoxAstLiteral*>(node);

        // Check if its a reference to another variable.
        if (literal->Value.IsRef()) {
            FoxAstVarRef* ref = literal->Value.pValueRef;

            // If the variable is not defined in scope, we should check to see if its a global.
            // Globals need to be hoisted up to the current scope.

            FoxBytecodeVarHandle* local_handle = FindVarHandle(ref->GetNameHash());

            // If the variable is not defined locally but there is a global entry for the variable, use that and hoist
            // the variable up to scope.
            if (local_handle == nullptr) {
                FoxBytecodeVarHandle* global_handle = FindGlobalHandle(ref->GetNameHash());

                // This is not a global variable and is undefined.
                if (!global_handle) {
                    CompileError("Cannot find variable {} (Hash={})", ref->pName->GetStr(), ref->GetNameHash());
                    return eFoxType::NONETYPE;
                }

                // A global handle does exist, hoist it up.
                EmitVariableGlobalHoist(global_handle, ref->GetNameHash());
            }
        }

        if (literal->Value.Type == eFoxType::INT) {
            EmitPush32(literal->Value.ValueInt);
        }
        else if (literal->Value.Type == eFoxType::REF) {
            EmitPushVar(FindVarInScope(literal->Value.pValueRef->pName->GetHash()));

            // Get the type of the variable
            FoxBytecodeVarHandle* vhandle = FindVarHandle(literal->Value.pValueRef->pName->GetHash());

            if (vhandle) {
                LogInfo("Handle type: {}", static_cast<int32>(vhandle->Type));
                return vhandle->Type;
            }

            return eFoxType::NONETYPE;
        }
        else if (literal->Value.Type == eFoxType::FLOAT) {
            EmitPushFloat32(literal->Value.ValueFloat);
        }
        else if (literal->Value.Type == eFoxType::STRING) {
            // Add fixup for push, this will be update to the offset in the string data.
            EmitPushString(0);
            AddString(mBytecode.Size() - 4, literal->Value.ValueString);
        }

        return literal->Value.Type;
    }
    else if (node->NodeType == FX_AST_BINOP) {
        FoxAstBinop* binop = static_cast<FoxAstBinop*>(node);
        EmitBinop(binop);
        return GetVarOrLiteralType(binop->pLeft);
    }
    else if (node->NodeType == FX_AST_PROCCALL) {
        FoxAstFunctionCall* call = static_cast<FoxAstFunctionCall*>(node);

        eFoxType builtin_call_rvalue = DoBuiltin(call);
        if (builtin_call_rvalue != eFoxType::NONETYPE) {
            return builtin_call_rvalue;
        }

        EmitFunctionCall(call, true);
        return call->GetReturnType();
    }
    else {
        CompileError("EmitPushVarOrLiteral: Unknown node type");
    }

    return eFoxType::NONETYPE;
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

void FoxBytecodeCompiler::EmitPopDiscard()
{
    // VPOP
    WriteOp(BcBase_Pop, BcSpecPop_Discard);
}

void FoxBytecodeCompiler::EmitPopReturnAddr() { WriteOp(BcBase_Pop, BcSpecPop_ReturnAddr); }


void FoxBytecodeCompiler::EmitJumpRelative(uint16 offset)
{
    WriteOp(BcBase_Jump, BcSpecJump_Relative);
    Write16(offset);
}

void FoxBytecodeCompiler::EmitJumpConditional(uint16 offset, eFoxConditionResult cond)
{
    BcSpecJump cond_spec = BcSpecJump_Equal;

    switch (cond) {
    case eFoxConditionResult::Less:
        cond_spec = BcSpecJump_Less;
        break;
    case eFoxConditionResult::LessEqual:
        cond_spec = BcSpecJump_LessEqual;
        break;
    case eFoxConditionResult::Equal:
        cond_spec = BcSpecJump_Equal;
        break;
    case eFoxConditionResult::NotEqual:
        cond_spec = BcSpecJump_NotEqual;
        break;
    case eFoxConditionResult::Greater:
        cond_spec = BcSpecJump_Greater;
        break;
    case eFoxConditionResult::GreaterEqual:
        cond_spec = BcSpecJump_GreaterEqual;
        break;
    default:;
    }

    WriteOp(BcBase_Jump, cond_spec);
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
void FoxBytecodeCompiler::EmitJumpReturnToCallerString() { WriteOp(BcBase_Jump, BcSpecJump_ReturnToCaller_String); }

void FoxBytecodeCompiler::EmitJumpPause(uint16 time)
{
    WriteOp(BcBase_Jump, BcSpecJump_Pause);
    Write16(time);
}


void FoxBytecodeCompiler::EmitVariableSetInt32(uint16 var_index, int32 value)
{
    WriteOp(BcBase_Variable, BcSpecVariable_Set_Int32);
    Write16(var_index);
    Write32(value);
}

void FoxBytecodeCompiler::EmitVariableSetString(uint16 var_index)
{
    WriteOp(BcBase_Variable, BcSpecVariable_Set_String);
    Write16(var_index);
    Write32(0);
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
    WriteOp(BcBase_Variable, is_global ? BcSpecVariable_DefineGlobal : BcSpecVariable_Define);
    Write16(var_index);

    if (is_global) {
        Write32(name_hash);
    }
}

void FoxBytecodeCompiler::EmitVariableDefineFetchParam(eFoxType type, uint16 var_index)
{
    BcSpecVariable spec;

    switch (type) {
    case eFoxType::INT:
        spec = BcSpecVariable_DefineFetchParam_Int32;
        break;
    case eFoxType::FLOAT:
        spec = BcSpecVariable_DefineFetchParam_Float32;
        break;
    case eFoxType::STRING:
        spec = BcSpecVariable_DefineFetchParam_String;
        break;
    default:
        spec = BcSpecVariable_DefineFetchParam_Int32;
    }

    WriteOp(BcBase_Variable, spec);
    Write16(var_index);
}


void FoxBytecodeCompiler::EmitVariableIndex(uint16 var_index) { Write16(var_index); }

void FoxBytecodeCompiler::EmitVariableCastInt32() { WriteOp(BcBase_Variable, BcSpecVariable_Cast_Int32); }
void FoxBytecodeCompiler::EmitVariableCastFloat32() { WriteOp(BcBase_Variable, BcSpecVariable_Cast_Float32); }

void FoxBytecodeCompiler::EmitVariableGlobalHoist(FoxBytecodeVarHandle* handle, Hash32 name_hash)
{
    // Emit the local definition
    EmitVariableDefine(mVariableIndex, name_hash, true);

    // Create a new handle (for local)
    FoxBytecodeVarHandle local_handle = *handle;
    local_handle.VariableIndex = mVariableIndex;

    LocalVarHandles.Insert(local_handle);

    ++mVariableIndex;
}


void FoxBytecodeCompiler::EmitCompare() { WriteOp(BcBase_Compare, BcSpecCompare_Default); }
void FoxBytecodeCompiler::EmitCompareNotZero() { WriteOp(BcBase_Compare, BcSpecCompare_NotZero); }


void FoxBytecodeCompiler::EmitType(eFoxType type)
{
    BcSpecType op_type = BcSpecType_Int;

    if (type == eFoxType::STRING) {
        op_type = BcSpecType_String;
    }

    WriteOp(BcBase_Type, op_type);
}

uint32 FoxBytecodeCompiler::EmitDataString(const char* str, uint16 length, bool emit_length_prefix)
{
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

    if (emit_length_prefix) {
        Write16(final_length);
    }

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
    // Push LHS and RHS to the stack
    eFoxType left_type = EmitPushVarOrLiteral(binop->pLeft);
    eFoxType right_type = EmitPushVarOrLiteral(binop->pRight);

    if (left_type != right_type) {
        CompileError("Mismatched type: Cannot perform operation on '{}' and '{}'", left_type, right_type);
        return;
    }

    const bool is_int = (left_type == eFoxType::INT);

    if (binop->OpToken->Type == TT::Plus) {
        WriteOp(BcBase_Arith, (is_int) ? BcSpecArith_Add_Int32 : BcSpecArith_Add_Float32);
    }
    else if (binop->OpToken->Type == TT::Asterisk) {
        WriteOp(BcBase_Arith, (is_int) ? BcSpecArith_Multiply_Int32 : BcSpecArith_Multiply_Float32);
    }
}


uint16 FoxBytecodeCompiler::GetSizeOfType(Token* token)
{
    const Hash32 type_hash = token->GetHash();

    constexpr Hash32 type_int_hash = HashStr32("int");
    constexpr Hash32 type_float_hash = HashStr32("float");
    constexpr Hash32 type_str_hash = HashStr32("str");

    if (type_hash == type_int_hash) {
        return sizeof(int32);
    }
    else if (type_hash == type_float_hash) {
        return sizeof(float32);
    }
    else if (type_hash == type_str_hash) {
        return sizeof(int32);
    }
    else {
        CompileError("GetSizeOfType: Unknown type");
    }

    return 0;
}


void FoxBytecodeCompiler::EmitAssign(FoxAstAssign* assign)
{
    FoxBytecodeVarHandle* var_handle = FindVarHandle(assign->pLhs->pName->GetHash());
    if (var_handle == nullptr) {
        CompileError("Var '{:.{}}' does not exist!", assign->pLhs->pName->Start, assign->pLhs->pName->Length);
        return;
    }

    if (!var_handle) {
        CompileError("Could not find var handle to assign to!");
        return;
    }

    EmitRhs(assign->pRhs, RhsMode::RHS_ASSIGN_TO_HANDLE, var_handle);
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

void FoxBytecodeCompiler::AddString(uint32 fixup_offset, const String& value)
{
    FoxBytecodeString* str = Strings.Insert();
    str->StringValue = value;
    str->FixupOffset = fixup_offset;

    LogInfo(LC_SCRIPT, "Adding string {} at offset {}", value, fixup_offset);
}

eFoxType FoxBytecodeCompiler::GetLiteralUnderlyingType(FoxAstLiteral* literal) const
{
    eFoxType underlying_type = literal->Value.Type;

    if (underlying_type == eFoxType::REF) {
        FoxBytecodeVarHandle* var_handle = FindVarHandle(literal->Value.pValueRef->GetNameHash());
        if (var_handle != nullptr) {
            underlying_type = var_handle->Type;
        }
    }

    return underlying_type;
}

void FoxBytecodeCompiler::EmitRhs(FoxAstNode* rhs, FoxBytecodeCompiler::RhsMode mode, FoxBytecodeVarHandle* handle)
{
    if (rhs->NodeType == FX_AST_LITERAL) {
        FoxAstLiteral* literal = static_cast<FoxAstLiteral*>(rhs);

        // Check if its a reference to another variable.
        if (literal->Value.IsRef()) {
            FoxAstVarRef* ref = literal->Value.pValueRef;

            // If the variable is not defined in scope, we should check to see if its a global.
            // Globals need to be hoisted up to the current scope.

            FoxBytecodeVarHandle* local_handle = FindVarHandle(ref->GetNameHash());

            // If the variable is not defined locally but there is a global entry for the variable, use that and hoist
            // the variable up to scope.
            if (local_handle == nullptr) {
                FoxBytecodeVarHandle* global_handle = FindGlobalHandle(ref->GetNameHash());

                // This is not a global variable and is undefined.
                if (!global_handle) {
                    CompileError("Cannot find variable {} (Hash={})", ref->pName->GetStr(), ref->GetNameHash());
                    return;
                }

                // A global handle does exist, hoist it up.
                EmitVariableGlobalHoist(global_handle, ref->GetNameHash());
            }
        }


        // Check to see if the rhs type matches the variable we are assigning to. We can get the underlying type, as we
        // want to make sure the type is correct when using refs as well.
        if (GetLiteralUnderlyingType(literal) != handle->Type) {
            CompileError("Type mismatch: assigning type '{}' to variable of type '{}'", literal->Value.Type,
                         handle->Type);
        }

        switch (literal->Value.Type) {
        case eFoxType::NONETYPE:
            break;
        case eFoxType::INT:
            EmitVariableSetInt32(handle->VariableIndex, literal->Value.ValueInt);
            break;
        case eFoxType::FLOAT:
            EmitVariableSetFloat32(handle->VariableIndex, literal->Value.ValueFloat);
            break;
        case eFoxType::STRING:
            EmitVariableSetString(handle->VariableIndex);
            AddString(mBytecode.Size() - sizeof(uint32), literal->Value.ValueString);
            break;
        case eFoxType::REF:
            EmitVariableSetVar(handle->VariableIndex, FindVarInScope(literal->Value.pValueRef->pName->GetHash()));
            break;
        }
    }
    else if (rhs->NodeType == FX_AST_PROCCALL || rhs->NodeType == FX_AST_BINOP) {
        if (rhs->NodeType == FX_AST_BINOP) {
            FoxAstBinop* binop = static_cast<FoxAstBinop*>(rhs);
            EmitBinop(binop);
        }

        else if (rhs->NodeType == FX_AST_PROCCALL) {
            FoxAstFunctionCall* call = static_cast<FoxAstFunctionCall*>(rhs);

            bool is_builtin_call = DoBuiltin(call) != eFoxType::NONETYPE;

            if (!is_builtin_call) {
                if (call->GetReturnType() != handle->Type) {
                    CompileError("Type mismatch: assigning function return type '{}' to variable of type '{}'",
                                 call->GetReturnType(), handle->Type);
                }
                EmitFunctionCall(call, true);
            }
        }

        // Pop the newly pushed value to the variable
        EmitPopVar(handle->VariableIndex);
    }
}

FoxBytecodeVarHandle* FoxBytecodeCompiler::DoVarDeclare(FoxAstVarDecl* decl, VarDeclareMode mode)
{
    RETURN_VALUE_IF_NO_NODE(decl, nullptr);


    const Hash32 type_int = HashStr32("int");
    const Hash32 type_float = HashStr32("float");
    const Hash32 type_string = HashStr32("str");

    Hash32 decl_hash = decl->pNameToken->GetHash();

    FoxBytecodeVarHandle* existing_var = FindVarHandle(decl_hash);
    if (existing_var) {
        CompileError("Redefinition of variable '{}' from '{}' to '{}'", decl->pNameToken->GetStr(), existing_var->Type,
                     decl->pTypeToken->GetStr());
        return existing_var;
    }

    eFoxType var_type = FoxStringToType(decl->pTypeToken);
    const uint16 size_of_type = GetSizeOfType(decl->pTypeToken);

    if (mode == VarDeclareMode::DECLARE_PARAMETER) {
        EmitVariableDefineFetchParam(var_type, mVariableIndex);
    }
    else {
        EmitVariableDefine(mVariableIndex, decl->pNameToken->GetHash(), decl->bDefineAsGlobal);
    }

    FoxBytecodeVarHandle handle {
        .HashedName = decl_hash,
        .Type = var_type,
        .Offset = 0,
        .ScopeIndex = mScopeIndex,
        .VariableIndex = mVariableIndex,
        .SizeOnStack = size_of_type,
    };

    LocalVarHandles.Insert(handle);
    ++mVariableIndex;

    if (decl->bDefineAsGlobal) {
        GlobalHandles.Insert(handle);
    }

    FoxBytecodeVarHandle* var_handle = FindVarHandle(decl_hash);

    if (var_handle == nullptr) {
        CompileError("Could not find variable handle!");
        return nullptr;
    }

    if (mode == VarDeclareMode::DO_NOT_ALLOW_ASSIGNMENT || mode == VarDeclareMode::DECLARE_PARAMETER) {
        return var_handle;
    }

    if (decl->pAssignment) {
        FoxAstNode* rhs = decl->pAssignment->pRhs;

        EmitRhs(rhs, RhsMode::RHS_ASSIGN_TO_HANDLE, var_handle);
    }

    return var_handle;
}

void FoxBytecodeCompiler::ValidateParameters(FoxAstFunctionCall* call)
{
    if (call->pFunction == nullptr || call->pFunction->pDeclaration == nullptr) {
        CompileError("Attempting to call undefined function");
        return;
    }

    // Get the parameter definitions for the function declaration
    const std::vector<FoxAstNode*>& param_decl_stmts = call->pFunction->pDeclaration->pParams->Statements;

    bool has_correct_param_count = call->Params.size() == param_decl_stmts.size();

    if (call->pFunction->pDeclaration->bIsVariadic) {
        has_correct_param_count = (param_decl_stmts.size() <= call->Params.size());
    }

    // Check to make sure there are the correct number of arguments
    if (!has_correct_param_count) {
        CompileError("Expected {} parameters but only {} were passed in", param_decl_stmts.size(), call->Params.size());
        return;
    }

    // Check the argument types match
    for (int32 i = 0; i < call->Params.size(); i++) {
        if (i >= param_decl_stmts.size()) {
            break;
        }

        eFoxType type = GetVarOrLiteralType(call->Params[i]);
        FoxAstVarDecl* decl = static_cast<FoxAstVarDecl*>(param_decl_stmts[i]);

        if (type != decl->Type) {
            CompileError("Paremeter type {} does not match {}", type, decl->Type);
        }
    }
}

#define BUILTIN_REQUIRE_PARAM_N(num_)                                                                                  \
    if (call->Params.size() != num_) {                                                                                 \
        CompileError("Not enough parameters for builtin function!");                                                   \
        return eFoxType::NONETYPE;                                                                                     \
    }

eFoxType FoxBytecodeCompiler::DoBuiltin(FoxAstFunctionCall* call)
{
    static constexpr Hash32 scCastInt = HashStr32("castint");
    static constexpr Hash32 scCastFloat = HashStr32("castfloat");
    static constexpr Hash32 scPause = HashStr32("pause");
    static constexpr Hash32 scVPush = HashStr32("vpush");
    static constexpr Hash32 scVPop = HashStr32("vpop");

    static constexpr Hash32 scVRetI = HashStr32("vreti");
    static constexpr Hash32 scVRetF = HashStr32("vretf");
    static constexpr Hash32 scVRetS = HashStr32("vrets");

    switch (call->HashedName) {
    case scCastInt: {
        BUILTIN_REQUIRE_PARAM_N(1);

        eFoxType vtype = EmitPushVarOrLiteral(call->Params[0]);

        if (vtype != eFoxType::INT) {
            EmitVariableCastInt32();
        }

        // The cast builtins are transformation operations, so they pop the value off of the stack and push the
        // converted value back onto it. If the type is already an int, preserve the original value by doing nothing to
        // avoid mangling the value with casts.

        return eFoxType::INT;
    }
    case scCastFloat: {
        BUILTIN_REQUIRE_PARAM_N(1);

        eFoxType vtype = EmitPushVarOrLiteral(call->Params[0]);

        if (vtype != eFoxType::FLOAT) {
            EmitVariableCastFloat32();
        }

        return eFoxType::FLOAT;
    }

    case scVPush: {
        BUILTIN_REQUIRE_PARAM_N(1);
        return EmitPushVarOrLiteral(call->Params[0]);
    }

    case scVPop: {
        BUILTIN_REQUIRE_PARAM_N(1);
        FoxAstLiteral* lit = static_cast<FoxAstLiteral*>(call->Params[0]);
        if (lit->NodeType != FX_AST_LITERAL && !lit->Value.IsRef()) {
            CompileError("Call to `vpop(var)` must have an output variable passed in");
            return eFoxType::NONETYPE;
        }

        FoxBytecodeVarHandle* vh = FindVarHandle(lit->Value.pValueRef->pName->GetHash());
        if (vh == nullptr) {
            CompileError("Cannot find variable to pop into!");
            return eFoxType::NONETYPE;
        }

        EmitPopVar(vh->VariableIndex);

        return vh->Type;
    }
    case scPause: {
        BUILTIN_REQUIRE_PARAM_N(1);
        FoxAstLiteral* lit = static_cast<FoxAstLiteral*>(call->Params[0]);
        if (lit->NodeType != FX_AST_LITERAL && lit->Value.Type != eFoxType::INT) {
            CompileError("Call to `pause(time)` must be provided an integer time");
            return eFoxType::NONETYPE;
        }

        EmitJumpPause(lit->Value.ValueInt);

        return eFoxType::INT;
    }
    case scVRetI: {
        mpCurrentFunctionBody->pBlock->bHasExplicitReturn = true;
        EmitJumpReturnToCallerInt32();
        return eFoxType::INT;
    }
    case scVRetF: {
        mpCurrentFunctionBody->pBlock->bHasExplicitReturn = true;
        EmitJumpReturnToCallerFloat32();
        return eFoxType::FLOAT;
    }
    case scVRetS: {
        mpCurrentFunctionBody->pBlock->bHasExplicitReturn = true;
        EmitJumpReturnToCallerString();
        return eFoxType::STRING;
    }
    default:;
    }


    return eFoxType::NONETYPE;
}


void FoxBytecodeCompiler::EmitFunctionCall(FoxAstFunctionCall* call, bool preserve_return_value)
{
    RETURN_IF_NO_NODE(call);

    FoxBytecodeFunctionHandle* handle = FindFunctionHandle(call->HashedName);

    ValidateParameters(call);

    // if (handle == nullptr) {
    //     CompileError("DoFunctionCall: Undefined reference to function (Hash={})", call->HashedName);
    //     return;
    // }


    int32 parameter_index = 0;

    // Fetch all parameters into registers
    for (FoxAstNode* param : call->Params) {
        EmitPushVarOrLiteral(param);
        parameter_index++;
    }


    if (call->pFunction && call->pFunction->pDeclaration->bIsVariadic) {
        EmitPush32(call->Params.size());
    }

    // If the function is externally defined, emit external jump instead
    if (handle && handle->BytecodeIndex == UINT32_MAX) {
        EmitJumpCallExternal(call->HashedName);
        return;
    }

    EmitJumpCallAbsolute(call->HashedName);

    // If there is no consumer for the return value, emit a discard instruction
    if (!preserve_return_value && call->HasReturnType()) {
        EmitPopDiscard();
    }
}


FoxBytecodeVarHandle* FoxBytecodeCompiler::DefineParam(FoxAstNode* param_decl_node)
{
    if (param_decl_node->NodeType != FX_AST_VARDECL) {
        CompileError("DefineParam: Param node type is not vardecl!");
        return nullptr;
    }

    FoxAstVarDecl* var_decl_node = static_cast<FoxAstVarDecl*>(param_decl_node);

    // Emit variable without emitting pushes or pops
    FoxBytecodeVarHandle* handle = DoVarDeclare(var_decl_node, VarDeclareMode::DECLARE_PARAMETER);

    if (!handle) {
        CompileError("DefineParam: Could not define and fetch param!");
        return nullptr;
    }

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
            EmitFunctionDeclaration(static_cast<FoxAstFunctionDecl*>(stmt));
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

    // Number of symbols
    Write32(num_symbols);

    // String table offset
    Write32(0);

    for (FoxAstNode* stmt : root->Statements) {
        if (stmt->NodeType == FX_AST_PROCDECL) {
            FoxAstFunctionDecl* proc_decl = static_cast<FoxAstFunctionDecl*>(stmt);

            EmitDataString(proc_decl->pNameToken->Start, proc_decl->pNameToken->Length, true);

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

void FoxBytecodeCompiler::EmitStrings()
{
    EmitMarker(BcSpecMarker_StringsBegin);

    uint32 sto = mBytecode.Size();

    // Fixup strings offset in symbol table
    Fixup32(4, sto);

    for (const FoxBytecodeString& str : Strings) {
        Fixup32(str.FixupOffset, mBytecode.Size() - sto);
        EmitDataString(str.StringValue.CStr(), str.StringValue.GetLength(), false);
    }

    EmitMarker(BcSpecMarker_StringsEnd);
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

            Fixup32(sym_offset, bc_offset);
        }

        int32 parameter_index = 0;

        uint32 num_parameters = function->pParams->Statements.size();

        for (parameter_index = 0; parameter_index < num_parameters; parameter_index++) {
            FoxAstNode* param_decl_node = function->pParams->Statements[(num_parameters - parameter_index - 1)];
            DefineParam(param_decl_node);
        }
        // Pop values into parameter variables in reverse order
        // for (parameter_index = 0; parameter_index < num_parameters; parameter_index++) {
        //     EmitPopVar(parameter_index);
        // }

        EmitBlock(function->pBlock, num_parameters, true);

        // Check to see if there has been a return statement in the function

        if (function->pBlock) {
            // There is no return statement in the function's block, add a void return statement
            if (!function->pBlock->bHasExplicitReturn) {
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

void FoxBytecodeCompiler::EmitBlock(FoxAstBlock* block, int num_parameters, bool is_function_body)
{
    RETURN_IF_NO_NODE(block);


    const uint16 var_index_before_scope = mVariableIndex;

    // Frame markers were originally added for hints to the native compiler when this language used an abstract IR
    // instance of a bytecode. This isn't for functional purposed anymore, and is more just for readability and
    // debugging tools.
    if (!is_function_body && mbEmitDebugInfo) {
        EmitMarker(BcSpecMarker_FrameBegin);
    }


    // Emit all nodes inside of the scope
    for (FoxAstNode* node : block->Statements) {
        EmitNode(node);
    }

    // Delete all variables allocated in the block + parameters
    {
        const uint32 num_vars_to_delete = num_parameters + (mVariableIndex - var_index_before_scope);

        LogInfo(LC_SCRIPT, "{} Vars on entry, {} Vars on exit, {} parameters -> {} Vars to delete",
                var_index_before_scope, mVariableIndex, num_parameters, num_vars_to_delete);

        for (int32 i = 0; i < num_vars_to_delete; i++) {
            LocalVarHandles.RemoveLast();
        }

        mVariableIndex -= num_vars_to_delete;
    }

    if (!is_function_body && mbEmitDebugInfo) {
        EmitMarker(BcSpecMarker_FrameEnd);
    }

    ++mScopeIndex;
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
    else if (op_spec == BcSpecPush_String) {
        uint32 offset = Read32();
        BC_PRINT_OP("PUSHS [str] {}", offset);
    }
    else if (op_spec == BcSpecPush_Var) {
        VarIndex var = Read16();
        BC_PRINT_OP("VPUSH ${}", var);
    }
    else if (op_spec == BcSpecPush_ReturnAddr) {
        BC_PRINT_OP("PUSHRA");
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
    else if (op_spec_raw == BcSpecPop_Discard) {
        BC_PRINT_OP("DISCARD");
    }
    else if (op_spec_raw == BcSpecPop_ReturnAddr) {
        BC_PRINT_OP("POPRA");
    }
}

void FoxBytecodePrinter::DoArith(char* s, uint8 op_base, uint8 op_spec)
{
    switch (op_spec) {
    case BcSpecArith_Add_Int32:
        BC_PRINT_OP("ADD [int32]");
        break;
    case BcSpecArith_Add_Float32:
        BC_PRINT_OP("ADDF [float32]");
        break;
    case BcSpecArith_Multiply_Int32:
        BC_PRINT_OP("MUL [int32]");
        break;
    case BcSpecArith_Multiply_Float32:
        BC_PRINT_OP("MULF [float32]");
        break;
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
    else if (op_spec == BcSpecJump_NotEqual) {
        uint16 offset = Read16();
        BC_PRINT_OP("JMPNEQ {}", offset);
    }
    else if (op_spec == BcSpecJump_Less) {
        uint16 offset = Read16();
        BC_PRINT_OP("JMPLT {}", offset);
    }
    else if (op_spec == BcSpecJump_LessEqual) {
        uint16 offset = Read16();
        BC_PRINT_OP("JMPLTEQ {}", offset);
    }
    else if (op_spec == BcSpecJump_Greater) {
        uint16 offset = Read16();
        BC_PRINT_OP("JMPGT {}", offset);
    }
    else if (op_spec == BcSpecJump_GreaterEqual) {
        uint16 offset = Read16();
        BC_PRINT_OP("JMPGTEQ {}", offset);
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
    else if (op_spec == BcSpecJump_ReturnToCaller_String) {
        BC_PRINT_OP("VRETS [str]");
    }
    else if (op_spec == BcSpecJump_Pause) {
        uint16 value = Read16();
        BC_PRINT_OP("PAUSE {}", value);
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


char* FoxBytecodePrinter::ReadString(char* buffer, uint32 buffer_size, bool has_prefixed_length)
{
    if (!has_prefixed_length) {
        uint16* u16_buffer = reinterpret_cast<uint16*>(buffer);
        for (int index = 0;; index += sizeof(uint16)) {
            uint16 value = Read16Rev();
            (*u16_buffer) = value;
            // Break if the rightmost byte is zero
            if ((value & 0xFF00) == 0) {
                break;
            }
            u16_buffer++;
        }

        return buffer;
    }


    uint32 string_length = Read16();

    if (string_length > buffer_size) {
        LogWarning(LC_SCRIPT, "ReadString: String length is greater than the read buffer size! ({} > {})",
                   string_length, buffer_size);
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
    char string_buffer[256];

    switch (op_spec) {
    case BcSpecVariable_Set_Int32: {
        uint16 var_index = Read16();
        uint32 value = Read32();
        BC_PRINT_OP("VSET [int32] ${}, {}", var_index, value);
    } break;

    case BcSpecVariable_Set_Float32: {
        uint16 var_index = Read16();
        float32 value = std::bit_cast<float32>(Read32());
        BC_PRINT_OP("VSET [float32] ${}, {:f}", var_index, value);

    } break;

    case BcSpecVariable_Set_String: {
        uint16 var_index = Read16();
        uint32 value = Read32();

        uint32 old_index = mBytecodeIndex;
        mBytecodeIndex = mStringTableOffset;
        BC_PRINT_OP("VSET [str] ${}, \"{}\"", var_index, ReadString(string_buffer, 256, false));
        mBytecodeIndex = old_index;
    } break;

    case BcSpecVariable_Set_Var: {
        VarIndex dst_index = Read16();
        VarIndex src_index = Read16();

        BC_PRINT_OP("VSET ${}, ${}", dst_index, src_index);
    } break;

    case BcSpecVariable_Define: {
        uint16 var_index = Read16();
        BC_PRINT_OP("VDEFINE ${}", var_index);
    } break;

    case BcSpecVariable_Define_Float32: {
        uint16 var_index = Read16();
        BC_PRINT_OP("VDEFINEF [float32] ${}", var_index);
    } break;

    case BcSpecVariable_DefineGlobal: {
        uint16 var_index = Read16();
        Hash32 name_hash = Read32();
        BC_PRINT_OP("VGLOBAL ${} AS {}", var_index, name_hash);
    } break;

    case BcSpecVariable_DefineGlobal_Float32: {
        uint16 var_index = Read16();
        Hash32 name_hash = Read32();
        BC_PRINT_OP("VGLOBALF [float32] ${} AS {}", var_index, name_hash);
    } break;


    case BcSpecVariable_DefineFetchParam_Int32: {
        uint16 var_index = Read16();
        BC_PRINT_OP("VPARAM [int32] ${}", var_index);
    } break;

    case BcSpecVariable_DefineFetchParam_Float32: {
        uint16 var_index = Read16();
        BC_PRINT_OP("VPARAMF [float32] ${}", var_index);
    } break;

    case BcSpecVariable_DefineFetchParam_String: {
        uint16 var_index = Read16();
        BC_PRINT_OP("VPARAMS [str] ${}", var_index);
    } break;

    case BcSpecVariable_Cast_Int32:
        BC_PRINT_OP("VCAST [int32]");
        break;

    case BcSpecVariable_Cast_Float32:
        BC_PRINT_OP("VCASTF [float32]");
        break;
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


void FoxBytecodePrinter::LoadSymbolTable()
{
    uint32 num_symbols = Read32();

    mStringTableOffset = Read32();

    constexpr uint32 cTempBufferSize = 512;
    char buffer[cTempBufferSize];

    LogInfo("--- Symbols ---");


    for (uint32 index = 0; index < num_symbols; index++) {
        ReadString(buffer, cTempBufferSize, true);
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
    LoadSymbolTable();

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
