#pragma once

#include "RenderPass.hpp"
#include "Shader.hpp"

#include <vulkan/vulkan.h>

#include <Core/Ref.hpp>
#include <Core/RefCountedBase.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Slice.hpp>
#include <Renderer/Vertex.hpp>

namespace fx {

enum class eFaceOrder
{
	Default,
	Reverse,
};

enum class eCullMode
{
	None,
	Back,
	Front,
};

FX_FORCE_INLINE constexpr VkFrontFace FaceOrderToVk(eFaceOrder order)
{
	switch (order) {
	case eFaceOrder::Default:
		return VK_FRONT_FACE_COUNTER_CLOCKWISE;
	case fx::eFaceOrder::Reverse:
		return VK_FRONT_FACE_CLOCKWISE;
	default:;
	}

	return VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

FX_FORCE_INLINE constexpr VkCullModeFlags CullModeToVk(eCullMode mode)
{
	switch (mode) {
	case eCullMode::None:
		return VK_CULL_MODE_NONE;
	case eCullMode::Back:
		return VK_CULL_MODE_BACK_BIT;
	case eCullMode::Front:
		return VK_CULL_MODE_FRONT_BIT;
	}

	return VK_CULL_MODE_NONE;
}


} // namespace fx

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
	uint32 LightId = 0;
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
	eShaderType ShaderTypes;
};

/**
 * @brief Marks an internal state to require the next pipeline bound to output its dynamic states.
 */
void RequirePipelineDynamicStates();


class PipelineLayout : public RefCountedBase
{
public:
	PipelineLayout() = default;

	PipelineLayout(const PipelineLayout& other);

	PipelineLayout(const Slice<const PushConstants>& push_constant_defs,
				   const Slice<VkDescriptorSetLayout>& descriptor_set_layouts)
	{
		Create(push_constant_defs, descriptor_set_layouts);
	}

	void Create(const Slice<const PushConstants>& push_constant_defs,
				const Slice<VkDescriptorSetLayout>& descriptor_set_layouts);


	FX_FORCE_INLINE VkPipelineLayout Get() const { return InternalLayout; }
	FX_FORCE_INLINE bool IsValid() const { return InternalLayout != nullptr; }

	PipelineLayout& operator=(const PipelineLayout& other);

	void DestroyObject() override;

	~PipelineLayout();

public:
	VkPipelineLayout InternalLayout = nullptr;

	StackArray<PushConstants, ShaderUtil::scNumShaderTypes> mPushConstDefs;
};


class Pipeline
{
public:
	Pipeline() = default;

	void Create(const std::string& name, const Slice<Ref<ShaderProgram>>& shaders,
				const Slice<VkAttachmentDescription>& attachments,
				const Slice<VkPipelineColorBlendAttachmentState>& color_blend_attachments,
				VertexDescription* vertex_info, const RenderPass& render_pass, const PipelineProperties& properties);


	FX_FORCE_INLINE void SetLayout(PipelineLayout layout)
	{
		Layout = layout;

		// Layout is referenced from another pipeline or modified externally, do not destroy
		// mbDoNotDestroyLayout = true;
	}

	FX_FORCE_INLINE bool HasLayout() const { return Layout.IsValid(); }

	void Bind(const CommandBuffer& command_buffer) const;

	void SetViewport(const Vec2u& size);

	void Destroy();
	~Pipeline() { Destroy(); }

private:
public:
	// VkPipelineLayout Layout = nullptr;
	PipelineLayout Layout;
	VkPipeline InternalPipeline = nullptr;

	bool bBindAttachedDescriptors = false;
	SizedArray<Hash32> DescriptorIDs;

	mutable Vec2u ViewportSize = Vec2u::sZero;

	String Name;

	Ref<ShaderProgram> VertexShader { nullptr };
	Ref<ShaderProgram> PixelShader { nullptr };

	bool bIsViewportFullscreen = false;

	/// True if the pipeline uses dynamic states for viewport and scissor
	bool bHasDynamicViewport = true;

	SizedArray<VkDescriptorSetLayout> DescriptorSetLayouts;

private:
	GpuDevice* mDevice = nullptr;

protected:
	bool mbDoNotDestroyLayout = false;
};

} // namespace fx::renderer
