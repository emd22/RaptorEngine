#include "Shader.hpp"

#include <fstream>
#include <ios>
#include <iostream>

#include <Renderer/Renderer.hpp>
#include <Core/Types.hpp>

#include "Renderer/Backend/Vulkan/ShaderList.hpp"
#include "Renderer/Renderer.hpp"
#include "Core/Log.hpp"
#include "vulkan/vulkan_core.h"

namespace vulkan {

void Shader::Load(const char *path, ShaderType type)
{
    Type = type;

    // TODO: add to asset loading thread

    std::ifstream file;
    file.open(path, std::iostream::binary);

    if (!file.is_open()) {
        Log::Error("Could not open shader file [%s]", path);
        return;
    }

    file.seekg(0, std::ios::end);
    const auto file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    // as we are aliging to uint32* (4 bytes), we should ensure there
    // is enough memory allocated
    const auto buffer_size =  ((file_size / sizeof(uint32)) + 1) * sizeof(uint32);

    // TODO: use pooled memory!
    char *file_buffer = new char[buffer_size];

    file.read(file_buffer, file_size);
    file.close();

    CreateShaderModule(file_size, (uint32 *)file_buffer);

    delete[] file_buffer;
}

void Shader::Destroy()
{
    if (ShaderModule == nullptr) {
        return;
    }

    vulkan::RvkGpuDevice *device = RendererVulkan->GetDevice();
    vkDestroyShaderModule(device->Device, ShaderModule, nullptr);
}

void Shader::CreateShaderModule(std::ios::pos_type file_size, uint32 *shader_data)
{
    const VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = (uint32)file_size,
        .pCode = (uint32_t *)shader_data,
    };

    vulkan::RvkGpuDevice *device = RendererVulkan->GetDevice();

    const VkResult status = vkCreateShaderModule(device->Device, &create_info, nullptr, &ShaderModule);

    if (status != VK_SUCCESS) {
        Log::Error("Could not create vulkan shader module...");
        return;
    }
}





}; // namespace vulkan
