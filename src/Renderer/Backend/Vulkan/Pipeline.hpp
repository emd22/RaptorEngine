#pragma once

#include "ShaderList.hpp"
#include "RenderPass.hpp"

#include <Core/StaticArray.hpp>

#include <vulkan/vulkan.h>

#include <Math/Mat4.hpp>

class FxCommandBuffer;

namespace vulkan {

enum FxVertexFlags : int8
{
    FxVertexPosition = 0x01,
    FxVertexNormal = 0x02,
};

template <int8 Flags>
struct FxVertex;

template <>
struct FxVertex<FxVertexPosition>
{
    float32 Position[3];
} __attribute__((packed));

template <>
struct FxVertex<FxVertexPosition | FxVertexNormal>
{
    float32 Position[3];
    float32 Normal[3];
} __attribute__((packed));

struct VertexInfo
{
    VkVertexInputBindingDescription binding;
    StaticArray<VkVertexInputAttributeDescription> attributes;
};


struct alignas(16) DrawPushConstants
{
    // float32 MVPMatrix[16];
};

class GraphicsPipeline
{
public:
    void Create(ShaderList shader_list);
    void Destroy();

    void Bind(RvkCommandBuffer &command_buffer);

    ~GraphicsPipeline()
    {
        Destroy();
    }

private:
    VertexInfo MakeVertexInfo();
    void CreateLayout();

public:
    VkDescriptorSetLayout DescriptorSetLayout = nullptr;
    VkPipelineLayout Layout = nullptr;
    VkPipeline Pipeline = nullptr;

    vulkan::RenderPass RenderPass;
private:
    RvkGpuDevice *mDevice = nullptr;
    ShaderList mShaders;
};

}; // namespace vulkan
