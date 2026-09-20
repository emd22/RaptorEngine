#pragma once

#include "RenderPass.hpp"
#include "Shader.hpp"

#include <vulkan/vulkan.h>

#include <Core/Ref.hpp>
#include <Core/RefCountedBase.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Slice.hpp>
#include <Renderer/PipelineNames.hpp>
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

enum class eDrawFlags : uint32
{
	None = 0,

	ProbeCapture = (1 << 0),
	DebugIrradiance = (1 << 1),
	DebugProbeVisibility = (1 << 2),
	/// World decals are not projected onto this draw
	NoDecals = (1 << 3),
};

FxEnumFlags(eDrawFlags);


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
	uint32 TileColumns = 0;

	eDrawFlags Flags = eDrawFlags::None;
	static_assert(sizeof(Flags) == 4);

	uint32 TargetSize[2] = { 0U, 0U };

	/// Index of the draw's first matrix in `GraphicsBackend::BoneBuffer` (skinned pipelines only).
	uint32 BoneBase = 0;
	uint32 _Pad0 = 0;

	float32 EyePosition[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
};

static_assert(sizeof(DrawPushConstants) <= 128, "DrawPushConstants exceeds the minimum guaranteed push constant size");

struct alignas(16) DebugLayerPushConstants
{
	float32 CombinedMatrix[16];
	uint32 DebugColor;
};

struct alignas(16) TextPushConstants
{
	float32 CombinedMatrix[16];
	uint32 TextColor;
	uint32 InstanceBase;
	float32 AtlasMinU;
	float32 AtlasMinV;
	float32 AtlasMaxU;
	float32 AtlasMaxV;
	/// Non-zero draws the whole image in colour instead of glyphs with a background
	uint32 IsImage;
};

struct alignas(16) LightVertPushConstants
{
	float32 CameraMatrix[16];
	uint32 ObjectId = 0;
	uint32 LightId = 0;
};

struct alignas(16) LightCullPushConstants
{
	float32 CameraMatrix[16];
	float32 ScreenSize[2];
	uint32 LightCount = 0;
	uint32 TileColumns = 0;
	/// Light that is swapped out for `ReplacementSlot` in the tile lists, UINT32_MAX for none
	uint32 ReplacedLight = UINT32_MAX;
	uint32 ReplacementSlot = 0;
	/// Number of decals in GraphicsBackend::DecalBuffer for this frame
	uint32 DecalCount = 0;
};

/// A single light slot in GraphicsBackend::LightBuffer. Mirrors `Light` in Shaders/LightingCommon.hlsli.
struct alignas(16) LightGpuData
{
	/// View projection matrix the light's shadow map was rendered with, for lights with a region in the shadow atlas
	float32 LightCameraMatrix[16];
	float32 InvView[16];
	float32 InvProjection[16];

	float32 EyePosition[3];
	float32 Radius;

	/// Direction towards the light for directional lights, world position otherwise
	float32 Position[3];
	uint32 Color;

	float32 CameraSize[2];
	uint32 Ambient;
	uint32 Type;

	/// Spot lights only: world space direction the cone points along
	float32 SpotDirection[3];
	/// Spot lights only: cosine of the outer cone half-angle, the light is zero outside of it
	float32 SpotCosOuter;

	/// Spot lights only: 1 / (cos(inner) - cos(outer)), the falloff rate between the two cone angles
	float32 SpotAngleScale;
	float32 _Pad0[3];

	/// Where the light's shadow map is in the shadow atlas: xy scales and zw offsets a shadow map UV into an atlas UV.
	/// All zero when the light has no shadow map.
	float32 ShadowAtlasRect[4];
};

static_assert(sizeof(LightGpuData) == 288, "LightGpuData must match the Light struct in LightingCommon.hlsli");

/// A single decal in GraphicsBackend::DecalBuffer. Mirrors `Decal` in Shaders/DecalCommon.hlsli.
struct alignas(16) DecalGpuData
{
	float32 WorldToDecal[16];

	float32 Center[3];
	/// Tint in RGB, opacity in A. Packed RGBA8 with R in the low byte.
	uint32 Color;

	float32 HalfAxisX[3];
	float32 Roughness;

	float32 HalfAxisY[3];
	/// How much of `Roughness` is blended in, 0 leaves the surface's roughness alone
	float32 RoughnessWeight;

	float32 HalfAxisZ[3];
	float32 NormalStrength;

	float32 AtlasRect[4];
};

static_assert(sizeof(DecalGpuData) == 144, "DecalGpuData must match the Decal struct in DecalCommon.hlsli");

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

	uint32 ViewportDivisor = 1U;
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
	struct DescriptorRef
	{
		DescriptorRef() = default;
		DescriptorRef(uint32 set_index, DescriptorSet* set, VkDescriptorSetLayout layout)
			: SetIndex(set_index), pSet(set), Layout(layout)
		{
		}

		uint32 SetIndex = 0;
		DescriptorSet* pSet { nullptr };
		VkDescriptorSetLayout Layout { nullptr };
	};

public:
	Pipeline() = default;

	void Create(ePipelineName name, const Slice<Ref<ShaderProgram>>& shaders,
				const Slice<VkAttachmentDescription>& attachments,
				const Slice<VkPipelineColorBlendAttachmentState>& color_blend_attachments,
				VertexDescription* vertex_info, const RenderPass& render_pass, const PipelineProperties& properties);

	/**
	 * @brief Creates a compute pipeline from a single compute shader program.
	 */
	void CreateCompute(ePipelineName name, const Ref<ShaderProgram>& shader);

	FX_FORCE_INLINE void SetLayout(PipelineLayout layout)
	{
		Layout = layout;

		// Layout is referenced from another pipeline or modified externally, do not destroy
		// mbDoNotDestroyLayout = true;
	}

	FX_FORCE_INLINE bool HasLayout() const { return Layout.IsValid(); }

	FX_FORCE_INLINE bool IsCompute() const { return bIsCompute; }

	FX_FORCE_INLINE VkPipelineBindPoint GetBindPoint() const
	{
		return (bIsCompute) ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
	}

	void Bind(const CommandBuffer& command_buffer) const;

	/// Draws after this with no face culling if `double_sided`, or with the pipeline's own culling otherwise.
	void SetDoubleSided(const CommandBuffer& command_buffer, bool double_sided) const;

	void Destroy();
	~Pipeline() { Destroy(); }

private:
public:
	// VkPipelineLayout Layout = nullptr;
	PipelineLayout Layout;
	VkPipeline InternalPipeline = nullptr;

	SizedArray<DescriptorRef> DescriptorIDs;

	mutable Vec2u ViewportSize = Vec2u::sZero;

	ePipelineName Name;

	Ref<ShaderProgram> VertexShader { nullptr };
	Ref<ShaderProgram> PixelShader { nullptr };
	Ref<ShaderProgram> ComputeShader { nullptr };

	/// True if this pipeline was created as a compute pipeline
	bool bIsCompute = false;

	/// The cull mode the pipeline was created with. It is dynamic state, so it is set when the pipeline is bound.
	VkCullModeFlags DefaultCullMode = VK_CULL_MODE_NONE;

	bool bIsViewportFullscreen = false;

	/// True if the pipeline uses dynamic states for viewport and scissor
	bool bHasDynamicViewport = true;

private:
	GpuDevice* mDevice = nullptr;

	uint32 mViewportDivisor = 1U;

protected:
	bool mbDoNotDestroyLayout = false;
};

} // namespace fx::renderer
