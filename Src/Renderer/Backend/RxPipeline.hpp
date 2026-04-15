#pragma once

#include "RxRenderPass.hpp"
#include "RxShader.hpp"

#include <vulkan/vulkan.h>

#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Slice.hpp>
#include <Renderer/RxVertex.hpp>

namespace fx::renderer {

struct RxVertexDescription;
class RxCommandBuffer;
class RxGpuDevice;

struct alignas(16) DrawPushConstants
{
    float32 CameraMatrix[16];
    uint32 ObjectId = 0;
    uint32 MaterialIndex = 0;
};

struct alignas(16) LightVertPushConstants
{
    float32 CameraMatrix[16];
    uint32 ObjectId = 0;
};

struct alignas(16) CompositionPushConstants
{
    float32 ViewInverse[16];
    float32 ProjInverse[16];
};

struct RxPipelineProperties
{
    VkCullModeFlags CullMode = VK_CULL_MODE_NONE;
    VkFrontFace WindingOrder = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode PolygonMode = VK_POLYGON_MODE_FILL;

    bool bDisableDepthTest : 1 = false;
    bool bDisableDepthWrite : 1 = false;

    Vec2u ViewportSize = Vec2u::sZero;
    VkCompareOp DepthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
};

struct RxPushConstants
{
    uint32 Size;
    VkShaderStageFlags StageFlags;
};


class RxPipeline
{
public:
    void Create(const std::string& name, const Slice<Ref<RxShaderProgram>>& shaders,
                const Slice<VkAttachmentDescription>& attachments,
                const Slice<VkPipelineColorBlendAttachmentState>& color_blend_attachments,
                RxVertexDescription* vertex_info, const RxRenderPass& render_pass,
                const RxPipelineProperties& properties);

    FX_FORCE_INLINE void SetLayout(VkPipelineLayout layout) { Layout = layout; }

    static VkPipelineLayout CreateLayout(const Slice<const RxPushConstants>& push_constant_defs,
                                         const Slice<VkDescriptorSetLayout>& descriptor_set_layouts);

    void Destroy();

    void Bind(const RxCommandBuffer& command_buffer);

    ~RxPipeline() { Destroy(); }

private:
public:
    VkPipelineLayout Layout = nullptr;
    VkPipeline Pipeline = nullptr;

    // XXX: TEMP
    Ref<RxShaderProgram> VertexShader { nullptr };
    Ref<RxShaderProgram> FragmentShader { nullptr };


private:
    RxGpuDevice* mDevice = nullptr;

protected:
    friend class RxPipelineBuilder;

    bool mbDoNotDestroyLayout = false;
};

} // namespace fx::renderer
