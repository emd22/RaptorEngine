#include "RxShader.hpp"

#include "FxEngine.hpp"

#include <Asset/AxPaths.hpp>
#include <Asset/FxShaderCompiler.hpp>
#include <Core/FxFile.hpp>
#include <Core/FxTypes.hpp>
#include <Core/MemPool/FxMemPool.hpp>
#include <Math/FxMathUtil.hpp>
#include <Renderer/RxGlobals.hpp>
#include <Renderer/RxRenderBackend.hpp>


FxHash64 RxShader::GenerateShaderId(RxShaderType type, const FxSizedArray<FxShaderMacro>& macros)
{
    FxHash64 hash = FX_HASH64_FNV1A_INIT;

    constexpr FxHash64 cPrefixHashFS = FxHashStr64("FRAGSHADER");
    constexpr FxHash64 cPrefixHashVS = FxHashStr64("VERTSHADER");

    if (type == RxShaderType::eFragment) {
        hash = cPrefixHashFS;
    }
    else if (type == RxShaderType::eVertex) {
        hash = cPrefixHashVS;
    }

    FxLogInfo("Building entry ID with {} macros", macros.Size);

    return FxHashData64(FxSlice<FxShaderMacro>(macros), hash);
}

bool RxShader::PreloadCompiledPrograms(const std::string& pack_path)
{
    bool did_read = mDataPack.ReadFromFile(pack_path.c_str());

    if (!did_read) {
        FxLogInfo("Could not read compiled shader from {}. Recompiling...", pack_path);
        return false;
    }

    mDataPack.ReadAllEntries();

    return true;
}

void RxShader::Load(const char* shader_name)
{
    Name = shader_name;

    std::string program_path = GetProgramPath();

    PreloadCompiledPrograms(program_path.c_str());
}


const std::string RxShader::GetSourcePath() const { return FxAssetPath(AxPathQuery::eShaders) + Name + ".slang"; }
const std::string RxShader::GetProgramPath() const
{
    std::string compiled_folder = std::string(FxAssetPath(AxPathQuery::eShaders)) + "Spirv/";
    std::string compiled_path = (compiled_folder + Name + ".spack");

    return compiled_path;
}


FxRef<RxShaderProgram> RxShader::LoadUncachedProgram(RxShaderType shader_type,
                                                     const FxSizedArray<FxShaderMacro>& macros)
{
    std::string source_path = GetSourcePath();
    const char* c_source_path = source_path.c_str();

    // Check if the shader is out of date
    if (FxShaderCompiler::IsOutOfDate(c_source_path)) {
        FxLogWarning("Shader {} is out of date! ({} macros)", c_source_path, macros.Size);
        // Shader is out of date, compile it
        FxShaderCompiler::Compile(c_source_path, mDataPack, macros);
        // Save the program back to the datapack
        mDataPack.WriteToFile(GetProgramPath().c_str());
    }

    // Generate an ID based on the shader type and macros. This is used for querying for the program in the DataPack.
    FxHash64 program_id = RxShader::GenerateShaderId(shader_type, macros);
    FxLogDebug("Getting program from {} (Id={})", Name, program_id);

    // Create the program
    FxRef<RxShaderProgram> program = FxMakeRef<RxShaderProgram>();
    program->ShaderType = shader_type;
    program->pShader = this;


    // Check for the program in the DataPack
    FxDataPackEntry* dp_entry = mDataPack.QuerySection(program_id);

    // If there is no compiled version of the program in the DataPack, compile it and save it to disk.
    if (!dp_entry) {
        std::string program_path = GetProgramPath();
        RecompileShader(GetSourcePath(), program_path, macros);

        dp_entry = mDataPack.QuerySection(program_id);
        if (dp_entry == nullptr) {
            FxLogError("Pack entry does not exist after compilation! (Id={})", program_id);
            return program;
        }
    }


    // If there is data available, create the program.
    if (dp_entry->Data.IsNotEmpty()) {
        FxLogInfo("Data is available, creating shader...");
        const FxSizedArray<uint8>& program_data = dp_entry->Data;
        FxAssert(program_data.Size == FxMath::AlignValue<4>(program_data.Size));

        CreateShaderModule(program_data.Size, reinterpret_cast<uint32*>(program_data.pData), program->InternalShader);

        return program;
    }

    // The entry exists in the DataPack but the data has not been loaded yet. Load only that section of the DataPack and
    // create the program.
    FxLogInfo("Loading data pack section for shader...");

    uint32 buffer_size = FxMath::AlignValue<4>(dp_entry->DataSize);
    uint32* buffer = FxMemPool::Alloc<uint32>(buffer_size);
    FxSlice<uint8> buffer_slice = FxMakeSlice<uint8>(reinterpret_cast<uint8*>(buffer), buffer_size);

    mDataPack.ReadSection(dp_entry, buffer_slice);

    CreateShaderModule(buffer_size, buffer, program->InternalShader);

    return program;
}


FxRef<RxShaderProgram> RxShader::GetProgram(RxShaderType shader_type, const FxSizedArray<FxShaderMacro>& macros)
{
    ProgramCache& cached_type = mCachedTypes[static_cast<uint32>(shader_type)];

    const FxHash64 macro_hash = FxHashData64(FxMakeSlice(macros.pData, macros.GetSizeInBytes()));
    auto program_it = cached_type.Programs.find(macro_hash);

    if (program_it != cached_type.Programs.end()) {
        return program_it->second;
    }

    // Program has not been cached, load it from the data pack or compile it, and save it back to the cache.
    FxRef<RxShaderProgram> program = LoadUncachedProgram(shader_type, macros);
    cached_type.Programs[macro_hash] = program;

    return program;
}

void RxShader::RecompileShader(const std::string& source_path, const std::string& compiled_path,
                               const FxSizedArray<FxShaderMacro>& macros)
{
    // The previous programs are loaded in already from `PreloadCompiledPrograms()`

    // Compile to the shader pack
    FxShaderCompiler::Compile(source_path.c_str(), mDataPack, macros);

    // Write the programs back to disk
    mDataPack.WriteToFile(compiled_path.c_str());
}


void RxShaderProgram::Destroy()
{
    if (InternalShader == nullptr) {
        return;
    }

    RxGpuDevice* device = gRenderer->GetDevice();
    vkDestroyShaderModule(device->Device, InternalShader, nullptr);
}


void RxShader::CreateShaderModule(uint32 file_size, uint32* shader_data, VkShaderModule& shader_module)
{
    const VkShaderModuleCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = file_size,
        .pCode = shader_data,
    };

    RxGpuDevice* device = gRenderer->GetDevice();

    const VkResult status = vkCreateShaderModule(device->Device, &create_info, nullptr, &shader_module);

    if (status != VK_SUCCESS) {
        FxLogError("Could not create Vulkan shader module: {}", RxUtil::ResultToStr(status));
        return;
    }
}
