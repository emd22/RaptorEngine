#pragma once

#include "FoxValue.hpp"

#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Types.hpp>
#include <unordered_map>

namespace fx::script {

///////////////////////////////////////////
// Bytecode VM
///////////////////////////////////////////

struct VMCallFrame
{
    uint32 StartStackIndex = 0;
};


struct VMVariable
{
    Hash32 NameHash = HashNull32;
    FoxValue Value;
};


struct FoxSymbol
{
    Name Symbol;
    uint32 Offset;
};


struct VMScope
{
    VMVariable Variables[32];
};

class FoxVM;

using VMExternalFunction = void (*)(FoxVM* vm, const SizedArray<FoxValue>& args);

struct VMExternalFunctionEntry
{
    VMExternalFunction pFunc;
    uint32 ArgCount = 0;
    bool bReturnsValue = false;
};

class FoxVM
{
    static constexpr uint32 scStackSize = 1024 * 16;

public:
    FoxVM() = default;

    void InitVM(SizedArray<uint8>&& bytecode);

    FX_FORCE_INLINE uint32 GetStackPointer() const { return StackPointer; }

    FoxSymbol* GetSymbol(const Hash32 name_hash) const;
    uint32 GetFunctionAddr(const Hash32 name_hash) const;

    void Push16(uint16 value);
    void Push32(eFoxType type, uint32 value);
    uint32 Pop32();

    void ExecuteOp();

private:
    void LoadSymTable();

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

    VMCallFrame* GetCurrentCallFrame();

public:
    uint8* Stack = nullptr;
    uint32 StackPointer = 0;
    uint32 ReturnAddress = 0;

    SizedArray<uint8> mBytecode;

    SizedArray<FoxSymbol> SymTable;
    SizedArray<VMScope> Scopes;
    int32 ScopeIndex = 0;

    std::unordered_map<Hash32, VMExternalFunctionEntry, Hash32Stl> ExternalFunctions;

    uint32 PC = 0;
    bool bReturnValueOnStack = false;

    eFoxType LastPushType = eFoxType::NONETYPE;

private:
    bool mIsInCallFrame = false;

    VMCallFrame mCallFrames[8];
    int mCallFrameIndex = 0;

    bool mIsInParams = false;


    eFoxType mCurrentType = eFoxType::NONETYPE;
};


} // namespace fx::script
