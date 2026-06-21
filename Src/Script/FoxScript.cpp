#include "FoxScript.hpp"

#include "FoxAst.hpp"
#include "FoxBytecodeCompiler.hpp"
#include "FoxParser.hpp"
#include "FoxVM.hpp"

#include <SDL3/SDL.h>

#include <Core/Defer.hpp>
#include <Core/String.hpp>
#include <chrono>

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

SizedArray<uint8> FoxScript::Compile(const String& path)
{
    File fp(path, File::eModType::Read, File::eDataType::Binary);

    if (!fp.IsFileOpen()) {
        LogError(LC_SCRIPT, "Could not open script file at '{}'", path);
        return SizedArray<uint8>();
    }

    Slice<char> file_data = fp.Read<char>();

    Tokenizer tokenizer(file_data.pData, file_data.Size);
    tokenizer.SetFileExtension(".fox");
    tokenizer.Tokenize();

    // for (const Token& token : tokenizer.TokenBuffer) {
    //     LogInfo("{}", token);
    // }

    FoxParser parser {};

    parser.Init(std::move(tokenizer.TokenBuffer));

    FoxAstNode* root_node = parser.Parse();
    if (parser.bHasErrors || root_node == nullptr) {
        LogError(LC_SCRIPT, "Errors found while parsing script, exitting...");
        return SizedArray<uint8>();
    }

    FoxAstPrinter printer(static_cast<FoxAstBlock*>(root_node));
    printer.Print(root_node);

    FoxBytecodeCompiler compiler {};
    compiler.WriteHeaderFile(path, static_cast<FoxAstBlock*>(root_node));

    SizedArray<uint8> bytecode = compiler.Compile(root_node);
    if (compiler.HasErrors()) {
        LogError(LC_SCRIPT, "Errors found during script compilation, cannot continue");
        return SizedArray<uint8>();
    }

    Path bytecode_path(path);
    bytecode_path.DirDown("Out");
    bytecode_path.SetExtension(".fsb");
    bytecode_path.CreateDirs();

    {
        LogInfo(LC_SCRIPT, "Write bytecode to {}", bytecode_path.Str());
        File bytecode_file(bytecode_path.Str(), File::eModType::Write, File::eDataType::Binary);
        bytecode_file.Write(bytecode.pData, bytecode.Size);
        bytecode_file.Close();
    }

    LogInfo(LC_SCRIPT, "Compiled script {}", path);

    // Since all we need is the bytecode now, we can destroy the AST.
    FoxAstDestroyer destroyer;
    destroyer.Do(root_node);

    FoxBytecodePrinter bc_printer(bytecode);
    bc_printer.Print();

    return std::move(bytecode);
}

void FoxScript::Load(const String& path)
{
    SizedArray<uint8> bytecode = Compile(path);


    Vm.InitVM(std::move(bytecode), nullptr);

    RegisterProc(HashStr32("WB_InitAmmoVars"), eFoxProcFlags::None, { eFoxType::INT, eFoxType::INT }, &WB_InitAmmoVars);
    RegisterProc(HashStr32("WB_InitStatVars"), eFoxProcFlags::None, { eFoxType::INT }, &WB_InitStatVars);


    // If there is an init function, call it
    CallProc(GetSymbol("init"), {});
}

void FoxScript::PushValue(const FoxValue& value) { Vm.Push32(value.Type, value.AsUInt()); }

void FoxScript::RegisterProc(Hash32 name_hash, eFoxProcFlags flags, const SizedArray<eFoxType> arg_types,
                             VMExternalFunction function)
{
    VMExternalProcEntry entry { .pFunc = function, .ArgTypes = SizedArray<eFoxType>::Clone(arg_types), .Flags = flags };

    Vm.ExternalProcs[name_hash] = std::move(entry);

    LogInfo("Registered external function {}", name_hash);
}

FoxValue FoxScript::CallProc(FoxSymbol* sym, const SizedArray<FoxValue>& args)
{
    if (!sym) {
        return FoxValue::scNone;
    }

    Vm.PushReturnAddr(0);

    ++Vm.ScopeIndex;
    Vm.PC = sym->Offset;

    for (uint32 arg_index = 0; arg_index < args.Size; arg_index++) {
        PushValue(args[arg_index]);
    }

    return Resume();
}

FoxValue FoxScript::Update() { return Vm.Update(); }

FoxValue FoxScript::Resume() { return Vm.Resume(); }


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
