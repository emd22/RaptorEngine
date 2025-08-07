#pragma once

#include <ios>
#include <iostream>

#include <Core/Types.hpp>

#include "ShaderList.hpp"

class RxShader
{
public:

    RxShader() = default;

    RxShader(const char *path, RxShaderType type)
        : Type(type)
    {
        Load(path, type);
    }

    void Load(const char *path, RxShaderType type);
    void Destroy();

    ~RxShader()
    {
        Destroy();
    }

    operator VkShaderModule()
    {
        return ShaderModule;
    }

private:
    void CreateShaderModule(std::ios::pos_type file_size, uint32 *shader_data);

public:
    VkShaderModule ShaderModule = nullptr;
    RxShaderType Type = RxShaderType::Unknown;
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
