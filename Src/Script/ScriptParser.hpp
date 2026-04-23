#pragma once

#include "ScriptAst.hpp"
#include "ScriptVar.hpp"

#include <Util/Tokenizer.hpp>


#define FX_SCRIPT_SCOPE_GLOBAL_VARS_START_SIZE 32
#define FX_SCRIPT_SCOPE_LOCAL_VARS_START_SIZE  16

#define FX_SCRIPT_SCOPE_GLOBAL_ACTIONS_START_SIZE 32
#define FX_SCRIPT_SCOPE_LOCAL_ACTIONS_START_SIZE  32

#define FX_SCRIPT_VAR_RETURN_VAL "__ReturnVal__"

// class FoxInterpreter;

namespace fx::script {

class FoxScript
{
    using TT = eTokenType;

public:
    FoxScript() = default;

    void LoadFile(const char* path);

    void PushScope();
    void PopScope();

    FoxVar* FindVar(Hash32 hashed_name);

    FoxFunction* FindFunction(Hash32 hashed_name);
    // FoxExternalFunc* FindExternalFunction(FoxHash hashed_name);

    FoxAstNode* TryParseKeyword(FoxAstBlock* parent_block);

    FoxAstAssign* TryParseAssignment(Token* var_name);

    FoxValue ParseValue();

    FoxAstFunctionDecl* ParseProcedureDeclare();
    FoxAstFunctionDecl* ParseExtfnDeclare();

    FoxAstNode* ParseRhs();
    FoxAstFunctionCall* ParseFunctionCall();

    /**
     * @brief Declares a variable for internal uses as if it was declared in the script.
     * @param name Name of the variable
     * @param type The name of the type
     * @param scope The scope the variable will be declared in
     * @return
     */
    FoxAstVarDecl* InternalVarDeclare(Token* name_token, Token* type_token, FoxScope* scope = nullptr);
    FoxAstVarDecl* ParseVarDeclare(FoxScope* scope = nullptr);

    FoxAstBlock* ParseBlock();

    FoxAstNode* ParseStatement(FoxAstBlock* parent_block);
    FoxAstNode* ParseStatementAsCommand(FoxAstBlock* parent_block);

    FoxAstBlock* Parse();

    /**
     * @brief Parses and executes a script.
     * @param interpreter The interpreter to execute with
     */
    void Execute();

    /**
     * @brief Executes a command on a script. Defaults to parsing with command style syntax.
     * @param command The command to execute on the script.
     * @return If the command has been executed
     */
    // bool ExecuteUserCommand(const char* command, FoxInterpreter& interpreter);

    Token& GetToken(int offset = 0);
    Token& EatToken(TT token_type);

    void PrintFunctionTable(const FoxScope& scope) const;

    // void RegisterExternalFunc(FoxHash func_name, std::vector<FoxValue::ValueType> param_types,
    // FoxExternalFunc::FuncType func, bool is_variadic);

    void DefineExternalVar(const char* type, const char* name, const FoxValue& value);

    eFoxType LabelToType(Token* token);

private:
    template <typename T>
        requires std::is_base_of_v<FoxLabelledData, T>
    T* FindLabelledData(Hash32 hashed_name, PagedArray<T>& buffer)
    {
        FoxScope* scope = mCurrentScope;

        while (scope) {
            T* var = scope->FindInScope<T>(hashed_name, buffer);
            if (var) {
                return var;
            }

            scope = scope->Parent;
        }

        return nullptr;
    }

    // void DefineDefaultExternalFunctions();

    Token* CreateTokenFromString(eTokenType type, const char* text);
    void CreateInternalVariableTokens();

private:
    PagedArray<FoxScope> mScopes;
    FoxScope* mCurrentScope = nullptr;

    // std::vector<FoxExternalFunc> mExternalFuncs;

    std::vector<FoxAstDocComment*> CurrentDocComments;

    FoxAstBlock* mRootBlock = nullptr;

    bool mHasErrors = false;
    bool mInCommandMode = false;

    char* mpFileData;
    PagedArray<Token> mTokens = {};
    uint32 mTokenIndex = 0;

    // Name tokens for internal variables
    Token* mTokenReturnVar = nullptr;
};

} // namespace fx::script
