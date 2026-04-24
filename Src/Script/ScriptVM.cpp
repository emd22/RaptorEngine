#include "ScriptVM.hpp"

#include "BytecodeCompiler.hpp"
#include "ScriptBytecode.hpp"

namespace fx::script {

char* ScriptVM::ReadString(char* buffer, uint32 buffer_size)
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


void ScriptVM::LoadSymTable()
{
    uint32 num_symbols = Read32();
    LogInfo("Loaded {} symbols", num_symbols);

    SymTable.InitSize(num_symbols);

    constexpr uint32 scBufferSize = 512;
    char name_buffer[scBufferSize];

    for (uint32 index = 0; index < num_symbols; index++) {
        ReadString(name_buffer, scBufferSize);

        VMSymbol& sym = SymTable[index];

        sym.Symbol = Name(name_buffer);
        sym.Offset = Read32();

        LogInfo("Symbol loaded: {} at offset {}", sym.Symbol.Get(), sym.Offset);
    }
}

void ScriptVM::Start(PagedArray<uint8>&& bytecode)
{
    mBytecode = std::move(bytecode);
    mPushedTypes.Create(64);

    Scopes.InitSize(16);

    LoadSymTable();

    Stack = gScriptMemPool->Alloc<uint8>(scStackSize);

    // while (mPC < mBytecode.Size()) {
    //     ExecuteOp();
    // }
}


VMSymbol* ScriptVM::GetSymbol(const String& name) const
{
    for (VMSymbol& sym : SymTable) {
        if (sym.Symbol.Get() == name) {
            return &sym;
        }
    }

    return nullptr;
}

VMSymbol* ScriptVM::GetSymbol(Hash32 name_hash) const
{
    for (VMSymbol& sym : SymTable) {
        if (sym.Symbol.GetHash() == name_hash) {
            return &sym;
        }
    }

    return nullptr;
}


uint32 ScriptVM::GetFunctionAddr(Hash32 name_hash) const
{
    VMSymbol* sym = GetSymbol(name_hash);
    if (!sym) {
        return 0;
    }

    return sym->Offset;
}

FoxValue ScriptVM::CallFunction(VMSymbol* sym)
{
    if (!sym) {
        return FoxValue::scNone;
    }

    ScopeIndex++;

    mPC = sym->Offset;

    while (mPC < mBytecode.Size()) {
        ExecuteOp();

        if (ScopeIndex <= 0) {
            break;
        }
    }


    if (mbReturnValueOnStack) {
        return FoxValue::NumberFromRaw(LastPushType, Pop32());
    }

    return FoxValue::scNone;
}


uint16 ScriptVM::Read16()
{
    uint8 lo = mBytecode[mPC++];
    uint8 hi = mBytecode[mPC++];

    return ((static_cast<uint16>(lo) << 8) | hi);
}

uint16 ScriptVM::Read16Rev()
{
    uint8 lo = mBytecode[mPC++];
    uint8 hi = mBytecode[mPC++];

    return ((static_cast<uint16>(hi) << 8) | lo);
}

uint32 ScriptVM::Read32()
{
    uint16 lo = Read16();
    uint16 hi = Read16();

    return ((static_cast<uint32>(lo) << 16) | hi);
}

void ScriptVM::Push16(uint16 value)
{
    uint16* dptr = reinterpret_cast<uint16*>(Stack + StackPointer);
    (*dptr) = value;

    LogInfo("Push16: {}", value);

    StackPointer += sizeof(uint16);
}

void ScriptVM::Push32(eFoxType type, uint32 value)
{
    uint32* dptr = reinterpret_cast<uint32*>(Stack + StackPointer);
    (*dptr) = value;

    LogInfo("Push32: {}", value);

    LastPushType = type;
    StackPointer += sizeof(uint32);
}

uint32 ScriptVM::Pop32()
{
    if (StackPointer == 0) {
        LogError("Pop32: Cannot pop off of an empty stack!");
    }

    StackPointer -= sizeof(uint32);
    uint32 value = *reinterpret_cast<uint32*>(Stack + StackPointer);

    mbReturnValueOnStack = false;

    LogInfo("Pop32: {}", value);

    return value;
}

ScriptVMCallFrame* ScriptVM::GetCurrentCallFrame()
{
    if (!mIsInCallFrame || mCallFrameIndex < 1) {
        return nullptr;
    }

    return &mCallFrames[mCallFrameIndex - 1];
}

void ScriptVM::ExecuteOp()
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
    }
}

void ScriptVM::DoLoad(uint8 op_base, uint8 op_spec_raw) {}

VMScope& ScriptVM::ThisScope() { return Scopes[ScopeIndex]; }

void ScriptVM::DoPush(uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecPush_Int32) {
        Push32(eFoxType::INT, Read32());
    }
    else if (op_spec == BcSpecPush_Float32) {
        Push32(eFoxType::FLOAT, Read32());
    }
    else if (op_spec == BcSpecPush_Var) {
        uint16 var_index = Read16();

        VMVariable& var = ThisScope().Variables[var_index];
        Push32(var.Value.Type, var.Value.Get<int32>());
    }
}

void ScriptVM::DoPop(uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecPop_Variable_Int32) {
        uint16 var_index = Read16();
        ThisScope().Variables[var_index].Value.Set<int32>(Pop32());
    }
}


void ScriptVM::DoArith(uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecArith_Add) {
        // Pull values off the stack, push result back onto stack
        int32 a = static_cast<int32>(Pop32());
        int32 b = static_cast<int32>(Pop32());

        int32 result = a + b;

        LogInfo("Addition result: {}", result);
        Push32(eFoxType::INT, static_cast<uint32>(result));
    }

    else if (op_spec == BcSpecArith_Add_Float32) {
        // Pull values off the stack, push result back onto stack
        float32 a = std::bit_cast<float32>(Pop32());
        float32 b = std::bit_cast<float32>(Pop32());

        float32 result = a + b;

        LogInfo("Addition result (float32): {}", result);
        Push32(eFoxType::FLOAT, std::bit_cast<uint32>(result));
    }
}

void ScriptVM::DoSave(uint8 op_base, uint8 op_spec) {}

void ScriptVM::RegisterFunction(Hash32 hashed_name, uint32 arg_count, VMExternalFunction func)
{
    VMExternalFunctionEntry entry { .Func = func, .ArgCount = arg_count };

    ExternalFunctions[hashed_name] = entry;
    LogInfo("Registered external function {}", hashed_name);
}

void ScriptVM::CallExternalFunction(Hash32 hashed_name)
{
    auto it = ExternalFunctions.find(hashed_name);
    if (it == ExternalFunctions.end()) {
        LogWarning("External function {} not found", hashed_name);
        return;
    }

    SizedArray<FoxValue> args {};
    args.InitSize(it->second.ArgCount);

    for (uint32 i = 0; i < it->second.ArgCount; i++) {
        args[it->second.ArgCount - i - 1] = (FoxValue(static_cast<int32>(Pop32())));
    }

    it->second.Func(args);
}


void ScriptVM::DoJump(uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecJump_Relative) {
        uint16 offset = Read16();
        mPC += offset;
    }
    else if (op_spec == BcSpecJump_Absolute) {
        uint32 position = Read32();
        mPC = position;
    }
    else if (op_spec == BcSpecJump_CallAbsolute) {
        uint32 name_hash = Read32();
        uint32 call_offset = GetFunctionAddr(name_hash);

        LogInfo("Call addr: {}", call_offset);

        mPushedTypes.Clear();
        mIsInParams = false;

        ++ScopeIndex;

        ReturnAddress = mPC;
        // Jump to the action address
        mPC = call_offset;
    }
    else if (op_spec == BcSpecJump_ReturnToCaller) {
        LogInfo("Returning from scope...");
        --ScopeIndex;
        mPC = ReturnAddress;
    }
    else if (op_spec == BcSpecJump_ReturnToCaller_Int32) {
        LastPushType = eFoxType::INT;

        LogInfo("Returning from scope (valued)...");
        --ScopeIndex;
        mbReturnValueOnStack = true;
        mPC = ReturnAddress;
    }
    else if (op_spec == BcSpecJump_ReturnToCaller_Float32) {
        LastPushType = eFoxType::FLOAT;
        LogInfo("Returning from scope (valued)...");
        --ScopeIndex;
        mbReturnValueOnStack = true;
        mPC = ReturnAddress;
    }
    else if (op_spec == BcSpecJump_CallExternal) {
        Hash32 hashed_name = Read32();
        CallExternalFunction(hashed_name);
    }
    // else if (op_spec == BcSpecJump_CallExternal) {
    //     uint32 hashed_name = Read32();

    //     ScriptExternalFunc* external_func = FindExternalAction(hashed_name);

    //     if (!external_func) {
    //         printf("!!! Could not find external function in VM!\n");
    //         return;
    //     }

    //     std::vector<ScriptValue> params;
    //     params.reserve(external_func->ParameterTypes.size());

    //     uint32 num_params = mPushedTypes.Size();

    //     printf("Num Params: %u\n", num_params);

    //     for (int i = 0; i < num_params; i++) {
    //         ScriptValue::ValueType param_type = mPushedTypes.GetLast();

    //         ScriptValue value;
    //         value.Type = param_type;

    //         if (param_type == ScriptValue::INT) {
    //             value.ValueInt = Pop32();
    //             value.Type = param_type;
    //         }
    //         else if (param_type == ScriptValue::STRING) {
    //             uint32 string_location = Pop32();
    //             uint8* str_base_ptr = &mBytecode[string_location];
    //             // uint16 str_length = *((uint16*)str_base_ptr);

    //             str_base_ptr += 2;

    //             value.ValueString = reinterpret_cast<char*>(str_base_ptr);
    //             value.Type = param_type;
    //         }

    //         mPushedTypes.RemoveLast();

    //         params.push_back(value);
    //     }

    // mPushedTypes.Clear();
    // mIsInParams = false;

    // FoxValue return_value {};
    // external_func->Function(this, params, &return_value);
} // namespace fx::script

void ScriptVM::DoData(uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecData_String) {
        uint16 length = Read16();
        mPC += length;
    }
    // else if (op_spec == BcSpecData_ParamsStart) {
    //     mIsInParams = true;

    //     // Push the current return address pointer. This is so nested action calls
    //     // can correctly navigate back to the caller.
    //     // Push32(Registers[FX_REG_RA]);
    // }
}

void ScriptVM::DoType(uint8 op_base, uint8 op_spec)
{
    // if (op_spec == OpSpecType_Int) {
    //     mCurrentType = ScriptValue::INT;
    // }
    // else if (op_spec == OpSpecType_String) {
    //     mCurrentType = ScriptValue::STRING;
    // }
}

void ScriptVM::DoMove(uint8 op_base, uint8 op_spec_raw)
{
    // uint8 op_spec = ((op_spec_raw >> 4) & 0x0F);
    // ScriptRegister op_reg = static_cast<ScriptRegister>(op_spec_raw & 0x0F);

    // if (op_spec == OpSpecMove_Int32) {
    //     int32 value = Read32();
    //     Registers[op_reg] = value;
    // }
}

void ScriptVM::DoVariable(uint8 op_base, uint8 op_spec)
{
    if (op_spec == BcSpecVariable_Set_Int32) {
        uint16 var_index = Read16();
        uint32 value = Read32();
        LogInfo("VSET [int32] ${}, {}", var_index, value);

        ThisScope().Variables[var_index].Value.Set<int32>(value);
    }
    else if (op_spec == BcSpecVariable_Set_Float32) {
        uint16 var_index = Read16();
        float32 value = std::bit_cast<float32>(Read32());
        LogInfo("VSET [float32] ${}, {}", var_index, value);

        ThisScope().Variables[var_index].Value.Set<float32>(value);
    }
    else if (op_spec == BcSpecVariable_Set_Var) {
        VarIndex dst_index = Read16();
        VarIndex src_index = Read16();

        ThisScope().Variables[dst_index].Value = ThisScope().Variables[src_index].Value;
    }
    else if (op_spec == BcSpecVariable_Define) {
        uint16 var_index = Read16();
        Hash32 name_hash = Read32();

        ThisScope().Variables[var_index].NameHash = name_hash;
        ThisScope().Variables[var_index].Value.Set<int32>(0);

        LogInfo("Define variable {}", var_index);
    }
    else if (op_spec == BcSpecVariable_Cast_Int32) {
        uint16 var_index = Read16();

        FoxValue& var_value = ThisScope().Variables[var_index].Value;
        float32 fvalue = var_value.Get<float32>();
        var_value.Set<int32>(fvalue);
    }
}
} // namespace fx::script
