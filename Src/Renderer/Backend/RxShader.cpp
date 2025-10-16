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

#include <Math/FxMathUtil.hpp>

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
    uint32 buffer_size = FxMath::AlignValue<4>(spirv_file.GetFileSize());
    FxLogInfo("buffer size: {}", buffer_size);

    uint32* buffer = FxMemPool::Alloc<uint32>(buffer_size);

    FxSlice<uint32> file_buffer = spirv_file.Read<uint32>(FxMakeSlice(buffer, buffer_size));

    // file.read(file_buffer, file_size);
    // file.close();

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
    const VkShaderModuleCreateInfo create_info = {
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
