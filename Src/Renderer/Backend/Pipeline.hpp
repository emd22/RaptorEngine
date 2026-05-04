#pragma once

#include "RenderPass.hpp"
#include "Shader.hpp"

#include <vulkan/vulkan.h>

#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Slice.hpp>
#include <Renderer/Vertex.hpp>

namespace fx::renderer {

struct VertexDescription;
class CommandBuffer;
class GpuDevice;

struct alignas(16) DrawPushConstants
{
    float32 CameraMatrix[16];
    uint32 ObjectId = 0;
    uint32 MaterialIndex = 0;
};

struct alignas(16) DebugLayerPushConstants
{
    float32 CombinedMatrix[16];
    uint32 DebugColor;
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

struct PipelineProperties
{
    VkCullModeFlags CullMode = VK_CULL_MODE_NONE;
    VkFrontFace WindingOrder = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode PolygonMode = VK_POLYGON_MODE_FILL;

    bool bDisableDepthTest : 1 = false;
    bool bDisableDepthWrite : 1 = false;

    bool bRenderLines : 1 = false;

    Vec2u ViewportSize = Vec2u::sZero;
    VkCompareOp DepthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
};

struct PushConstants
{
    uint32 Size;
    VkShaderStageFlags StageFlags;
};


class Pipeline
{
public:
    Pipeline() = default;

    void Create(const std::string& name, const Slice<Ref<ShaderProgram>>& shaders,
                const Slice<VkAttachmentDescription>& attachments,
                const Slice<VkPipelineColorBlendAttachmentState>& color_blend_attachments,
                VertexDescription* vertex_info, const RenderPass& render_pass, const PipelineProperties& properties);

    FX_FORCE_INLINE void SetLayout(VkPipelineLayout layout)
    {
        Layout = layout;

        // Layout is referenced from another pipeline or modified externally, do not destroy
        mbDoNotDestroyLayout = true;
    }

    static VkPipelineLayout CreateLayout(const Slice<const PushConstants>& push_constant_defs,
                                         const Slice<VkDescriptorSetLayout>& descriptor_set_layouts);

    void Destroy();

    void Bind(const CommandBuffer& command_buffer);

    ~Pipeline() { Destroy(); }

private:
public:
    VkPipelineLayout Layout = nullptr;
    VkPipeline InternalPipeline = nullptr;

    String Name;

    // XXX: TEMP
    Ref<ShaderProgram> VertexShader { nullptr };
    Ref<ShaderProgram> FragmentShader { nullptr };

private:
    GpuDevice* mDevice = nullptr;

protected:
    friend class PipelineBuilder;

    bool mbDoNotDestroyLayout = false;
};

} // namespace fx::renderer
