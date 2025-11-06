#pragma once

#include <vulkan/vulkan.h>

#include <Core/FxPanic.hpp>
#include <Core/FxSizedArray.hpp>
#include <Core/FxTypes.hpp>
#include <Core/Log.hpp>


// class ShaderInfo
// {
// public:
//     ShaderInfo() = default;

//     ShaderInfo(RxShaderType type, VkShaderModule module) : ShaderModule(module), mShaderType(type) {}

//     VkShaderStageFlagBits GetStageBit()
//     {
//         switch (mShaderType) {
//         case RxShaderType::Unknown:
//             FxPanic("ShaderList", "Attempting to get shader stage bit of ShaderType::Unknown!");
//         case RxShaderType::Vertex:
//             return VK_SHADER_STAGE_VERTEX_BIT;
//         case RxShaderType::Fragment:
//             return VK_SHADER_STAGE_FRAGMENT_BIT;
//         }
//     }

// public:
//     VkShaderModule ShaderModule = nullptr;

// private:
//     RxShaderType mShaderType;
// };

// class ShaderList
// {
// public:
//     FxSizedArray<ShaderInfo> GetShaderStages()
//     {
//         FxSizedArray<ShaderInfo> shader_stages(2);

//         if (Vertex != nullptr) {
//             ShaderInfo info = ShaderInfo(RxShaderType::Vertex, Vertex);
//             shader_stages.Insert(info);
//         }
//         if (Fragment != nullptr) {
//             ShaderInfo info = ShaderInfo(RxShaderType::Fragment, Fragment);
//             shader_stages.Insert(info);
//         }

//         return shader_stages;
//     }

// public:
//     VkShaderModule Vertex = nullptr;
//     VkShaderModule Fragment = nullptr;
// };
