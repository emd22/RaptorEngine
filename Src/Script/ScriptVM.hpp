#pragma once

#include "ScriptVar.hpp"

#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Types.hpp>
#include <unordered_map>

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

using VMExternalFunction = void (*)(const SizedArray<FoxValue>& args);

struct VMExternalFunctionEntry
{
    VMExternalFunction Func;
    uint32 ArgCount = 0;
};

class ScriptVM
{
    static constexpr uint32 scStackSize = 1024 * 16;

public:
    ScriptVM() = default;

    void Start(PagedArray<uint8>&& bytecode);

    void RegisterFunction(Hash32 hashed_name, uint32 arg_count, VMExternalFunction func);

    VMSymbol* GetSymbol(const String& name) const;
    VMSymbol* GetSymbol(Hash32 name_hash) const;
    uint32 GetFunctionAddr(Hash32 name_hash) const;

    FoxValue CallFunction(VMSymbol* sym);

    void Push16(uint16 value);
    void Push32(eFoxType type, uint32 value);

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

    void CallExternalFunction(Hash32 hashed_name);

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

    std::unordered_map<Hash32, VMExternalFunctionEntry, Hash32Stl> ExternalFunctions;

private:
    uint32 mPC = 0;

    bool mIsInCallFrame = false;

    ScriptVMCallFrame mCallFrames[8];
    int mCallFrameIndex = 0;

    bool mIsInParams = false;
    PagedArray<eFoxType> mPushedTypes;

    bool mbReturnValueOnStack = false;
    eFoxType LastPushType = eFoxType::NONETYPE;

    eFoxType mCurrentType = eFoxType::NONETYPE;
};


} // namespace fx::script
