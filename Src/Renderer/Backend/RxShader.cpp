#include "RxShader.hpp"

#include "FxEngine.hpp"

#include <Asset/FxAssetPaths.hpp>
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

// static FxSlice<const char> GetFilenameWithoutExtension(const char* path)
// {
//     FxSlice<const char> str_slice = FxMakeSlice(path, strlen(path));

//     for (int index = 0; index < str_slice.Size - 1; index++) {
//         // If we encounter the first '.',
//         if (str_slice[index] != '.') {
//             const bool is_relative_path_cmd = (index < str_slice.Size - 2) &&
//                                               (str_slice[index + 1] == '/' || str_slice[index + 1] == '.');

//             if (is_relative_path_cmd) {
//                 continue;
//             }

//             str_slice.Size = index - 1;
//             return str_slice;
//         }
//     }

//     return str_slice;
// }

static const char* ShaderTypeToExtension(RxShaderType type)
{
    switch (type) {
    case RxShaderType::eFragment:
        return ".spv_fs";
    case RxShaderType::eVertex:
        return ".spv_vs";
    case RxShaderType::eUnknown:
    default:;
    }

    FxLogError("No extension for shader type!");

    return "";
}

// static std::string MakeMacrosIdentifier(const FxSizedArray<FxShaderMacro>& macros)
// {
//     std::string identifier;

//     for (const FxShaderMacro& macro : macros) {
//     }
// }

static FxHash64 BuildEntryId(RxShaderType type, const FxSizedArray<FxShaderMacro>& macros)
{
    FxHash64 hash = FX_HASH64_FNV1A_INIT;

    constexpr FxHash64 cPrefixHashFS = FxHashStr64("FS");
    constexpr FxHash64 cPrefixHashVS = FxHashStr64("VS");

    if (type == RxShaderType::eFragment) {
        hash = cPrefixHashFS;
    }
    else if (type == RxShaderType::eVertex) {
        hash = cPrefixHashVS;
    }

    return FxHashData64(FxSlice<FxShaderMacro>(macros), hash);
}


void RxShader::Load(const char* shader_name, RxShaderType type, const FxSizedArray<FxShaderMacro>& macros)
{
    Type = type;

    // const char* spirv_ext = ShaderTypeToExtension(type);
    const char* spirv_ext = ".spack";

    std::string path = FxAssetPath(FxAssetPathQuery::eShaders) + std::string(shader_name);

    std::string spirv_folder = std::string(FxAssetPath(FxAssetPathQuery::eShaders)) + "Spirv/";
    std::string spirv_path = (spirv_folder + shader_name + spirv_ext);

    // FxFile spirv_file(spirv_path.c_str(), FxFile::eRead, FxFile::eText);
    FxDataPack shader_pack;

    FxHash64 entry_id = BuildEntryId(type, macros);

    bool pack_exists = shader_pack.ReadFromFile(spirv_path.c_str());

    // SPIRV file could not be opened, try to compile it
    if (!pack_exists || !shader_pack.QuerySection(entry_id)) {
        std::string slang_path = path + ".slang";

        FxLogInfo("Shader SPIRV not found at {}. Compiling {}...", spirv_path, slang_path);

        FxLogInfo("Compiled folder: {}", spirv_folder);

        // std::string output_path = spirv_folder + shader_name + spirv_ext;

        // If the pack already exists read the existing entries in.
        // if (pack_exists) {
        //     shader_pack.TextReadAllData();
        // }

        // Compile to the shader pack
        FxShaderCompiler::Compile(slang_path.c_str(), shader_pack, macros);

        // Write back to disk
        shader_pack.WriteToFile(spirv_path.c_str());

        shader_pack.File.Flush();

        // Try to open SPIRV file again
        // spirv_file.Open(spirv_path.c_str(), FxFile::eRead, FxFile::eText);

        // Still erroring when trying to open SPIRV, print an error and break out
        // if (!shader_pack.ReadFromFile(output_path.c_str())) {
        //     FxLogError("Error opening shader pack!");
        //     return;
        // }
    }

    shader_pack.ReadFromFile(spirv_path.c_str());

    FxDataPackEntry* entry = shader_pack.QuerySection(entry_id);

    if (!entry) {
        FxLogError("Could not find shader pack entry! (ID={})", entry_id);
        return;
    }

    FxLogInfo("Shader Data({}) @ {}", entry->DataSize, entry->DataOffset);

    uint32 buffer_size = FxMath::AlignValue<4>(entry->DataSize);
    uint32* buffer = FxMemPool::Alloc<uint32>(buffer_size);

    FxSlice<uint8> buffer_slice = FxMakeSlice<uint8>(reinterpret_cast<uint8*>(buffer), buffer_size);

    shader_pack.ReadSection(entry, buffer_slice);

    FxFile out_data("testdata.bin", FxFile::eWrite, FxFile::eBinary);
    out_data.Write(buffer_slice.pData, buffer_slice.Size);

    out_data.Close();

    // FxSlice<uint32> file_buffer = spirv_file.Read<uint32>(FxMakeSlice(buffer, buffer_size));

    // Use buffer_size(not file_buffer.Size) as we are using the total buffer size, not the amount
    // of bytes read.)
    CreateShaderModule(buffer_size, reinterpret_cast<uint32*>(buffer_slice.pData));
    // spirv_file.Close();

    FxMemPool::Free(buffer_slice.pData);
}

void RxShader::Destroy()
{
    if (ShaderModule == nullptr) {
        return;
    }

    RxGpuDevice* device = gRenderer->GetDevice();
    vkDestroyShaderModule(device->Device, ShaderModule, nullptr);
}

void RxShader::CreateShaderModule(uint32 file_size, uint32* shader_data)
{
    const VkShaderModuleCreateInfo create_info {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = file_size,
        .pCode = shader_data,
    };

    RxGpuDevice* device = gRenderer->GetDevice();

    const VkResult status = vkCreateShaderModule(device->Device, &create_info, nullptr, &ShaderModule);

    if (status != VK_SUCCESS) {
        OldLog::Error("Could not create vulkan shader module...");
        return;
    }
}
