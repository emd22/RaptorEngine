#pragma once

#include <Core/FxPanic.hpp>
#include "vulkan/vulkan_core.h"
#include <Core/Types.hpp>
#include <Core/StaticArray.hpp>

#include <Core/Log.hpp>
#include <vulkan/vulkan.h>

enum class ShaderType
{
    Unknown,
    Vertex,
    Fragment,
};

class ShaderInfo
{
public:
    ShaderInfo() = default;

    ShaderInfo(ShaderType type, VkShaderModule module)
        : ShaderModule(module), mShaderType(type)
    {
    }

    VkShaderStageFlagBits GetStageBit()
    {
        switch (mShaderType) {
            case ShaderType::Unknown:
                FxPanic_("ShaderList", "Attempting to get shader stage bit of ShaderType::Unknown!", 0);
            case ShaderType::Vertex:
                return VK_SHADER_STAGE_VERTEX_BIT;
            case ShaderType::Fragment:
                return VK_SHADER_STAGE_FRAGMENT_BIT;
        }
    }

public:
    VkShaderModule ShaderModule = nullptr;

private:
    ShaderType mShaderType;
};

class ShaderList
{
public:
    StaticArray<ShaderInfo> GetShaderStages()
    {
        StaticArray<ShaderInfo> shader_stages(2);

        if (Vertex != nullptr) {
            ShaderInfo info = ShaderInfo(ShaderType::Vertex, Vertex);
            shader_stages.Insert(info);
        }
        if (Fragment != nullptr) {
            ShaderInfo info = ShaderInfo(ShaderType::Fragment, Fragment);
            shader_stages.Insert(info);
        }

        return shader_stages;
    }

public:
    VkShaderModule Vertex = nullptr;
    VkShaderModule Fragment = nullptr;
};
