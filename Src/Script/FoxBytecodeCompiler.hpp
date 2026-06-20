#pragma once

#include "FoxAst.hpp"
#include "FoxBytecode.hpp"

#include <Core/PagedArray.hpp>
#include <Core/Ref.hpp>
#include <Core/Types.hpp>

namespace fx::script {


struct FoxBytecodeVarHandle
{
    Hash32 HashedName = HashNull32;
    eFoxType Type = eFoxType::INT;

    bool bIsPointer = false;
    bool bIsPointerAssigned = false;

    int64 Offset = 0;

    int32 ScopeIndex = 0;
    uint16 VariableIndex = 0;

    uint16 SizeOnStack = 4;
};

struct FoxBytecodeFunctionHandle
{
    Hash32 HashedName = HashNull32;
    uint32 BytecodeIndex = 0;
};

enum class eFoxConditionResult
{
    Less,
    LessEqual,
    Equal,
    NotEqual,
    GreaterEqual,
    Greater,
};

enum class eFoxPushMode
{
    Default,
    PushPointer,
};

using VarIndex = uint16;
constexpr VarIndex VarIndexNull = UINT16_MAX;

struct FoxBytecodeString
{
    uint32 FixupOffset = 0;
    String StringValue;
};

class FoxBytecodeCompiler
{
public:
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


    enum VarDeclareMode
    {
        DECLARE_DEFAULT,
        DO_NOT_ALLOW_ASSIGNMENT,
        DECLARE_PARAMETER,
    };

public:
    FoxBytecodeCompiler() = default;

    SizedArray<uint8> Compile(FoxAstNode* root_node);
    void EmitNode(FoxAstNode* node);

    void PrintBytecode();
    bool HasErrors() const { return mErrorCount > 0; }
    void WriteHeaderFile(const String& script_path, FoxAstBlock* root);

    ~FoxBytecodeCompiler() = default;


private:
    void EmitBlock(FoxAstBlock* block, int params_to_save, bool is_function_body);
    void EmitFunctionDeclaration(FoxAstFunctionDecl* function);
    void EmitFunctionDefinitionsInBlock(FoxAstBlock* block);

    eFoxType DoBuiltin(FoxAstFunctionCall* call, bool do_not_call);
    void EmitFunctionCall(FoxAstFunctionCall* call, bool preserve_return_value);
    void EmitModuleCall(FoxAstModuleCall* call, bool preserve_return_value);

    FoxBytecodeVarHandle* DoVarDeclare(FoxAstVarDecl* decl, VarDeclareMode mode = DECLARE_DEFAULT);
    void EmitAssign(FoxAstAssign* assign);
    FoxBytecodeVarHandle* DefineParam(FoxAstNode* param_decl_node);
    FoxBytecodeVarHandle* DefineReturnVar(FoxAstVarDecl* decl);

    bool ValidateParameters(FoxAstFunctionCall* call);
    void AddString(uint32 fixup_offset, const String& value);

    void EmitSymbolTable(FoxAstBlock* root);
    void EmitLinkTable(FoxAstBlock* root);
    void EmitStrings();

    template <typename... TTypes>
    void CompileError(const char* fmt, TTypes&&... args)
    {
        LogError(fmt, args...);
        ++mErrorCount;
    }

    uint16 GetSizeOfType(Token* type);

    void EmitPush32(int32 value);
    void EmitPushFloat32(float32 value);
    void EmitPushString(uint32 value);
    void EmitPushVar(VarIndex var);
    void EmitPushVarPtr(VarIndex var);
    void EmitPushReadPtr(VarIndex var);
    void EmitPushReturnAddr();

    eFoxType EmitPushUnderlyingValue(FoxAstNode* node, eFoxPushMode mode = eFoxPushMode::Default);
    eFoxType GetUnderlyingType(FoxAstNode* node);
    bool IsUnderlyingVariableRef(FoxAstNode* node);

    void EmitStackAlloc(uint16 size);

    void EmitPopVar(VarIndex var);
    void EmitPopDiscard();
    void EmitPopReturnAddr();

    void EmitJumpRelative(uint16 offset);
    void EmitJumpConditional(uint16 offset, eFoxConditionResult cond);
    void EmitJumpAbsolute(uint32 position);
    void EmitJumpCallAbsolute(uint32 position);

    void EmitJumpReturnToCaller();
    void EmitJumpReturnToCallerInt32();
    void EmitJumpReturnToCallerFloat32();
    void EmitJumpReturnToCallerString();

    void EmitJumpPause(uint16 time_ms);

    void EmitJumpCallExternal(Hash32 hashed_name);

    void EmitReturn(FoxAstReturn* return_node);
    void EmitIfStatement(FoxAstIf* if_node);
    eFoxConditionResult EmitPushConditionResult(FoxAstNode* condition_node);

    void EmitVariableSetInt32(uint16 var_index, int32 value, bool is_pointer);
    void EmitVariableSetString(uint16 var_index, bool is_pointer);
    void EmitVariableSetFloat32(uint16 var_index, float32 value, bool is_pointer);
    void EmitVariableSetVar(VarIndex dst, VarIndex src, bool is_dst_pointer);


    void EmitVariableDefine(eFoxType type, uint16 var_index);
    void EmitVariableGlobalDefine(eFoxType type, uint16 var_index, Hash32 name_hash);
    void EmitVariableIndex(uint16 var_index);

    void EmitVariableDefineFetchParam(eFoxType type, uint16 var_index);

    void EmitVariableGlobalHoist(FoxBytecodeVarHandle* handle, Hash32 name_hash);

    void EmitVariableCastInt32();
    void EmitVariableCastFloat32();

    void EmitCompare();
    void EmitCompareNotZero();

    void EmitType(eFoxType type);

    /**
     * @brief Emits a null-terminated string to the bytecode with the length preceding it.
     * The length is 16-bit and is used to quickly read in the string and ensure we are not reading past boundaries.
     */
    uint32 EmitDataString(const char* str, uint16 length, bool emit_length_prefix);

    void EmitBinop(FoxAstBinop* binop);

    void EmitRhs(FoxAstNode* rhs, RhsMode mode, FoxBytecodeVarHandle* handle);

    void EmitMarker(BcSpecMarker spec);

    void Fixup16(uint32 addr, uint16 value);
    void Fixup32(uint32 addr, uint32 value);

    void WriteOp(uint8 base_op, uint8 spec_op);
    void Write16(uint16 value);
    void Write32(uint32 value);

    FoxBytecodeVarHandle* FindVarHandle(Hash32 hashed_name) const;
    VarIndex FindVarInScope(Hash32 hashed_name) const;

    FoxBytecodeVarHandle* FindGlobalHandle(Hash32 hashed_name) const;


    FoxBytecodeFunctionHandle* FindFunctionHandle(Hash32 hashed_name);

    eFoxType GetLiteralUnderlyingType(FoxAstLiteral* literal) const;

public:
    SizedArray<FoxBytecodeVarHandle> LocalVarHandles;
    PagedArray<FoxBytecodeVarHandle> GlobalHandles;

    std::vector<FoxBytecodeFunctionHandle> FunctionHandles;
    PagedArray<uint8> mBytecode {};

    PagedArray<FoxBytecodeString> Strings;

private:
    uint16 mVariableIndex = 0;
    uint32 mErrorCount = 0;

    int32 mScopeIndex = 0;

    FoxAstFunctionDecl* mpCurrentFunctionBody = nullptr;

    bool mEntryPointEmitted = false;
    bool mbEmitDebugInfo = true;
};


class FoxBytecodePrinter
{
public:
    FoxBytecodePrinter(const SizedArray<uint8>& bytecode) { mpBytecode = Slice(bytecode); }

    void LoadSymbolTable();
    void LoadLinkTable();

    void Print();
    void PrintOp();


private:
    uint16 Read16();
    uint32 Read32();

    uint16 Read16Rev();

    void DoPush(char* s, uint8 op_base, uint8 op_spec);
    void DoPop(char* s, uint8 op_base, uint8 op_spec);
    void DoArith(char* s, uint8 op_base, uint8 op_spec);
    void DoJump(char* s, uint8 op_base, uint8 op_spec);
    void DoData(char* s, uint8 op_base, uint8 op_spec);
    void DoType(char* s, uint8 op_base, uint8 op_spec);
    void DoMarker(char* s, uint8 op_base, uint8 op_spec);
    void DoVariable(char* s, uint8 op_base, uint8 op_spec);
    void DoCompare(char* s, uint8 op_base, uint8 op_spec);

    char* ReadString(char* buffer, uint32 buffer_size, bool has_prefixed_length);

private:
    uint32 mBytecodeIndex = 0;
    Slice<uint8> mpBytecode { nullptr };

    uint32 mStringTableOffset = 0;
};

} // namespace fx::script
