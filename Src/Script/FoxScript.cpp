#include "FoxScript.hpp"

#include "FoxAst.hpp"
#include "FoxBytecodeCompiler.hpp"
#include "FoxParser.hpp"
#include "FoxVM.hpp"

#include <Core/Defer.hpp>
#include <Core/String.hpp>

namespace fx::script {


static void WB_InitAmmoVars(FoxVM* vm, const SizedArray<FoxValue>& args)
{
    LogInfo("Mag size: {}, Num mags: {}", args[0].Get<int32>(), args[1].Get<int32>());
}

static void WB_InitStatVars(FoxVM* vm, const SizedArray<FoxValue>& args)
{
    LogInfo("Damage: {}", args[0].Get<int32>());
}

FoxScript::FoxScript(const String& path) { Load(path); }

void FoxScript::Load(const String& path)
{
    File fp(path, File::eModType::Read, File::eDataType::Binary);

    if (!fp.IsFileOpen()) {
        LogError("Could not open script file at '{}'", path);
        return;
    }


    Slice<char> file_data = fp.Read<char>();

    Tokenizer tokenizer(file_data.pData, file_data.Size);
    tokenizer.Tokenize();

    for (const Token& token : tokenizer.TokenBuffer) {
        LogInfo("{}", token);
    }

    FoxParser parser {};

    parser.Init(std::move(tokenizer.TokenBuffer));

    FoxAstNode* root_node = parser.Parse();
    if (parser.bHasErrors || root_node == nullptr) {
        LogError("Errors found while parsing script, exitting...");
        return;
    }

    FoxBytecodeCompiler compiler {};
    SizedArray<uint8> bytecode = compiler.Compile(root_node);
    if (compiler.HasErrors()) {
        LogError("Errors found during script compilation, cannot continue");
        return;
    }

    LogInfo("Compiled script {}", path);
    // Script is compiled, we can now start freeing loaded file data.


    FoxBytecodePrinter bc_printer(bytecode);
    bc_printer.Print();

    Vm.InitVM(std::move(bytecode));

    RegisterProc(HashStr32("WB_InitAmmoVars"), false, 2, &WB_InitAmmoVars);
    RegisterProc(HashStr32("WB_InitStatVars"), false, 1, &WB_InitStatVars);

    // Since all we need is the bytecode now, we can destroy the AST.
    FoxAstDestroyer destroyer;
    destroyer.Do(root_node);

    // Call OnLoad if it exists
    CallProc(HashStr32("OnLoad"), {});

    mpSymTick = GetSymbol(HashStr32("OnTick"));

    // {
    //     gEnginePool->Free(file_data.pData);

    //     for (char* ptr : tokenizer.DataPtrs) {
    //         gEnginePool->Free(ptr);
    //     }
    // }
}

void FoxScript::PushValue(const FoxValue& value) { Vm.Push32(value.Type, value.AsUInt()); }

void FoxScript::RegisterProc(Hash32 name_hash, bool returns_value, uint32 parameter_count, VMExternalFunction function)
{
    VMExternalProcEntry entry {
        .pFunc = function,
        .ArgCount = parameter_count,
        .bReturnsValue = returns_value,
    };

    Vm.ExternalProcs[name_hash] = entry;

    LogInfo("Registered external function {}", name_hash);
}

FoxValue FoxScript::CallProc(FoxSymbol* sym, const SizedArray<FoxValue>& args)
{
    if (!sym) {
        return FoxValue::scNone;
    }

    Vm.ScopeIndex++;

    Vm.PC = sym->Offset;

    for (uint32 arg_index = 0; arg_index < args.Size; arg_index++) {
        PushValue(args[arg_index]);
    }

    while (Vm.PC < Vm.mBytecode.Size) {
        Vm.ExecuteOp();

        if (Vm.ScopeIndex <= 0) {
            break;
        }
    }

    if (Vm.bReturnValueOnStack) {
        return FoxValue::NumberFromRaw(Vm.LastPushType, Vm.Pop32());
    }

    return FoxValue::scNone;
}

FoxValue FoxScript::CallProc(const Hash32 name_hash, const SizedArray<FoxValue>& args)
{
    return CallProc(GetSymbol(name_hash), args);
}


void FoxScript::SetGlobal(const Hash32 name_hash, const FoxValue& value) { Vm.Globals[name_hash] = value; }

FoxValue FoxScript::GetGlobal(const Hash32 name_hash) const
{
    auto it = Vm.Globals.find(name_hash);
    if (it == Vm.Globals.end()) {
        return FoxValue::scNone;
    }

    return it->second;
}


} // namespace fx::script
