#include "RxShader.hpp"

#include "FxEngine.hpp"

#include <Asset/FxAssetPaths.hpp>
#include <Asset/FxShaderCompiler.hpp>
#include <Core/FxFile.hpp>
#include <Core/FxTypes.hpp>
#include <Core/Log.hpp>
#include <Core/MemPool/FxMemPool.hpp>
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


void RxShader::Load(const char* shader_name, RxShaderType type, const FxSizedArray<FxShaderMacro>& macros)
{
    Type = type;

    const char* spirv_ext = ShaderTypeToExtension(type);

    std::string path = FxAssetPath(FxAssetPathQuery::eShaders) + std::string(shader_name);

    std::string spirv_folder = std::string(FxAssetPath(FxAssetPathQuery::eShaders)) + "Spirv/";
    std::string spirv_path = (spirv_folder + shader_name + spirv_ext);

    FxFile spirv_file(spirv_path.c_str(), "rb");

    // SPIRV file could not be opened, try to compile it
    if (!spirv_file.IsFileOpen()) {
        std::string slang_path = path + ".slang";

        FxLogInfo("Shader SPIRV not found at {}. Compiling {}...", spirv_path, slang_path);

        FxLogInfo("Compiled folder: {}", spirv_folder);

        std::string output_path = spirv_folder + shader_name + ".spv";

        FxShaderCompiler::Compile(slang_path.c_str(), output_path.c_str(), macros);

        // Try to open SPIRV file again
        spirv_file.Open(spirv_path.c_str(), "rb");

        // Still erroring when trying to open SPIRV, print an error and break out
        if (!spirv_file.IsFileOpen()) {
            FxLogError("Error opening shader SPIRV file!");
            return;
        }
    }

    uint32 buffer_size = FxMath::AlignValue<4>(spirv_file.GetFileSize());
    uint32* buffer = FxMemPool::Alloc<uint32>(buffer_size);

    FxSlice<uint32> file_buffer = spirv_file.Read<uint32>(FxMakeSlice(buffer, buffer_size));

    // Use buffer_size(not file_buffer.Size) as we are using the total buffer size, not the amount
    // of bytes read.)
    CreateShaderModule(buffer_size, file_buffer.Ptr);
    spirv_file.Close();

    FxMemPool::Free(file_buffer.Ptr);
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
