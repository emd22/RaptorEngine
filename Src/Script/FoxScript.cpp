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

void FoxScript::Load(const String& path)
{
    FoxParser parser {};

    parser.LoadFile(path);

    Defer([&]() { gEnginePool->Free(parser.pFileData); });


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

    FoxBytecodePrinter bc_printer(bytecode);
    bc_printer.Print();

    Vm.InitVM(std::move(bytecode));

    RegisterFunction(HashStr32("WB_InitAmmoVars"), false, 2, &WB_InitAmmoVars);
    RegisterFunction(HashStr32("WB_InitStatVars"), false, 1, &WB_InitStatVars);

    // Since all we need is the bytecode now, we can destroy the AST.
    FoxAstDestroyer destroyer;
    destroyer.Do(root_node);

    CallIfExists(HashStr32("OnLoad"), {});
}

void FoxScript::PushValue(const FoxValue& value) { Vm.Push32(value.Type, value.AsUInt()); }

void FoxScript::RegisterFunction(Hash32 name_hash, bool returns_value, uint32 parameter_count,
                                 VMExternalFunction function)
{
    VMExternalFunctionEntry entry {
        .pFunc = function,
        .ArgCount = parameter_count,
        .bReturnsValue = returns_value,
    };

    Vm.ExternalFunctions[name_hash] = entry;

    LogInfo("Registered external function {}", name_hash);
}

FoxValue FoxScript::Call(FoxSymbol* sym, const SizedArray<FoxValue>& args)
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


FoxValue FoxScript::CallIfExists(const Hash32 name_hash, const SizedArray<FoxValue>& args)
{
    FoxSymbol* sym = GetSymbol(name_hash);
    if (sym) {
        return Call(sym, args);
    }

    return FoxValue::scNone;
}


} // namespace fx::script
