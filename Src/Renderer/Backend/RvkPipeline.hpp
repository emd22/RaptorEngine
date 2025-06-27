#pragma once

#include "ShaderList.hpp"
#include "RvkRenderPass.hpp"

#include <Core/FxSizedArray.hpp>

#include <vulkan/vulkan.h>

#include <Math/Mat4.hpp>

class RvkCommandBuffer;

enum FxVertexFlags : int8
{
    FxVertexPosition = 0x01,
    FxVertexNormal = 0x02,
    FxVertexUV = 0x04,
};

template <int8 Flags>
struct RvkVertex;

template <>
struct RvkVertex<FxVertexPosition>
{
    float32 Position[3];
} __attribute__((packed));

template <>
struct RvkVertex<FxVertexPosition | FxVertexNormal>
{
    float32 Position[3];
    float32 Normal[3];
} __attribute__((packed));

template <>
struct RvkVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>
{
    float32 Position[3];
    float32 Normal[3];
    float32 UV[2];
} __attribute__((packed));

struct VertexInfo
{
    VkVertexInputBindingDescription binding;
    FxSizedArray<VkVertexInputAttributeDescription> attributes;
};


struct alignas(16) DrawPushConstants
{
    float32 MVPMatrix[16];
};

class RvkGraphicsPipeline
{
public:
    void Create(ShaderList shader_list);
    void Destroy();

    void Bind(RvkCommandBuffer &command_buffer);

    ~RvkGraphicsPipeline()
    {
        Destroy();
    }

private:
    VertexInfo MakeVertexInfo();
    void CreateLayout();

public:
    VkDescriptorSetLayout MainDescriptorSetLayout = nullptr;
    VkDescriptorSetLayout MaterialDescriptorSetLayout = nullptr;
    // VkDescriptorSetLayout DescriptorSetLayout = nullptr;
    VkPipelineLayout Layout = nullptr;
    VkPipeline Pipeline = nullptr;

    RvkRenderPass RenderPass;
private:
    RvkGpuDevice *mDevice = nullptr;
    ShaderList mShaders;
};
