#include "FoxVM.hpp"

#include "FoxBytecode.hpp"
#include "FoxBytecodeCompiler.hpp"

namespace fx::script {

char* FoxVM::ReadString(char* buffer, uint32 buffer_size)
{
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


void FoxVM::LoadSymTable()
{
    uint32 num_symbols = Read32();
    mStringsOffset = Read32();

    LogInfo("Loaded {} symbols", num_symbols);

    SymTable.InitSize(num_symbols);

    constexpr uint32 scBufferSize = 512;
    char name_buffer[scBufferSize];

    for (uint32 index = 0; index < num_symbols; index++) {
        ReadString(name_buffer, scBufferSize);

        FoxSymbol& sym = SymTable[index];

        sym.Symbol = Name(name_buffer);
        sym.Offset = Read32();
    }
}

void FoxVM::InitVM(SizedArray<uint8>&& bytecode)
{
    mBytecode = std::move(bytecode);

    LoadSymTable();

    Stack = gScriptMemPool->Alloc<uint8>(scStackSize);
    pVariables = gScriptMemPool->Alloc<VMVariable>(sizeof(VMVariable) * 32);
}


uint16 FoxVM::Read16()
{
    uint8 lo = mBytecode[PC++];
    uint8 hi = mBytecode[PC++];

    return ((static_cast<uint16>(lo) << 8) | hi);
}

uint16 FoxVM::Read16Rev()
{
    uint8 lo = mBytecode[PC++];
    uint8 hi = mBytecode[PC++];

    return ((static_cast<uint16>(hi) << 8) | lo);
}

uint32 FoxVM::Read32()
{
    uint16 lo = Read16();
    uint16 hi = Read16();

    return ((static_cast<uint32>(lo) << 16) | hi);
}

void FoxVM::Push16(uint16 value)
{
    if (StackPointer + 2 > scStackSize) {
        LogError(LC_SCRIPT, "Push16: Out of stack memory!");
        return;
    }

    uint16* dptr = reinterpret_cast<uint16*>(Stack + StackPointer);
    (*dptr) = value;

    StackPointer += sizeof(uint16);
}

void FoxVM::Push32(eFoxType type, uint32 value)
{
    if (StackPointer + 4 > scStackSize) {
        LogError(LC_SCRIPT, "Push32: Out of stack memory!");
        return;
    }

    uint32* dptr = reinterpret_cast<uint32*>(Stack + StackPointer);
    (*dptr) = value;

    LastPushType = type;
    StackPointer += sizeof(uint32);
}

uint32 FoxVM::Pop32()
{
    if (StackPointer <= 0) {
        LogError(LC_SCRIPT, "Pop32: No values on stack");
        return 0;
    }

    StackPointer -= sizeof(uint32);
    uint32 value = *reinterpret_cast<uint32*>(Stack + StackPointer);

    bReturnValueOnStack = false;

    return value;
}

VMCallFrame* FoxVM::GetCurrentCallFrame()
{
    if (!mIsInCallFrame || mCallFrameIndex < 1) {
        return nullptr;
    }

    return &mCallFrames[mCallFrameIndex - 1];
}

void FoxVM::ExecuteOp()
{
    uint16 op_full = Read16();

    const uint8 op_base = static_cast<uint8>(op_full >> 8);
    const uint8 op_spec = static_cast<uint8>(op_full & 0xFF);

    switch (op_base) {
    case BcBase_Push:
        DoPush(op_base, op_spec);
        break;
    case BcBase_Pop:
        DoPop(op_base, op_spec);
        break;
    case BcBase_Load:
        DoLoad(op_base, op_spec);
        break;
    case BcBase_Arith:
        DoArith(op_base, op_spec);
        break;
    case BcBase_Jump:
        DoJump(op_base, op_spec);
        break;
    case BcBase_Save:
        DoSave(op_base, op_spec);
        break;
    case BcBase_Data:
        DoData(op_base, op_spec);
        break;
    case BcBase_Type:
        DoType(op_base, op_spec);
        break;
    case BcBase_Move:
        DoMove(op_base, op_spec);
        break;
    case BcBase_Variable:
        DoVariable(op_base, op_spec);
        break;
    case BcBase_Compare:
        DoCompare(op_base, op_spec);
        break;
    }
}

void FoxVM::DoLoad(uint8 op_base, uint8 op_spec_raw) {}

void FoxVM::DoPush(uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecPush_Int32) {
        Push32(eFoxType::INT, Read32());
    }
    else if (op_spec == BcSpecPush_Float32) {
        Push32(eFoxType::FLOAT, Read32());
    }
    else if (op_spec == BcSpecPush_Var) {
        uint16 var_index = Read16();

        VMVariable& var = GetVar(var_index);
        Push32(var.Value.Type, var.Value.Get<int32>());
    }
}

void FoxVM::DoPop(uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecPop_Variable_Int32) {
        uint16 var_index = Read16();

        VMVariable& var = GetVar(var_index);

        if (var.bIsGlobalRef) {
            Globals[var.NameHash].Set<int32>(Pop32());
            return;
        }

        GetVar(var_index).Value.Set<int32>(Pop32());
    }
    else if (op_spec == BcSpecPop_Variable_Float32) {
        uint16 var_index = Read16();

        VMVariable& var = GetVar(var_index);

        if (var.bIsGlobalRef) {
            Globals[var.NameHash].Set<float32>(Pop32());
            return;
        }

        GetVar(var_index).Value.Set<float32>(Pop32());
    }

    else if (op_spec == BcSpecPop_Discard) {
        Pop32();
    }
}


void FoxVM::DoArith(uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecArith_Add) {
        // Pull values off the stack, push result back onto stack
        int32 a = static_cast<int32>(Pop32());
        int32 b = static_cast<int32>(Pop32());

        int32 result = a + b;
        Push32(eFoxType::INT, static_cast<uint32>(result));
    }

    else if (op_spec == BcSpecArith_Add_Float32) {
        // Pull values off the stack, push result back onto stack
        float32 a = std::bit_cast<float32>(Pop32());
        float32 b = std::bit_cast<float32>(Pop32());

        float32 result = a + b;
        Push32(eFoxType::FLOAT, std::bit_cast<uint32>(result));
    }
}

void FoxVM::DoSave(uint8 op_base, uint8 op_spec) {}

void FoxVM::CallExternalFunction(Hash32 hashed_name)
{
    auto it = ExternalProcs.find(hashed_name);
    if (it == ExternalProcs.end()) {
        LogWarning(LC_SCRIPT, "External function {} not found", hashed_name);
        return;
    }

    VMExternalProcEntry& func = it->second;

    SizedArray<FoxValue> args {};
    args.InitSize(func.ArgCount);

    for (uint32 i = 0; i < it->second.ArgCount; i++) {
        args[func.ArgCount - i - 1] = (FoxValue(static_cast<int32>(Pop32())));
    }

    uint32 pre_stack_size = GetStackPointer();
    func.pFunc(this, args);

    if (func.bReturnsValue && GetStackPointer() <= pre_stack_size) {
        LogError(LC_SCRIPT, "Native function registered with a return type did not push a return value");

        // Sketchy, but we need to save this sinking ship somehow
        Push32(eFoxType::INT, 0U);
    }
}


const char* FoxVM::GetString(uint32 offset) const
{
    // + 1 to remove length encoded at start of string. Everything here is aligned to 16 bits, so +1 will get us the
    // actual string data.
    return reinterpret_cast<const char*>(&mBytecode[mStringsOffset + offset]);
}

FoxSymbol* FoxVM::GetSymbol(const Hash32 name_hash) const
{
    for (FoxSymbol& sym : SymTable) {
        if (sym.Symbol.GetHash() == name_hash) {
            return &sym;
        }
    }

    return nullptr;
}

uint32 FoxVM::GetProcAddr(const Hash32 name_hash) const
{
    FoxSymbol* sym = GetSymbol(name_hash);
    if (!sym) {
        return 0;
    }

    return sym->Offset;
}

void FoxVM::StashVariables()
{
    if (ScopeIndex < 0) {
        LogWarning(LC_SCRIPT, "Scope index < 0");
        return;
    }
    VariableBaseIndex += ScopeVarCounts[ScopeIndex];
}

void FoxVM::RevertVariables()
{
    if (ScopeIndex < 0) {
        LogWarning(LC_SCRIPT, "Scope index < 0");
        return;
    }

    VariableBaseIndex -= ScopeVarCounts[ScopeIndex - 1];
}

void FoxVM::DoJump(uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecJump_Relative) {
        uint16 offset = Read16();
        PC += offset;
    }
    else if (op_spec == BcSpecJump_Equal) {
        uint16 offset = Read16();

        if (CompareResult == 0) {
            PC += offset;
        }
    }
    else if (op_spec == BcSpecJump_NotEqual) {
        uint16 offset = Read16();

        if (CompareResult != 0) {
            PC += offset;
        }
    }
    else if (op_spec == BcSpecJump_Less) {
        uint16 offset = Read16();

        if (CompareResult < 0) {
            PC += offset;
        }
    }
    else if (op_spec == BcSpecJump_LessEqual) {
        uint16 offset = Read16();

        if (CompareResult <= 0) {
            PC += offset;
        }
    }
    else if (op_spec == BcSpecJump_Greater) {
        uint16 offset = Read16();

        LogInfo("Res: {}", CompareResult);

        if (CompareResult > 0) {
            PC += offset;
        }
    }
    else if (op_spec == BcSpecJump_GreaterEqual) {
        uint16 offset = Read16();

        if (CompareResult >= 0) {
            PC += offset;
        }
    }
    else if (op_spec == BcSpecJump_Absolute) {
        uint32 position = Read32();
        PC = position;
    }
    else if (op_spec == BcSpecJump_CallAbsolute) {
        StashVariables();

        uint32 name_hash = Read32();
        uint32 call_offset = GetProcAddr(name_hash);

        mIsInParams = false;

        ++ScopeIndex;

        ReturnAddress = PC;
        // Jump to the action address
        PC = call_offset;
    }
    else if (op_spec == BcSpecJump_CallExternal) {
        Hash32 hashed_name = Read32();
        CallExternalFunction(hashed_name);
    }
    else if (op_spec == BcSpecJump_ReturnToCaller) {
        RevertVariables();

        --ScopeIndex;
        PC = ReturnAddress;
    }
    else if (op_spec == BcSpecJump_ReturnToCaller_Int32) {
        RevertVariables();

        LastPushType = eFoxType::INT;
        --ScopeIndex;
        bReturnValueOnStack = true;
        PC = ReturnAddress;
    }
    else if (op_spec == BcSpecJump_ReturnToCaller_Float32) {
        RevertVariables();

        LastPushType = eFoxType::FLOAT;
        --ScopeIndex;
        bReturnValueOnStack = true;
        PC = ReturnAddress;
    }
    else if (op_spec == BcSpecJump_ReturnToCaller_String) {
        RevertVariables();

        LastPushType = eFoxType::STRING;
        --ScopeIndex;
        bReturnValueOnStack = true;
        PC = ReturnAddress;
    }
}

void FoxVM::DoData(uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecData_String) {
        uint16 length = Read16();
        PC += length;
    }
}

void FoxVM::DoType(uint8 op_base, uint8 op_spec) {}

void FoxVM::DoMove(uint8 op_base, uint8 op_spec_raw) {}

void FoxVM::DoVariable(uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecVariable_Set_Int32) {
        uint16 var_index = Read16();
        uint32 value = Read32();
        LogInfo("VSET [int32] ${}, {}", var_index, value);


        VMVariable& var = GetVar(var_index);
        var.Value.Set<int32>(value);

        // Update global variable
        if (var.bIsGlobalRef) {
            Globals[var.NameHash].Set<int32>(value);
        }
    }
    else if (op_spec == BcSpecVariable_Set_Float32) {
        uint16 var_index = Read16();
        float32 value = std::bit_cast<float32>(Read32());
        LogInfo("VSET [float32] ${}, {}", var_index, value);

        VMVariable& var = GetVar(var_index);
        var.Value.Set<float32>(value);

        // Update global variable
        if (var.bIsGlobalRef) {
            Globals[var.NameHash].Set<float32>(value);
        }
    }
    else if (op_spec == BcSpecVariable_Set_String) {
        uint16 var_index = Read16();
        uint32 string_offset = Read32();

        VMVariable& var = GetVar(var_index);
        var.Value.Set<int32>(string_offset);

        // Update global variable
        if (var.bIsGlobalRef) {
            Globals[var.NameHash].Set<int32>(string_offset);
        }
    }
    else if (op_spec == BcSpecVariable_Set_Var) {
        VarIndex dst_index = Read16();
        VarIndex src_index = Read16();

        VMVariable& dst_var = GetVar(dst_index);

        if (dst_var.bIsGlobalRef) {
            Globals[dst_var.NameHash] = GetVar(src_index).Value;
        }
        else {
            GetVar(dst_index).Value = GetVar(src_index).Value;
        }
    }
    else if (op_spec == BcSpecVariable_Define) {
        uint16 var_index = Read16();
        Hash32 name_hash = Read32();

        VMVariable& var = GetVar(var_index);
        ScopeVarCounts[ScopeIndex] = var_index + 1;

        var.NameHash = name_hash;
        var.Value.Set<int32>(0);
    }

    else if (op_spec == BcSpecVariable_DefineGlobal) {
        uint16 var_index = Read16();
        Hash32 name_hash = Read32();

        auto it = Globals.find(name_hash);
        if (it == Globals.end()) {
            Globals[name_hash].Set<int32>(0);
        }

        VMVariable& var = GetVar(var_index);

        var.NameHash = name_hash;
        var.bIsGlobalRef = true;
        var.Value.Set<int32>(Globals[name_hash].Get<int32>());
    }
    else if (op_spec == BcSpecVariable_Cast_Int32) {
        uint16 var_index = Read16();

        FoxValue& var_value = GetVar(var_index).Value;
        float32 fvalue = var_value.Get<float32>();
        var_value.Set<int32>(fvalue);
    }
}

void FoxVM::DoCompare(uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecCompare_Default) {
        int32 b = std::bit_cast<int32>(Pop32());
        int32 a = std::bit_cast<int32>(Pop32());

        CompareResult = (a - b);
    }
    else if (op_spec == BcSpecCompare_NotZero) {
        int32 value = std::bit_cast<int32>(Pop32());

        // Compare result is zero if value does not equal zero
        CompareResult = (value == 0);
    }
}

FoxVM::~FoxVM() { gScriptMemPool->Free(pVariables); }

} // namespace fx::script
