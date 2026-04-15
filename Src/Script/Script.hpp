// #pragma once

// #include <Core/MemPool/MPPagedArray.hpp>
// #include <Util/Tokenizer.hpp>
// #include <vector>

// #define FX_SCRIPT_VERSION_MAJOR 0
// #define FX_SCRIPT_VERSION_MINOR 3
// #define FX_SCRIPT_VERSION_PATCH 0

// #define FX_SCRIPT_USE_MEMPOOL 1

// #ifdef FX_SCRIPT_USE_MEMPOOL

// #include <Core/Memory.hpp>

// #define FX_SCRIPT_ALLOC_MEMORY(ptrtype_, size_) MemPool::Alloc<ptrtype_>(size_)
// #define FX_SCRIPT_ALLOC_NODE(nodetype_)         MemPool::Alloc<nodetype_>(sizeof(nodetype_))
// #define FX_SCRIPT_FREE(ptrtype_, ptr_)          MemPool::Free<ptrtype_>(ptr_)

// #else

// // #define FX_SCRIPT_ALLOC_MEMORY(ptrtype_, size_) new ptrtype_[size_]
// #define FX_SCRIPT_ALLOC_MEMORY(ptrtype_, size_) ScriptAllocMemory<ptrtype_>(size_)
// #define FX_SCRIPT_ALLOC_NODE(nodetype_)         ScriptAllocMemory<nodetype_>(sizeof(nodetype_))
// #define FX_SCRIPT_FREE(ptrtype_, ptr_)          ScriptFreeMemory<ptrtype_>(ptr_)

// #include <cstdlib>

// #endif

// template <typename T>
// T* ScriptAllocMemory(size_t size)
// {
//     T* ptr = reinterpret_cast<T*>(malloc(size));
//     if constexpr (std::is_constructible_v<T>) {
//         new (ptr) T;
//     }

//     return ptr;
// }

// template <typename T>
// void ScriptFreeMemory(T* ptr)
// {
//     if constexpr (std::is_destructible_v<T>) {
//         ptr->~T();
//     }

//     free(ptr);
// }

// struct AstVarRef;
// struct ScriptScope;
// struct ScriptAction;

// struct ScriptValue
// {
//     static ScriptValue None;

//     enum ValueType : uint16
//     {
//         NONETYPE = 0x00,
//         INT = 0x01,
//         FLOAT = 0x02,
//         STRING = 0x04,
//         VEC3 = 0x08,
//         REF = 0x10
//     };

//     ValueType Type = NONETYPE;

//     union
//     {
//         int ValueInt = 0;
//         float ValueFloat;
//         float ValueVec3[3];
//         char* ValueString;

//         AstVarRef* ValueRef;
//     };

//     ScriptValue() {}

//     explicit ScriptValue(ValueType type, int value) : Type(type), ValueInt(value) {}

//     explicit ScriptValue(ValueType type, float value) : Type(type), ValueFloat(value) {}

//     ScriptValue(const ScriptValue& other)
//     {
//         Type = other.Type;
//         if (other.Type == INT) {
//             ValueInt = other.ValueInt;
//         }
//         else if (other.Type == FLOAT) {
//             ValueFloat = other.ValueFloat;
//         }
//         else if (other.Type == STRING) {
//             ValueString = other.ValueString;
//         }
//         else if (other.Type == REF) {
//             ValueRef = other.ValueRef;
//         }
//     }

//     void Print() const
//     {
//         printf("[Value: ");
//         if (Type == NONETYPE) {
//             printf("Null]\n");
//         }
//         else if (Type == INT) {
//             printf("Int, %d]\n", ValueInt);
//         }
//         else if (Type == FLOAT) {
//             printf("Float, %f]\n", ValueFloat);
//         }
//         else if (Type == STRING) {
//             printf("String, %s]\n", ValueString);
//         }
//         else if (Type == REF) {
//             printf("Ref, %p]\n", ValueRef);
//         }
//     }

//     inline bool IsNumber() { return (Type == INT || Type == FLOAT); }

//     inline bool IsRef() { return (Type == REF); }
// };

// enum AstType
// {
//     FX_AST_LITERAL,
//     // FX_AST_NAME,

//     FX_AST_BINOP,
//     FX_AST_UNARYOP,
//     FX_AST_BLOCK,

//     // Variables
//     FX_AST_VARREF,
//     FX_AST_VARDECL,
//     FX_AST_ASSIGN,

//     // Actions
//     FX_AST_ACTIONDECL,
//     FX_AST_ACTIONCALL,
//     FX_AST_RETURN,

//     FX_AST_DOCCOMMENT,

//     FX_AST_COMMANDMODE,
// };

// struct AstNode
// {
//     AstType NodeType;
// };


// struct AstLiteral : public AstNode
// {
//     AstLiteral() { this->NodeType = FX_AST_LITERAL; }

//     // Tokenizer::Token* Token = nullptr;
//     ScriptValue Value;
// };

// struct AstBinop : public AstNode
// {
//     AstBinop() { this->NodeType = FX_AST_BINOP; }

//     Tokenizer::Token* OpToken = nullptr;
//     AstNode* Left = nullptr;
//     AstNode* Right = nullptr;
// };

// struct AstBlock : public AstNode
// {
//     AstBlock() { this->NodeType = FX_AST_BLOCK; }

//     std::vector<AstNode*> Statements;
// };

// struct AstVarRef : public AstNode
// {
//     AstVarRef() { this->NodeType = FX_AST_VARREF; }

//     Tokenizer::Token* Name = nullptr;
//     ScriptScope* Scope = nullptr;
// };

// struct AstAssign : public AstNode
// {
//     AstAssign() { this->NodeType = FX_AST_ASSIGN; }

//     AstVarRef* Var = nullptr;
//     // ScriptValue Value;
//     AstNode* Rhs = nullptr;
// };

// struct AstVarDecl : public AstNode
// {
//     AstVarDecl() { this->NodeType = FX_AST_VARDECL; }

//     Tokenizer::Token* Name = nullptr;
//     Tokenizer::Token* Type = nullptr;
//     AstAssign* Assignment = nullptr;

//     /// Ignore the scope that the variable is declared in, force it to be global.
//     bool DefineAsGlobal = false;
// };

// struct AstDocComment : public AstNode
// {
//     AstDocComment() { this->NodeType = FX_AST_DOCCOMMENT; }

//     Tokenizer::Token* Comment;
// };

// struct AstActionDecl : public AstNode
// {
//     AstActionDecl() { this->NodeType = FX_AST_ACTIONDECL; }

//     Tokenizer::Token* Name = nullptr;
//     AstVarDecl* ReturnVar = nullptr;
//     AstBlock* Params = nullptr;
//     AstBlock* Block = nullptr;

//     std::vector<AstDocComment*> DocComments;
// };

// struct AstCommandMode : public AstNode
// {
//     AstCommandMode() { this->NodeType = FX_AST_COMMANDMODE; }

//     AstNode* Node = nullptr;
// };

// struct AstActionCall : public AstNode
// {
//     AstActionCall() { this->NodeType = FX_AST_ACTIONCALL; }

//     ScriptAction* Action = nullptr;
//     Hash32 HashedName = 0;
//     std::vector<AstNode*> Params {}; // AstLiteral or AstVarRef
// };

// struct AstReturn : public AstNode
// {
//     AstReturn() { this->NodeType = FX_AST_RETURN; }
// };

// /**
//  * @brief Data is accessible from a label, such as a variable or an action.
//  */
// struct ScriptLabelledData
// {
//     Hash32 HashedName = 0;
//     Tokenizer::Token* Name = nullptr;

//     ScriptScope* Scope = nullptr;
// };

// struct ScriptAction : public ScriptLabelledData
// {
//     ScriptAction(Tokenizer::Token* name, ScriptScope* scope, AstBlock* block, AstActionDecl* declaration)
//     {
//         HashedName = name->GetHash();
//         Name = name;
//         Scope = scope;
//         Block = block;
//         Declaration = declaration;
//     }

//     AstActionDecl* Declaration = nullptr;
//     AstBlock* Block = nullptr;
// };

// struct ScriptVar : public ScriptLabelledData
// {
//     Tokenizer::Token* Type = nullptr;
//     ScriptValue Value;

//     bool IsExternal = false;

//     void Print() const
//     {
//         printf("[Var] Type: %.*s, Name: %.*s (Hash:%u)", Type->Length, Type->Start, Name->Length, Name->Start,
//                Name->GetHash());
//         Value.Print();
//     }

//     ScriptVar() {}

//     ScriptVar(Tokenizer::Token* type, Tokenizer::Token* name, ScriptScope* scope, bool is_external = false)
//         : Type(type)
//     {
//         this->HashedName = name->GetHash();
//         this->Name = name;
//         this->Scope = scope;
//         IsExternal = is_external;
//     }

//     ScriptVar(const ScriptVar& other)
//     {
//         HashedName = other.HashedName;
//         Type = other.Type;
//         Name = other.Name;
//         Value = other.Value;
//         IsExternal = other.IsExternal;
//     }

//     ScriptVar& operator=(ScriptVar&& other) noexcept
//     {
//         HashedName = other.HashedName;
//         Type = other.Type;
//         Name = other.Name;
//         Value = other.Value;
//         IsExternal = other.IsExternal;

//         Name = nullptr;
//         Type = nullptr;
//         HashedName = 0;

//         return *this;
//     }

//     ~ScriptVar()
//     {
//         if (!IsExternal) {
//             return;
//         }

//         // Free tokens allocated by external variables
//         if (Type && Type->Start) {
//             FX_SCRIPT_FREE(char, Type->Start);
//         }

//         if (this->Name && this->Name->Start) {
//             FX_SCRIPT_FREE(char, this->Name->Start);
//         }
//     }
// };

// class ScriptInterpreter;
// class ScriptVM;


// struct ScriptExternalFunc
// {
//     // using FuncType = void (*)(ScriptInterpreter& interpreter, std::vector<ScriptValue>& params, ScriptValue*
//     // return_value);

//     using FuncType = void (*)(ScriptVM* vm, std::vector<ScriptValue>& params, ScriptValue* return_value);

//     Hash32 HashedName = 0;
//     FuncType Function = nullptr;

//     std::vector<ScriptValue::ValueType> ParameterTypes;
//     bool IsVariadic = false;
// };

// struct ScriptScope
// {
//     PagedArray<ScriptVar> Vars;
//     PagedArray<ScriptAction> Actions;

//     ScriptScope* Parent = nullptr;

//     // This points to the return value for the current scope. If an action returns a value,
//     // this will be set to the variable that holds its value. This is interpreter only.
//     ScriptVar* ReturnVar = nullptr;

//     void PrintAllVarsInScope()
//     {
//         puts("\n=== SCOPE ===");
//         for (ScriptVar& var : Vars) {
//             var.Print();
//         }
//     }

//     ScriptVar* FindVarInScope(Hash32 hashed_name) { return FindInScope<ScriptVar>(hashed_name, Vars); }

//     ScriptAction* FindActionInScope(Hash32 hashed_name)
//     {
//         return FindInScope<ScriptAction>(hashed_name, Actions);
//     }

//     template <typename T>
//         requires std::is_base_of_v<ScriptLabelledData, T>
//     T* FindInScope(Hash32 hashed_name, const PagedArray<T>& buffer)
//     {
//         for (T& var : buffer) {
//             if (var.HashedName == hashed_name) {
//                 return &var;
//             }
//         }

//         return nullptr;
//     }
// };

// class ScriptInterpreter;

// class ConfigScript
// {
//     using Token = Tokenizer::Token;
//     using TT = Tokenizer::TokenType;

// public:
//     ConfigScript() = default;

//     void LoadFile(const char* path);

//     void PushScope();
//     void PopScope();

//     ScriptVar* FindVar(Hash32 hashed_name);

//     ScriptAction* FindAction(Hash32 hashed_name);
//     ScriptExternalFunc* FindExternalAction(Hash32 hashed_name);

//     AstNode* TryParseKeyword();

//     AstAssign* TryParseAssignment(Tokenizer::Token* var_name);

//     ScriptValue ParseValue();

//     AstActionDecl* ParseActionDeclare();

//     AstNode* ParseRhs();
//     AstActionCall* ParseActionCall();

//     AstVarDecl* ParseVarDeclare(ScriptScope* scope = nullptr);

//     AstBlock* ParseBlock();
//     AstNode* ParseStatement();

//     AstNode* ParseStatementAsCommand();

//     AstBlock* Parse();

//     /**
//      * @brief Parses and executes a script.
//      * @param interpreter The interpreter to execute with
//      */
//     void Execute(ScriptVM& vm);

//     /**
//      * @brief Executes a command on a script. Defaults to parsing with command style syntax.
//      * @param command The command to execute on the script.
//      * @return If the command has been executed
//      */
//     bool ExecuteUserCommand(const char* command, ScriptInterpreter& interpreter);

//     Token& GetToken(int offset = 0);
//     Token& EatToken(TT token_type);

//     void RegisterExternalFunc(Hash32 func_name, std::vector<ScriptValue::ValueType> param_types,
//                               ScriptExternalFunc::FuncType func, bool is_variadic);

//     void DefineExternalVar(const char* type, const char* name, const ScriptValue& value);

// private:
//     template <typename T>
//         requires std::is_base_of_v<ScriptLabelledData, T>
//     T* FindLabelledData(Hash32 hashed_name, PagedArray<T>& buffer)
//     {
//         ScriptScope* scope = mCurrentScope;

//         while (scope) {
//             T* var = scope->FindInScope<T>(hashed_name, buffer);
//             if (var) {
//                 return var;
//             }

//             scope = scope->Parent;
//         }

//         return nullptr;
//     }

//     void DefineDefaultExternalFunctions();

// private:
//     PagedArray<ScriptScope> mScopes;
//     ScriptScope* mCurrentScope;

//     std::vector<ScriptExternalFunc> mExternalFuncs;

//     std::vector<AstDocComment*> CurrentDocComments;

//     AstBlock* mRootBlock = nullptr;

//     bool mHasErrors = false;
//     bool mInCommandMode = false;

//     char* mFileData;
//     PagedArray<Token> mTokens = {};
//     uint32 mTokenIndex = 0;
// };

// //////////////////////////////////
// // Script AST Printer
// //////////////////////////////////

// class AstPrinter
// {
// public:
//     AstPrinter(AstBlock* root_block)
//     //: mRootBlock(root_block)
//     {
//     }

//     void Print(AstNode* node, int depth = 0)
//     {
//         if (node == nullptr) {
//             return;
//         }

//         for (int i = 0; i < depth; i++) {
//             putchar(' ');
//             putchar(' ');
//         }

//         if (node->NodeType == FX_AST_BLOCK) {
//             puts("[BLOCK]");

//             AstBlock* block = reinterpret_cast<AstBlock*>(node);
//             for (AstNode* child : block->Statements) {
//                 Print(child, depth + 1);
//             }
//             return;
//         }
//         else if (node->NodeType == FX_AST_ACTIONDECL) {
//             AstActionDecl* actiondecl = reinterpret_cast<AstActionDecl*>(node);
//             printf("[ACTIONDECL] ");
//             actiondecl->Name->Print();

//             for (AstNode* param : actiondecl->Params->Statements) {
//                 Print(param, depth + 1);
//             }

//             Print(actiondecl->Block, depth + 1);
//         }
//         else if (node->NodeType == FX_AST_VARDECL) {
//             AstVarDecl* vardecl = reinterpret_cast<AstVarDecl*>(node);

//             printf("[VARDECL] ");
//             vardecl->Name->Print();

//             Print(vardecl->Assignment, depth + 1);
//         }
//         else if (node->NodeType == FX_AST_ASSIGN) {
//             AstAssign* assign = reinterpret_cast<AstAssign*>(node);

//             printf("[ASSIGN] ");

//             assign->Var->Name->Print();
//             Print(assign->Rhs, depth + 1);
//         }
//         else if (node->NodeType == FX_AST_ACTIONCALL) {
//             AstActionCall* actioncall = reinterpret_cast<AstActionCall*>(node);

//             printf("[ACTIONCALL] ");
//             if (actioncall->Action == nullptr) {
//                 printf("{defined externally}");
//             }
//             else {
//                 actioncall->Action->Name->Print(true);
//             }

//             printf(" (%zu params)\n", actioncall->Params.size());
//         }
//         else if (node->NodeType == FX_AST_LITERAL) {
//             AstLiteral* literal = reinterpret_cast<AstLiteral*>(node);

//             printf("[LITERAL] ");
//             literal->Value.Print();
//         }
//         else if (node->NodeType == FX_AST_BINOP) {
//             AstBinop* binop = reinterpret_cast<AstBinop*>(node);

//             printf("[BINOP] ");
//             binop->OpToken->Print();

//             Print(binop->Left, depth + 1);
//             Print(binop->Right, depth + 1);
//         }
//         else if (node->NodeType == FX_AST_COMMANDMODE) {
//             AstCommandMode* command_mode = reinterpret_cast<AstCommandMode*>(node);
//             printf("[COMMANDMODE]\n");

//             Print(command_mode->Node, depth + 1);
//         }
//         else if (node->NodeType == FX_AST_RETURN) {
//             puts("[RETURN]");
//         }
//         else {
//             puts("[UNKNOWN]");
//         }
//         // else if (node->NodeType == FX_AST_)
//     }

// public:
//     // AstBlock* mRootBlock = nullptr;
// };

// /////////////////////////////////////////////
// // Script Bytecode Emitter
// /////////////////////////////////////////////

// enum ScriptRegister : uint8
// {
//     FX_REG_NONE = 0x00,
//     FX_REG_X0,
//     FX_REG_X1,
//     FX_REG_X2,
//     FX_REG_X3,

//     /**
//      * @brief Return address register.
//      */
//     FX_REG_RA,

//     /**
//      * @brief Register that contains the result of an operation.
//      */
//     FX_REG_XR,

//     /**
//      * @brief Register that contains the stack pointer for the VM.
//      */
//     FX_REG_SP,

//     FX_REG_SIZE,
// };

// enum ScriptRegisterFlag : uint16
// {
//     FX_REGFLAG_NONE = 0x00,
//     FX_REGFLAG_X0 = 0x01,
//     FX_REGFLAG_X1 = 0x02,
//     FX_REGFLAG_X2 = 0x04,
//     FX_REGFLAG_X3 = 0x08,
//     FX_REGFLAG_RA = 0x10,
//     FX_REGFLAG_XR = 0x20,
// };

// inline ScriptRegisterFlag operator|(ScriptRegisterFlag a, ScriptRegisterFlag b)
// {
//     return static_cast<ScriptRegisterFlag>(static_cast<uint16>(a) | static_cast<uint16>(b));
// }

// inline ScriptRegisterFlag operator&(ScriptRegisterFlag a, ScriptRegisterFlag b)
// {
//     return static_cast<ScriptRegisterFlag>(static_cast<uint16>(a) & static_cast<uint16>(b));
// }

// struct ScriptBytecodeVarHandle
// {
//     Hash32 HashedName = 0;
//     ScriptValue::ValueType Type = ScriptValue::INT;
//     int64 Offset = 0;

//     uint16 SizeOnStack = 4;
//     uint32 ScopeIndex = 0;
// };

// struct ScriptBytecodeActionHandle
// {
//     Hash32 HashedName = 0;
//     uint32 BytecodeIndex = 0;
// };

// class ScriptBCEmitter
// {
// public:
//     ScriptBCEmitter() = default;

//     void BeginEmitting(AstNode* node);
//     void Emit(AstNode* node);

//     enum RhsMode
//     {
//         RHS_FETCH_TO_REGISTER,

//         /**
//          * @brief Pushes the value to the stack, assuming that the value does not exist yet.
//          */
//         RHS_DEFINE_IN_MEMORY,

//         RHS_ASSIGN_TO_HANDLE,
//     };

//     static ScriptRegister RegFlagToReg(ScriptRegisterFlag reg_flag);
//     static ScriptRegisterFlag RegToRegFlag(ScriptRegister reg);

//     static const char* GetRegisterName(ScriptRegister reg);

//     PagedArray<uint8> mBytecode {};

//     enum VarDeclareMode
//     {
//         DECLARE_DEFAULT,
//         DECLARE_NO_EMIT,
//     };


// private:
//     void EmitBlock(AstBlock* block);
//     void EmitAction(AstActionDecl* action);
//     void DoActionCall(AstActionCall* call);
//     ScriptBytecodeVarHandle* DoVarDeclare(AstVarDecl* decl, VarDeclareMode mode = DECLARE_DEFAULT);
//     void EmitAssign(AstAssign* assign);
//     ScriptBytecodeVarHandle* DefineAndFetchParam(AstNode* param_decl_node);
//     ScriptBytecodeVarHandle* DefineReturnVar(AstVarDecl* decl);

//     ScriptRegister EmitVarFetch(AstVarRef* ref, RhsMode mode);

//     void DoLoad(uint32 stack_offset, ScriptRegister output_reg, bool force_absolute = false);
//     void DoSaveInt32(uint32 stack_offset, uint32 value, bool force_absolute = false);
//     void DoSaveReg32(uint32 stack_offset, ScriptRegister reg, bool force_absolute = false);

//     void EmitPush32(uint32 value);
//     void EmitPush32r(ScriptRegister reg);

//     void EmitPop32(ScriptRegister output_reg);

//     void EmitLoad32(int offset, ScriptRegister output_reg);
//     void EmitLoadAbsolute32(uint32 position, ScriptRegister output_reg);

//     void EmitSave32(int16 offset, uint32 value);
//     void EmitSaveReg32(int16 offset, ScriptRegister reg);

//     void EmitSaveAbsolute32(uint32 offset, uint32 value);
//     void EmitSaveAbsoluteReg32(uint32 offset, ScriptRegister reg);

//     void EmitJumpRelative(uint16 offset);
//     void EmitJumpAbsolute(uint32 position);
//     void EmitJumpAbsoluteReg32(ScriptRegister reg);
//     void EmitJumpCallAbsolute(uint32 position);
//     void EmitJumpReturnToCaller();
//     void EmitJumpCallExternal(Hash32 hashed_name);

//     void EmitMoveInt32(ScriptRegister reg, uint32 value);

//     void EmitParamsStart();
//     void EmitType(ScriptValue::ValueType type);

//     uint32 EmitDataString(char* str, uint16 length);

//     ScriptRegister EmitBinop(AstBinop* binop, ScriptBytecodeVarHandle* handle);

//     ScriptRegister EmitRhs(AstNode* rhs, RhsMode mode, ScriptBytecodeVarHandle* handle);

//     ScriptRegister EmitLiteralInt(AstLiteral* literal, RhsMode mode, ScriptBytecodeVarHandle* handle);
//     ScriptRegister EmitLiteralString(AstLiteral* literal, RhsMode mode, ScriptBytecodeVarHandle* handle);


//     void WriteOp(uint8 base_op, uint8 spec_op);
//     void Write16(uint16 value);
//     void Write32(uint32 value);

//     ScriptRegister FindFreeRegister();

//     ScriptBytecodeVarHandle* FindVarHandle(Hash32 hashed_name);
//     ScriptBytecodeActionHandle* FindActionHandle(Hash32 hashed_name);

//     void PrintBytecode();

//     void MarkRegisterUsed(ScriptRegister reg);
//     void MarkRegisterFree(ScriptRegister reg);

// public:
//     PagedArray<ScriptBytecodeVarHandle> VarHandles;
//     std::vector<ScriptBytecodeActionHandle> ActionHandles;

// private:
//     ScriptRegisterFlag mRegsInUse = FX_REGFLAG_NONE;

//     int64 mStackOffset = 0;
//     uint32 mStackSize = 0;

//     uint16 mScopeIndex = 0;
// };

// class ScriptBCPrinter
// {
// public:
//     ScriptBCPrinter(PagedArray<uint8>& bytecode)
//     {
//         mBytecode = bytecode;
//         mBytecode.bDoNotDestroy = true;
//     }

//     void Print();
//     void PrintOp();


// private:
//     uint16 Read16();
//     uint32 Read32();

//     void DoPush(char* s, uint8 op_base, uint8 op_spec);
//     void DoPop(char* s, uint8 op_base, uint8 op_spec);
//     void DoLoad(char* s, uint8 op_base, uint8 op_spec);
//     void DoArith(char* s, uint8 op_base, uint8 op_spec);
//     void DoSave(char* s, uint8 op_base, uint8 op_spec);
//     void DoJump(char* s, uint8 op_base, uint8 op_spec);
//     void DoData(char* s, uint8 op_base, uint8 op_spec);
//     void DoType(char* s, uint8 op_base, uint8 op_spec);
//     void DoMove(char* s, uint8 op_base, uint8 op_spec);

// private:
//     uint32 mBytecodeIndex = 0;
//     PagedArray<uint8> mBytecode;
// };


// ///////////////////////////////////////////
// // Bytecode VM
// ///////////////////////////////////////////

// struct ScriptVMCallFrame
// {
//     uint32 StartStackIndex = 0;
// };

// class ScriptVM
// {
// public:
//     ScriptVM() = default;

//     void Start(PagedArray<uint8>&& bytecode)
//     {
//         mBytecode = std::move(bytecode);
//         mPushedTypes.Create(64);

//         Stack = FX_SCRIPT_ALLOC_MEMORY(uint8, 1024);
//         // mStackOffset = 0;
//         Registers[FX_REG_SP] = 0;
//         memset(Registers, 0, sizeof(Registers));

//         while (mPC < mBytecode.Size()) {
//             ExecuteOp();
//         }

//         PrintRegisters();
//     }

//     void PrintRegisters();

//     void Push16(uint16 value);
//     void Push32(uint32 value);

//     uint32 Pop32();

// private:
//     void ExecuteOp();

//     void DoPush(uint8 op_base, uint8 op_spec);
//     void DoPop(uint8 op_base, uint8 op_spec);
//     void DoLoad(uint8 op_base, uint8 op_spec);
//     void DoArith(uint8 op_base, uint8 op_spec);
//     void DoSave(uint8 op_base, uint8 op_spec);
//     void DoJump(uint8 op_base, uint8 op_spec);
//     void DoData(uint8 op_base, uint8 op_spec);
//     void DoType(uint8 op_base, uint8 op_spec);
//     void DoMove(uint8 op_base, uint8 op_spec);

//     uint16 Read16();
//     uint32 Read32();

//     ScriptVMCallFrame& PushCallFrame();
//     ScriptVMCallFrame* GetCurrentCallFrame();
//     void PopCallFrame();

//     ScriptExternalFunc* FindExternalAction(Hash32 hashed_name);

// public:
//     // NONE, X0, X1, X2, X3, RA, XR, SP
//     int32 Registers[FX_REG_SIZE];

//     uint8* Stack = nullptr;

//     std::vector<ScriptExternalFunc> mExternalFuncs;

//     PagedArray<uint8> mBytecode;

// private:
//     uint32 mPC = 0;


//     bool mIsInCallFrame = false;

//     ScriptVMCallFrame mCallFrames[8];
//     int mCallFrameIndex = 0;

//     bool mIsInParams = false;
//     PagedArray<ScriptValue::ValueType> mPushedTypes;

//     ScriptValue::ValueType mCurrentType = ScriptValue::NONETYPE;
// };

// ////////////////////////////////////////////////
// // Script Interpreter
// ////////////////////////////////////////////////

// class ScriptInterpreter
// {
// public:
//     ScriptInterpreter() = default;

//     void PushScope();
//     void PopScope();

//     ScriptVar* FindVar(Hash32 hashed_name);
//     ScriptAction* FindAction(Hash32 hashed_name);
//     ScriptExternalFunc* FindExternalAction(Hash32 hashed_name);

//     /**
//      * @brief Evaluates and gets the immediate value if `value` is a reference, or returns the value if it is already
//      * immediate.
//      * @param value The value to query from
//      * @return the immediate(literal) value
//      */
//     const ScriptValue& GetImmediateValue(const ScriptValue& value);

//     void DefineExternalVar(const char* type, const char* name, const ScriptValue& value);

// private:
//     friend class ConfigScript;
//     void Create(AstBlock* root_block);

//     void Visit(AstNode* node);

//     void Interpret();

//     ScriptValue VisitExternalCall(AstActionCall* call, ScriptExternalFunc& func);
//     ScriptValue VisitActionCall(AstActionCall* call);
//     void VisitAssignment(AstAssign* assign);
//     ScriptValue VisitRhs(AstNode* node);

//     bool CheckExternalCallArgs(AstActionCall* call, ScriptExternalFunc& func);


// private:
//     AstNode* mRootBlock = nullptr;

//     bool mInCommandMode = false;

//     std::vector<ScriptExternalFunc> mExternalFuncs;

//     PagedArray<ScriptScope> mScopes;
//     ScriptScope* mCurrentScope = nullptr;
// };
