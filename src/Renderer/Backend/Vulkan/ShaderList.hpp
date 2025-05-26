#pragma once

#include <Core/FxPanic.hpp>
#include "vulkan/vulkan_core.h"
#include <Core/Types.hpp>
#include <Core/FxStaticArray.hpp>

#include <Core/Log.hpp>
#include <vulkan/vulkan.h>

enum class RvkShaderType
{
    Unknown,
    Vertex,
    Fragment,
};

class ShaderInfo
{
public:
    ShaderInfo() = default;

    ShaderInfo(RvkShaderType type, VkShaderModule module)
        : ShaderModule(module), mShaderType(type)
    {
    }

    VkShaderStageFlagBits GetStageBit()
    {
        switch (mShaderType) {
            case RvkShaderType::Unknown:
                FxPanic_("ShaderList", "Attempting to get shader stage bit of ShaderType::Unknown!", 0);
            case RvkShaderType::Vertex:
                return VK_SHADER_STAGE_VERTEX_BIT;
            case RvkShaderType::Fragment:
                return VK_SHADER_STAGE_FRAGMENT_BIT;
        }
    }

public:
    VkShaderModule ShaderModule = nullptr;

private:
    RvkShaderType mShaderType;
};

class ShaderList
{
public:
    FxStaticArray<ShaderInfo> GetShaderStages()
    {
        FxStaticArray<ShaderInfo> shader_stages(2);

        if (Vertex != nullptr) {
            ShaderInfo info = ShaderInfo(RvkShaderType::Vertex, Vertex);
            shader_stages.Insert(info);
        }
        if (Fragment != nullptr) {
            ShaderInfo info = ShaderInfo(RvkShaderType::Fragment, Fragment);
            shader_stages.Insert(info);
        }

        return shader_stages;
    }

public:
    VkShaderModule Vertex = nullptr;
    VkShaderModule Fragment = nullptr;
};
