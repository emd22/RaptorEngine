#pragma once

#include "ScriptVar.hpp"

#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Types.hpp>

namespace fx::script {

///////////////////////////////////////////
// Bytecode VM
///////////////////////////////////////////

struct ScriptVMCallFrame
{
    uint32 StartStackIndex = 0;
};


struct VMVariable
{
    Hash32 NameHash = HashNull32;
    FoxValue Value;
};


struct VMSymbol
{
    Name Symbol;
    uint32 Offset;
};


struct VMScope
{
    VMVariable Variables[32];
};

class ScriptVM
{
    static constexpr uint32 scStackSize = 1024 * 16;

public:
    ScriptVM() = default;

    void Start(PagedArray<uint8>&& bytecode);

    VMSymbol* GetSymbol(const String& name) const;
    VMSymbol* GetSymbol(Hash32 name_hash) const;
    uint32 GetFunctionAddr(Hash32 name_hash) const;

    uint32 CallFunction(VMSymbol* sym);

    void Push16(uint16 value);
    void Push32(uint32 value);

    uint32 Pop32();

private:
    void LoadSymTable();

    void ExecuteOp();

    void DoPush(uint8 op_base, uint8 op_spec);
    void DoPop(uint8 op_base, uint8 op_spec);
    void DoLoad(uint8 op_base, uint8 op_spec);
    void DoArith(uint8 op_base, uint8 op_spec);
    void DoSave(uint8 op_base, uint8 op_spec);
    void DoJump(uint8 op_base, uint8 op_spec);
    void DoData(uint8 op_base, uint8 op_spec);
    void DoType(uint8 op_base, uint8 op_spec);
    void DoMove(uint8 op_base, uint8 op_spec);
    void DoVariable(uint8 op_base, uint8 op_spec);

    VMScope& ThisScope();

    uint16 Read16();
    uint16 Read16Rev();
    uint32 Read32();
    char* ReadString(char* buffer, uint32 buffer_size);

    ScriptVMCallFrame* GetCurrentCallFrame();

public:
    uint8* Stack = nullptr;
    uint32 StackPointer = 0;
    uint32 ReturnAddress = 0;

    PagedArray<uint8> mBytecode;

    SizedArray<VMSymbol> SymTable;
    SizedArray<VMScope> Scopes;
    int32 ScopeIndex = 0;

private:
    uint32 mPC = 0;

    bool mIsInCallFrame = false;

    ScriptVMCallFrame mCallFrames[8];
    int mCallFrameIndex = 0;

    bool mIsInParams = false;
    PagedArray<FoxValue::eValueType> mPushedTypes;

    bool mbReturnValueOnStack = false;

    FoxValue::eValueType mCurrentType = FoxValue::NONETYPE;
};


} // namespace fx::script
