#include "RxShader.hpp"

#include "FxEngine.hpp"

#include <Asset/AxPaths.hpp>
#include <Asset/FxShaderCompiler.hpp>
#include <Core/FxFile.hpp>
#include <Core/FxTypes.hpp>
#include <Core/Log.hpp>
#include <Core/MemPool/FxMemPool.hpp>
#include <Renderer/RxRenderBackend.hpp>
// #include <fstream>
// #include <ios>
// #include <iostream>

#include <Math/FxMathUtil.hpp>


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

FxRef<RxShaderProgram> RxShader::GetProgram(RxShaderType shader_type, const FxSizedArray<FxShaderMacro>& macros)
{
    std::string source_path = GetSourcePath();
    const char* c_source_path = source_path.c_str();

    if (FxShaderCompiler::IsOutOfDate(c_source_path)) {
        FxLogWarning("Shader {} is out of date! ({} macros)", c_source_path, macros.Size);
        // Shader is out of date, compile it
        FxShaderCompiler::Compile(c_source_path, mDataPack, macros);
        // Save the program back to the datapack
        mDataPack.WriteToFile(GetProgramPath().c_str());
    }

    FxHash64 program_id = RxShader::GenerateShaderId(shader_type, macros);
    FxLogDebug("Getting program from {} (Id={})", Name, program_id);

    FxDataPackEntry* program_entry = mDataPack.QuerySection(program_id);

    FxRef<RxShaderProgram> program = FxMakeRef<RxShaderProgram>();

    // The program is not compiled currently, compile it!
    if (!program_entry) {
        std::string program_path = GetProgramPath();
        RecompileShader(GetSourcePath(), program_path, macros);

        program_entry = mDataPack.QuerySection(program_id);

        // There must be an issue during compilation or a bug in the shader compiler, return the empty program
        if (program_entry == nullptr) {
            FxLogError("Pack entry does not exist after compilation! (Id={})", program_id);
            return program;
        }
    }

    program->ShaderType = shader_type;

    if (program_entry && program_entry->Data.IsNotEmpty()) {
        const FxSizedArray<uint8>& program_data = program_entry->Data;

        FxDebugAssert(program_data.Size == FxMath::AlignValue<4>(program_data.Size));

        CreateShaderModule(program_data.Size, reinterpret_cast<uint32*>(program_data.pData), program->pShader);
    }
    // The program has not been loaded from the datapack yet, load only that section
    else {
        uint32 buffer_size = FxMath::AlignValue<4>(program_entry->DataSize);
        uint32* buffer = FxMemPool::Alloc<uint32>(buffer_size);
        FxSlice<uint8> buffer_slice = FxMakeSlice<uint8>(reinterpret_cast<uint8*>(buffer), buffer_size);

        mDataPack.ReadSection(program_entry, buffer_slice);

        CreateShaderModule(buffer_size, buffer, program->pShader);
    }

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
    if (pShader == nullptr) {
        return;
    }

    RxGpuDevice* device = gRenderer->GetDevice();
    vkDestroyShaderModule(device->Device, pShader, nullptr);
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
        OldLog::Error("Could not create vulkan shader module...");
        return;
    }
}
