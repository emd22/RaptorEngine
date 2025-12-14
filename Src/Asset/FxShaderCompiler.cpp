#include "FxShaderCompiler.hpp"

#include <ThirdParty/Slang/slang-com-ptr.h>
#include <ThirdParty/Slang/slang.h>

#include <Asset/FxDataPack.hpp>
#include <Core/FxBasicDb.hpp>
#include <Core/FxFile.hpp>
#include <Core/FxFilesystemIO.hpp>
#include <Core/FxStackArray.hpp>
#include <Renderer/Backend/RxShader.hpp>
#include <string>

static FxBasicDb sShaderCompileDb;

static const char* scVertexEntrypoint = "VertexMain";
static const char* scFragmentEntrypoint = "FragmentMain";

static Slang::ComPtr<slang::IGlobalSession>& GetSlangGlobalSession()
{
    thread_local Slang::ComPtr<slang::IGlobalSession> global_session;

    // If the session has not been created, create it
    if (!global_session.get()) {
        slang::createGlobalSession(global_session.writeRef());
    }

    return global_session;
}

static void CreateSlangSession(Slang::ComPtr<slang::ISession>& local_session, const FxSizedArray<FxShaderMacro>& macros)
{
    Slang::ComPtr<slang::IGlobalSession>& global_session = GetSlangGlobalSession();

    slang::TargetDesc target_desc {
        .format = SLANG_SPIRV,
        .profile = global_session->findProfile("spirv_1_4"),
    };

    FxSizedArray<slang::PreprocessorMacroDesc> preprocessor_macros;

    if (macros.IsNotEmpty()) {
        FxLogDebug("Creating preprocessor list of {} macros", macros.Size);
        preprocessor_macros.InitCapacity(macros.Size);
        for (const FxShaderMacro& macro : macros) {
            slang::PreprocessorMacroDesc* ppmacro = preprocessor_macros.Insert();
            ppmacro->name = macro.pcName;
            ppmacro->value = macro.pcValue;
        }
    }

    FxStackArray<slang::CompilerOptionEntry, 2> compiler_options = {
        {
            slang::CompilerOptionName::EmitSpirvDirectly,
            { slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr },
        },
        {
            slang::CompilerOptionName::LanguageVersion,
            { slang::CompilerOptionValueKind::Int, 2026, 0, nullptr, nullptr },
        }
    };

    slang::SessionDesc session_desc {
        .targets = &target_desc,
        .targetCount = 1,

        .preprocessorMacros = preprocessor_macros.pData,
        .preprocessorMacroCount = static_cast<SlangInt>(preprocessor_macros.Size),

        .compilerOptionEntries = compiler_options.pData,
        .compilerOptionEntryCount = compiler_options.Size,
    };

    global_session->createSession(session_desc, local_session.writeRef());
}

static Slang::ComPtr<slang::ISession>& GetDefaultSlangSession()
{
    thread_local Slang::ComPtr<slang::ISession> local_session;

    if (!local_session.get()) {
        CreateSlangSession(local_session, {});
    }

    return local_session;
}

static void PrintSlangDiagnostics(Slang::ComPtr<slang::IBlob>& diagnostic_blob)
{
    if (diagnostic_blob != nullptr) {
        FxLogError("Diagnostic Info:\n {}", reinterpret_cast<const char*>(diagnostic_blob->getBufferPointer()));
    }
}

static void RecordShaderCompileTime(FxHash64 entry_id, const char* path)
{
    FxLogInfo("Logging compile time for {} ({})", path, entry_id);

    uint64 modification_time = FxFilesystemIO::FileGetLastModified(path);
    sShaderCompileDb.WriteEntry(FxBasicDbEntry { .KeyHash = entry_id, .Value = std::to_string(modification_time) });
}

static bool IsShaderUpToDate(FxHash64 entry_id, const char* path)
{
    FxBasicDbEntry* entry = sShaderCompileDb.FindEntry(entry_id);
    if (!entry) {
        return false;
    }

    uint64 latest_modification_time = FxFilesystemIO::FileGetLastModified(path);

    if (std::stoull(entry->Value) != latest_modification_time) {
        return false;
    }

    return true;
}

void FxShaderCompiler::CompileAllShaders(const char* folder_path)
{
    // FxPagedArray<std::string> shader_paths = FxFilesystemIO::DirListIfHasExtension(folder_path, ".slang", true);

    // if (shader_paths.IsEmpty()) {
    //     return;
    // }


    // for (const std::string& shader_path : shader_paths) {
    //     const std::string path_to_compile = std::format("{}{}.slang", folder_path, shader_path);
    //     const std::string output_path = std::format("{}Spirv/{}.spv", folder_path, shader_path);

    //     FxDataPack shader_pack;
    //     FxShaderCompiler::Compile(path_to_compile.c_str(), shader_pack, false);
    // }

    // sShaderCompileDb.SaveToFile();
}

static FxSlice<uint8> CreateAlignedBufferForSpirv(const Slang::ComPtr<slang::IBlob>& spirv_code)
{
    uint32 unaligned_size = spirv_code->getBufferSize();
    uint32 buffer_size = FxMath::AlignValue<4>(unaligned_size);

    uint8* buffer = FxMemPool::Alloc<uint8>(buffer_size);

    memcpy(buffer, spirv_code->getBufferPointer(), unaligned_size);

    if (unaligned_size < buffer_size) {
        memset(buffer + unaligned_size, 0, buffer_size - unaligned_size);
    }

    return FxMakeSlice(buffer, buffer_size);
}

void FxShaderCompiler::Compile(const char* path, FxDataPack& pack, const FxSizedArray<FxShaderMacro>& macros,
                               bool do_db_flush)
{
    if (!sShaderCompileDb.IsOpen()) {
        sShaderCompileDb.Open(FX_BASE_DIR "/Shaders/ShaderCompileDb.fxdb");
    }

    FxHash64 compile_entry_id = FxHashStr64(path, FxHashData64(FxSlice<FxShaderMacro>(macros)));

    if (IsShaderUpToDate(compile_entry_id, path)) {
        FxLogInfo("Shader {} is up to date!", path);
        return;
    }

    bool has_vertex_shader = false;
    bool has_fragment_shader = false;

    slang::IModule* module = nullptr;
    Slang::ComPtr<slang::ISession>& session = GetDefaultSlangSession();

    // Create a new slang session with those macros defined
    if (macros.IsNotEmpty()) {
        CreateSlangSession(session, macros);
    }

    Slang::ComPtr<slang::IBlob> diagnostic_blob;

    module = session->loadModule(path, diagnostic_blob.writeRef());

    if (diagnostic_blob != nullptr) {
        FxLogError("Error loading Slang module for {}", path);
        PrintSlangDiagnostics(diagnostic_blob);
        return;
    }

    Slang::ComPtr<slang::IEntryPoint> vertex_entry_point;
    Slang::ComPtr<slang::IEntryPoint> fragment_entry_point;

    module->findEntryPointByName(scVertexEntrypoint, vertex_entry_point.writeRef());
    module->findEntryPointByName(scFragmentEntrypoint, fragment_entry_point.writeRef());

    FxStackArray<slang::IComponentType*, 3> component_types;
    component_types.Insert(module);

    // Add each entry point if they are defined
    if (vertex_entry_point.get()) {
        component_types.Insert(vertex_entry_point);
        has_vertex_shader = true;
    }

    if (fragment_entry_point.get()) {
        component_types.Insert(fragment_entry_point);
        has_fragment_shader = true;
    }

    Slang::ComPtr<slang::IComponentType> composed_program;

    SlangResult result = session->createCompositeComponentType(component_types.pData, component_types.Size,
                                                               composed_program.writeRef(), diagnostic_blob.writeRef());

    if (SLANG_FAILED(result) || diagnostic_blob != nullptr) {
        PrintSlangDiagnostics(diagnostic_blob);
    }

    FxLogInfo("COMPILING SHADER WITH {} MACROS", macros.Size);

    Slang::ComPtr<slang::IBlob> spirv_code;

    // Write the vertex shader code
    if (has_vertex_shader) {
        result = composed_program->getEntryPointCode(0, 0, spirv_code.writeRef(), diagnostic_blob.writeRef());

        if (SLANG_FAILED(result)) {
            FxLogError("Could not get vertex shader compiled SPIRV(Path={})", path);
            PrintSlangDiagnostics(diagnostic_blob);
            return;
        }

        {
            // FxFile out_data("testdataf.bin", FxFile::eWrite, FxFile::eBinary);
            // out_data.Write(spirv_code->getBufferPointer(), spirv_code->getBufferSize());
            // out_data.Close();

            constexpr FxHash64 sPrefixHashVS = FxHashStr64("VERTSHADER");

            // FxSlice<const uint8> buffer_slice = FxMakeSlice<const uint8>(
            // reinterpret_cast<const uint8*>(spirv_code->getBufferPointer()), spirv_code->getBufferSize());

            FxSlice<uint8> aligned_buffer = CreateAlignedBufferForSpirv(spirv_code);

            // FxLogInfo("COMPILE Data({})", aligned_buffer.Size);


            // const void* data_ptr = spirv_code->getBufferPointer();
            // FxSlice<uint8> data_slice = FxMakeSlice<uint8>(static_cast<uint8*>(const_cast<void*>(data_ptr)),
            //                                                spirv_code->getBufferSize());
            pack.AddEntry(FxHashData64(FxSlice<FxShaderMacro>(macros), sPrefixHashVS), aligned_buffer);

            FxMemPool::Free(aligned_buffer.pData);

            // std::string vertex_path = output_path;
            // vertex_path += "_vs";

            // FxFile output_file(vertex_path.c_str(), FxFile::eWrite, FxFile::eBinary);

            // output_file.Write(spirv_code->getBufferPointer(), spirv_code->getBufferSize());

            // output_file.Close();
        }

        FxLogInfo("Compiled vertex shader {} (Size={})", path, spirv_code->getBufferSize());
    }

    // Clear the spirv_code
    spirv_code.setNull();

    // Write the fragment shader code
    if (has_fragment_shader) {
        result = composed_program->getEntryPointCode(1, 0, spirv_code.writeRef(), diagnostic_blob.writeRef());

        if (SLANG_FAILED(result)) {
            FxLogError("Could not get fragment shader compiled SPIRV(Path={})", path);
            PrintSlangDiagnostics(diagnostic_blob);
            return;
        }

        {
            constexpr FxHash64 sPrefixHashFS = FxHashStr64("FRAGSHADER");


            // FxSlice<const uint8> buffer_slice = FxMakeSlice<const uint8>(
            // reinterpret_cast<const uint8*>(spirv_code->getBufferPointer()), spirv_code->getBufferSize());

            FxSlice<uint8> aligned_buffer = CreateAlignedBufferForSpirv(spirv_code);


            // const void* data_ptr = spirv_code->getBufferPointer();
            // FxSlice<uint8> data_slice = FxMakeSlice<uint8>(static_cast<uint8*>(const_cast<void*>(data_ptr)),
            //                                                spirv_code->getBufferSize());

            FxSlice<FxShaderMacro> macros_slice = FxSlice<FxShaderMacro>(macros);
            pack.AddEntry(FxHashData64(macros_slice, sPrefixHashFS), aligned_buffer);

            FxMemPool::Free(aligned_buffer.pData);


            // std::string fragment_path = output_path;
            // fragment_path += "_fs";

            // FxFile output_file(fragment_path.c_str(), FxFile::eWrite, FxFile::eBinary);

            // output_file.Write(spirv_code->getBufferPointer(), spirv_code->getBufferSize());

            // output_file.Close();
        }

        FxLogInfo("Compiled fragment shader {} (Size={})", path, spirv_code->getBufferSize());
    }

    RecordShaderCompileTime(compile_entry_id, path);

    if (do_db_flush) {
        sShaderCompileDb.SaveToFile();
    }
}

void FxShaderCompiler::Destroy()
{
    sShaderCompileDb.Close();

    // GetSlangSession().setNull();
    // GetSlangGlobalSession().setNull();
}
