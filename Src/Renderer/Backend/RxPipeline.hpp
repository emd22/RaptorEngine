#pragma once

#include "../RxVertex.hpp"
#include "RxRenderPass.hpp"

#include <vulkan/vulkan.h>

#include <Core/FxRef.hpp>
#include <Core/FxSizedArray.hpp>
#include <Core/FxSlice.hpp>
#include <Math/Mat4.hpp>

class RxCommandBuffer;

struct FxVertexInfo
{
    VkVertexInputBindingDescription binding;
    FxSizedArray<VkVertexInputAttributeDescription> attributes;

    bool bIsInited : 1 = false;
};


struct alignas(16) FxDrawPushConstants
{
    float32 MVPMatrix[16];
    float32 NormalMatrix[12];
    uint32 MaterialIndex = 0;
};

struct alignas(16) FxLightVertPushConstants
{
    float32 MVPMatrix[16];
};

struct alignas(16) FxLightFragPushConstants
{
    float32 InvView[16];
    float32 InvProj[16];

    float32 EyePosition[3];
    float32 LightRadius;
    float32 LightPosition[3];
    uint32 LightColor;
};

struct alignas(16) FxCompositionPushConstants
{
    float32 ViewInverse[16];
    float32 ProjInverse[16];
};

FxVertexInfo FxMakeVertexInfo();
FxVertexInfo FxMakeLightVertexInfo();

struct RxGraphicsPipelineProperties
{
    VkCullModeFlags CullMode = VK_CULL_MODE_NONE;
    VkFrontFace WindingOrder = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode PolygonMode = VK_POLYGON_MODE_FILL;

    bool bDisableDepthTest : 1 = false;
    bool bDisableDepthWrite : 1 = false;
};

struct RxPushConstants
{
    uint32 Size;
    VkShaderStageFlags StageFlags;
};

#include "RxShader.hpp"
#include "ShaderList.hpp"

class RxGraphicsPipeline
{
public:
    void Create(const std::string& name, const FxSlice<FxRef<RxShader>>& shaders,
                const FxSlice<VkAttachmentDescription>& attachments,
                const FxSlice<VkPipelineColorBlendAttachmentState>& color_blend_attachments, FxVertexInfo* vertex_info,
                const RxRenderPass& render_pass, const RxGraphicsPipelineProperties& properties);

    FX_FORCE_INLINE void SetLayout(VkPipelineLayout layout) { Layout = layout; }

    // void CreateComp(ShaderList shader_list, VkPipelineLayout layout, const
    // FxSlice<VkPipelineColorBlendAttachmentState>& color_blend_attachments, bool is_comp);

    // VkPipelineLayout CreateCompLayout();

    static VkPipelineLayout CreateLayout(const FxSlice<const RxPushConstants>& push_constant_defs,
                                         const FxSlice<VkDescriptorSetLayout>& descriptor_set_layouts);

    void Destroy();

    void Bind(const RxCommandBuffer& command_buffer);

    ~RxGraphicsPipeline() { Destroy(); }

private:
public:
    // VkDescriptorSetLayout MainDescriptorSetLayout = nullptr;
    // VkDescriptorSetLayout MaterialDescriptorSetLayout = nullptr;

    VkDescriptorSetLayout CompDescriptorSetLayout = nullptr;

    // VkDescriptorSetLayout DescriptorSetLayout = nullptr;
    VkPipelineLayout Layout = nullptr;
    VkPipeline Pipeline = nullptr;

    // RxRenderPass RenderPass;
    //
    // RxRenderPass* RenderPass = nullptr;

private:
    RxGpuDevice* mDevice = nullptr;
    // ShaderList mShaders;
};
