#pragma once

#include <ios>
#include <iostream>

#include <Core/Types.hpp>

#include "ShaderList.hpp"

class RvkShader
{
public:

    RvkShader() = default;

    RvkShader(const char *path, RvkShaderType type)
        : Type(type)
    {
        Load(path, type);
    }

    void Load(const char *path, RvkShaderType type);
    void Destroy();

    ~RvkShader()
    {
        Destroy();
    }

private:
    void CreateShaderModule(std::ios::pos_type file_size, uint32 *shader_data);

public:
    VkShaderModule ShaderModule = nullptr;
    RvkShaderType Type = RvkShaderType::Unknown;
};

// #include <Renderer/FxShader.hpp>

// class FxVulkanShader final : public FxShader
// {
// public:
//     FxVulkanShader() = default;
//     FxVulkanShader(FxShader::Type type, std::string &path)
//     {
//         Load(type, path);
//     }

//     void Load(FxShader::Type, std::string &path) override;
//     void Destroy() override;

//     Type GetType() { return mType; }

//     ~FxVulkanShader()
//     {
//         Destroy();
//     }

// };
