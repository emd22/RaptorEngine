#include "RxShader.hpp"

#include "FxEngine.hpp"
#include "ShaderList.hpp"

#include <Core/FxFile.hpp>
#include <Core/FxTypes.hpp>
#include <Core/Log.hpp>
#include <Core/MemPool/FxMemPool.hpp>
// #include <fstream>
// #include <ios>
// #include <iostream>

void RxShader::Load(const char* path, RxShaderType type)
{
    Type = type;

    // TODO: add to asset loading thread

    FxFile spirv_file(path, "rb");

    // std::ifstream file;
    // file.open(path, std::iostream::binary);

    if (!spirv_file.IsFileOpen()) {
        FxLogError("Error opening shader SPIRV file!");
        return;
    }

    // if (!file.is_open()) {
    //     OldLog::Error("Could not open shader file [%s]", path);
    //     return;
    // }


    // file.seekg(0, std::ios::end);
    // const auto file_size = file.tellg();
    // file.seekg(0, std::ios::beg);
    char* file_buffer = spirv_file.Read<char>();

    // file.read(file_buffer, file_size);
    // file.close();

    CreateShaderModule(spirv_file.GetFileSize(), reinterpret_cast<uint32*>(file_buffer));
    spirv_file.Close();

    FxMemPool::Free(file_buffer);
}

void RxShader::Destroy()
{
    if (ShaderModule == nullptr) {
        return;
    }

    RxGpuDevice* device = gRenderer->GetDevice();
    vkDestroyShaderModule(device->Device, ShaderModule, nullptr);
}

void RxShader::CreateShaderModule(std::ios::pos_type file_size, uint32* shader_data)
{
    const VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = (uint32)file_size,
        .pCode = (uint32_t*)shader_data,
    };

    RxGpuDevice* device = gRenderer->GetDevice();

    const VkResult status = vkCreateShaderModule(device->Device, &create_info, nullptr, &ShaderModule);

    if (status != VK_SUCCESS) {
        OldLog::Error("Could not create vulkan shader module...");
        return;
    }
}
