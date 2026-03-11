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

    FxStackArray<slang::CompilerOptionEntry, 3> compiler_options = {
        {
            slang::CompilerOptionName::EmitSpirvDirectly,
            { slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr },
        },
        {
            // Use "slang 2026" profile to warn for legacy code
            slang::CompilerOptionName::LanguageVersion,
            { slang::CompilerOptionValueKind::Int, 2026, 0, nullptr, nullptr },
        },
        {
            // Column-major matrices
            slang::CompilerOptionName::MatrixLayoutColumn,
            { slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr },
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

static void RecordShaderCompileTime(const char* path)
{
    FxHash64 compile_id = FxHashStr64(path);
    FxLogInfo("Logging compile time for {} ({})", path, compile_id);

    uint64 modification_time = FxFilesystemIO::FileGetLastModified(path);
    sShaderCompileDb.WriteEntry(FxBasicDbEntry { .KeyHash = compile_id, .Value = std::to_string(modification_time) });
}

static bool IsShaderUpToDate(FxHash64 entry_id, const char* path)
{
    FxBasicDbEntry* entry = sShaderCompileDb.FindEntry(entry_id);
    if (!entry) {
        FxLogWarning("Cannot find entry for shader!");
        return false;
    }

    uint64 latest_modification_time = FxFilesystemIO::FileGetLastModified(path);

    if (std::stoull(entry->Value) < latest_modification_time) {
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


static FxSlice<uint8> CreateAlignedBufferForSpirv(const RxShaderOutline& outline,
                                                  Slang::ComPtr<slang::IBlob> spirv_code)
{
    const uint32 reflection_size = outline.GetReflectionSize();
    const uint32 total_buffer_size = reflection_size + FxMath::AlignValue<4>(spirv_code->getBufferSize());

    uint8* big_buffer = FxMemPool::Alloc<uint8>(total_buffer_size);

    // Write the reflection data (shader outline) out to the buffer
    outline.WriteToBuffer(reinterpret_cast<uint32*>(big_buffer));

    // Write compiled SPIRV data after the reflection data
    uint8* compiled_data = big_buffer + reflection_size;
    memcpy(compiled_data, spirv_code->getBufferPointer(), spirv_code->getBufferSize());

    return FxMakeSlice(big_buffer, total_buffer_size);
}


bool FxShaderCompiler::IsOutOfDate(const char* path)
{
    if (!sShaderCompileDb.IsOpen()) {
        sShaderCompileDb.Open(FX_BASE_DIR "/Shaders/LastUpdated.fxd");
    }

    FxHash64 compile_entry_id = FxHashStr64(path);

    return !IsShaderUpToDate(compile_entry_id, path);
}

bool FxShaderCompiler::CompileIfOutOfDate(const char* path, FxDataPack& pack, const FxSizedArray<FxShaderMacro>& macros)
{
    bool out_of_date = IsOutOfDate(path);

    if (out_of_date) {
        Compile(path, pack, macros);
    }

    return out_of_date;
}

#define WRITE_COMPILED_PROGRAM(index_, shader_type_)                                                                   \
    if (WriteCompiledProgram(index_, shader_type_) == Result::eFailed) {                                               \
        return Result::eFailed;                                                                                        \
    }

static bool UpdateFromUserAttributes(slang::TypeReflection* type)
{
    uint32 num_attrs = type->getUserAttributeCount();

    // constexpr FxHash32 cDynamic = FxHashStr32("FxDynamic");

    for (uint32 i = 0; i < num_attrs; i++) {
        slang::Attribute* attr = type->getUserAttributeByIndex(i);

        // Not enough attributes yet to justify the hash and iteration cost
        if (!strcmp(attr->getName(), "FxDynamic")) {
            // desc_entry.bUseDynamicType = true;
            return true;
        }


        // const FxHash32 name_hash = FxHashStr32(attr->getName());

        // switch (name_hash) {
        // case cDynamic:
        //     desc_entry.bUseDynamicType = true;
        //     break;
        // default:;
        // }
    }
    return false;
}

static RxShaderDescriptorType UpdateFromUserType(slang::TypeReflection* type)
{
    SlangResourceShape shape = type->getResourceShape();

    switch ((shape & SLANG_RESOURCE_BASE_SHAPE_MASK)) {
    case SLANG_STRUCTURED_BUFFER:
        return RxShaderDescriptorType::eStructuredBuffer;
        break;
    case SLANG_TEXTURE_2D: // Same as SLANG_SAMPLER
        return RxShaderDescriptorType::eSampler2D;
        break;
    default:
        FxLogError("Cannot find type for descriptor table slot in shader!");
        break;
    }

    return RxShaderDescriptorType::eStructuredBuffer;
}


static RxShaderOutline GetProgramReflection(Slang::ComPtr<slang::IComponentType>& composed_program,
                                            RxShaderType shader_type)
{
    RxShaderOutline outline;

    slang::ProgramLayout* program_layout = composed_program->getLayout(static_cast<uint32>(shader_type));
    const uint32 num_uniforms = program_layout->getParameterCount();

    FxLogInfo("Number of Uniforms: {}", num_uniforms);

    for (uint32 i = 0; i < num_uniforms; i++) {
        slang::VariableLayoutReflection* var_layout = program_layout->getParameterByIndex(i);

        const char* name = var_layout->getName();
        slang::ParameterCategory category = var_layout->getCategory();

        slang::TypeLayoutReflection* type_layout = var_layout->getTypeLayout();

        uint32 binding = var_layout->getOffset(SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT);
        uint32 set = var_layout->getBindingSpace(SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT);

        switch (category) {
        // Constant buffers
        case slang::ParameterCategory::PushConstantBuffer: {
            uint32 pc_size = static_cast<uint32>(type_layout->getSize(SLANG_PARAMETER_CATEGORY_PUSH_CONSTANT_BUFFER));
            FxLogInfo("PC size: {}", pc_size);
            outline.PushConstantSizes[static_cast<uint32>(shader_type)] = pc_size;
        } break;

        // Uniform buffers
        case slang::ParameterCategory::Uniform:
            outline.AddDescriptor(RxShaderDescriptorType::eUniformBuffer, false, set, binding);
            break;

        // Samplers, SSBOs
        case slang::ParameterCategory::DescriptorTableSlot: {
            slang::TypeReflection* type = var_layout->getType();
            outline.AddDescriptor(UpdateFromUserType(type), UpdateFromUserAttributes(type), set, binding);
        } break;

        // Vertex attributes
        case slang::ParameterCategory::VaryingInput:
            break;

        default:;
        }


        FxLogInfo("Found uniform '{}' at (set={}, binding={})", name, set, binding);
    }

    return outline;
}

FxShaderCompiler::Result FxShaderCompiler::Compile(const char* path, FxDataPack& pack,
                                                   const FxSizedArray<FxShaderMacro>& macros, bool do_db_flush)
{
    if (!sShaderCompileDb.IsOpen()) {
        sShaderCompileDb.Open(FX_BASE_DIR "/Shaders/LastUpdated.fxd");
    }

    FxLogInfo("Compiling shader {} with {} macros", path, macros.Size);

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
        return Result::eFailed;
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

    Slang::ComPtr<slang::IBlob> spirv_code;

    auto WriteCompiledProgram = [&](uint32 entry_index, RxShaderType shader_type) -> Result
    {
        result = composed_program->getEntryPointCode(entry_index, 0, spirv_code.writeRef(), diagnostic_blob.writeRef());

        if (SLANG_FAILED(result)) {
            FxLogError("Could not get {} shader SPIRV (Path={})", RxShaderUtil::TypeToName(shader_type), path);
            PrintSlangDiagnostics(diagnostic_blob);
            return Result::eFailed;
        }

        {
            RxShaderOutline refl = GetProgramReflection(composed_program, shader_type);
            FxSlice<uint8> aligned_buffer = CreateAlignedBufferForSpirv(refl, spirv_code);
            pack.AddEntry(RxShader::GenerateShaderId(shader_type, macros), aligned_buffer);
            FxMemPool::Free(aligned_buffer.pData);
        }

        FxLogInfo("Compiled {} shader {} (Size={})", RxShaderUtil::TypeToName(shader_type), path,
                  spirv_code->getBufferSize());

        spirv_code.setNull();

        return Result::eSuccess;
    };

    // Write the compiled programs to the DataPack

    WRITE_COMPILED_PROGRAM(0, RxShaderType::eVertex);
    WRITE_COMPILED_PROGRAM(1, RxShaderType::eFragment);

    FxLogDebug("Compiled shader pack (HasVertex={}, HasFragment={})", has_vertex_shader, has_fragment_shader);

    RecordShaderCompileTime(path);

    if (do_db_flush) {
        sShaderCompileDb.SaveToFile();
    }

    return Result::eSuccess;
}

void FxShaderCompiler::Destroy()
{
    sShaderCompileDb.Close();

    // GetSlangSession().setNull();
    // GetSlangGlobalSession().setNull();
}
