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
    bool bIsGlobalRef = false;
    FoxValue Value;
};


struct FoxSymbol
{
    Name Symbol;
    uint32 Offset;
};


class FoxVM;

using VMExternalFunction = void (*)(FoxVM* vm, const SizedArray<FoxValue>& args);

struct VMExternalProcEntry
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
    uint32 GetProcAddr(const Hash32 name_hash) const;

    void Push16(uint16 value);
    void Push32(eFoxType type, uint32 value);
    uint32 Pop32();

    void ExecuteOp();

    ~FoxVM();

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
    void DoCompare(uint8 op_base, uint8 op_spec);

    VMVariable& GetVar(uint16 index) { return pVariables[index + VariableBaseIndex]; }

    void CallExternalFunction(Hash32 hashed_name);

    void StashVariables();
    void RevertVariables();


    uint16 Read16();
    uint16 Read16Rev();
    uint32 Read32();
    char* ReadString(char* buffer, uint32 buffer_size);

    VMCallFrame* GetCurrentCallFrame();

public:
    uint8* Stack = nullptr;
    uint32 StackPointer = 0;
    uint32 ReturnAddress = 0;
    int32 CompareResult = 0;

    SizedArray<uint8> mBytecode;

    SizedArray<FoxSymbol> SymTable;

    std::unordered_map<Hash32, FoxValue, Hash32Stl> Globals;

    VMVariable* pVariables = nullptr;

    /// The base index for variables. When calling another function, the variable indexes will start from 0, but will be
    /// offset by this value. This avoids clobbering variables when calling other functions.
    int32 VariableBaseIndex = 0;
    uint16 ScopeVarCounts[32];

    int32 ScopeIndex = 0;


    std::unordered_map<Hash32, VMExternalProcEntry, Hash32Stl> ExternalProcs;

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
