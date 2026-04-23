#pragma once


#include "ScriptAst.hpp"

#include <Core/Hash.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Types.hpp>
#include <Util/Tokenizer.hpp>

namespace fx {


namespace script {

struct FoxAstBlock;
struct FoxAstFunctionDecl;
struct FoxScope;

/**
 * @brief Data is accessible from a label, such as a variable or an function.
 */
struct FoxLabelledData
{
    Hash32 HashedName = 0;
    Token* Name = nullptr;

    FoxScope* Scope = nullptr;
};

struct FoxFunction : public FoxLabelledData
{
    FoxFunction(Token* name, FoxScope* scope, FoxAstBlock* block, FoxAstFunctionDecl* declaration)
    {
        HashedName = name->GetHash();
        Name = name;
        Scope = scope;
        pBlock = block;
        pDeclaration = declaration;
    }

    FoxAstFunctionDecl* pDeclaration = nullptr;
    FoxAstBlock* pBlock = nullptr;
};


struct FoxVar : public FoxLabelledData
{
    Token* Type = nullptr;
    FoxValue Value;

    bool IsExternal = false;

    void Print() const
    {
        printf("[Var] Type: %.*s, Name: %.*s (Hash:%u)", Type->Length, Type->Start, Name->Length, Name->Start,
               Name->GetHash());
        Value.Print();
    }

    FoxVar() {}

    FoxVar(Token* type, Token* name, FoxScope* scope, bool is_external = false) : Type(type)
    {
        this->HashedName = name->GetHash();
        this->Name = name;
        this->Scope = scope;
        IsExternal = is_external;
    }

    FoxVar(const FoxVar& other)
    {
        HashedName = other.HashedName;
        Type = other.Type;
        Name = other.Name;
        Value = other.Value;
        IsExternal = other.IsExternal;
    }

    FoxVar& operator=(FoxVar&& other) noexcept
    {
        HashedName = other.HashedName;
        Type = other.Type;
        Name = other.Name;
        Value = other.Value;
        IsExternal = other.IsExternal;

        Name = nullptr;
        Type = nullptr;
        HashedName = 0;

        return *this;
    }

    ~FoxVar()
    {
        if (!IsExternal) {
            return;
        }

        // Free tokens allocated by external variables
        if (Type && Type->Start) {
            free(Type->Start);
        }

        if (this->Name && this->Name->Start) {
            free(this->Name->Start);
        }
    }
};


struct FoxScope
{
    PagedArray<FoxVar> Vars;
    PagedArray<FoxFunction> Functions;

    FoxScope* Parent = nullptr;

    // This points to the return value for the current scope. If an function returns a value,
    // this will be set to the variable that holds its value. This is interpreter only.
    FoxVar* ReturnVar = nullptr;

    void PrintAllVarsInScope()
    {
        puts("\n=== SCOPE ===");
        for (FoxVar& var : Vars) {
            var.Print();
        }
    }

    FoxVar* FindVarInScope(Hash32 hashed_name) { return FindInScope<FoxVar>(hashed_name, Vars); }

    FoxFunction* FindFunctionInScope(Hash32 hashed_name) { return FindInScope<FoxFunction>(hashed_name, Functions); }

    template <typename T>
        requires std::is_base_of_v<FoxLabelledData, T>
    T* FindInScope(Hash32 hashed_name, const PagedArray<T>& buffer)
    {
        for (T& var : buffer) {
            if (var.HashedName == hashed_name) {
                return &var;
            }
        }

        return nullptr;
    }
};

} // namespace script

} // namespace fx
