#pragma once

#include "RenderPass.hpp"
#include "Shader.hpp"

#include <vulkan/vulkan.h>

#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Slice.hpp>
#include <Renderer/PipelineKey.hpp>
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
	/// The draw does not sample the light probes
	NoProbes = (1 << 4),
	ProbeBounce = (1 << 5),
	NoReflectionProbes = (1 << 6),
	ReflectionCapture = (1 << 7),
	DebugReflection = (1 << 8),
	DebugReflectionCoverage = (1 << 9),
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

	/// Rows the light grid was dispatched with. Paired with `TileColumns` so the shading pass can clamp a pixel's
	/// tile to the grid that was actually culled
	uint32 TileRows = 0;

	float32 EyePosition[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

	/// Scale applied to the lit result before it is written to the HDR target, see GraphicsBackend::PreExposure
	float32 PreExposure = 1.0f;

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

	/// Direction towards the light for directional lights, already normalized. World position otherwise.
	float32 Position[3];
	float32 Radius;

	uint32 Color;
	uint32 Type;
	float32 Intensity;
	float32 InvRadiusSq;

	/// Spot lights only: world space direction the cone points along
	float32 SpotDirection[3];
	/// Spot lights only: cosine of the outer cone half-angle, the light is zero outside of it
	float32 SpotCosOuter;

	float32 LinearColor[3];
	/// Spot lights only: 1 / (cos(inner) - cos(outer)), the falloff rate between the two cone angles
	float32 SpotAngleScale;

	/// Where the light's shadow map is in the shadow atlas: xy scales and zw offsets a shadow map UV into an atlas UV.
	/// All zero when the light has no shadow map.
	float32 ShadowAtlasRect[4];
};


static_assert(sizeof(LightGpuData) == 144, "LightGpuData must match the Light struct in LightingCommon.hlsli");
static_assert(sizeof(LightGpuData) * 64 <= 16384, "The light buffer must fit the guaranteed uniform buffer range");

/// A single decal in GraphicsBackend::DecalBuffer. Mirrors `Decal` in Shaders/DecalCommon.hlsli.
struct alignas(16) DecalGpuData
{
	float32 WorldToDecal[16];

	float32 Center[3];
	/// Tint in RGB, opacity in A. Packed RGBA8 with R in the low byte.
	uint32 Color;

	/// Unit length. The shading pass needs only the directions and would otherwise normalize all three per covered
	/// pixel, so the lengths live in HalfExtents and light culling scales them back up once per tile.
	float32 AxisX[3];
	float32 Roughness;

	float32 AxisY[3];
	/// How much of `Roughness` is blended in, 0 leaves the surface's roughness alone
	float32 RoughnessWeight;

	float32 AxisZ[3];
	float32 NormalStrength;

	float32 AtlasRect[4];

	/// World space half extents along each axis. W picks the atlas, see `DecalAtlas`.
	float32 HalfExtents[4];
};

static_assert(sizeof(DecalGpuData) == 160, "DecalGpuData must match the Decal struct in DecalCommon.hlsli");

struct PipelineProperties
{
	VkCullModeFlags CullMode = VK_CULL_MODE_NONE;
	VkFrontFace WindingOrder = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	VkPolygonMode PolygonMode = VK_POLYGON_MODE_FILL;

	bool bDisableDepthTest : 1 = false;
	bool bDisableDepthWrite : 1 = false;

	bool bRenderLines : 1 = false;

	VkCompareOp DepthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
};

struct PushConstants
{
	uint32 Size;
	eShaderType ShaderTypes;
};

class PipelineLayout
{
public:
	PipelineLayout() = default;

	PipelineLayout(const PipelineLayout& other);
	PipelineLayout(PipelineLayout&& other) noexcept;

	PipelineLayout(const Slice<const PushConstants>& push_constant_defs,
				   const Slice<VkDescriptorSetLayout>& descriptor_set_layouts)
	{
		Create(push_constant_defs, descriptor_set_layouts);
	}

	void Create(const Slice<const PushConstants>& push_constant_defs,
				const Slice<VkDescriptorSetLayout>& descriptor_set_layouts);

	/// Copies of a layout share it, and it is destroyed with the last copy.
	PipelineLayout& operator=(const PipelineLayout& other);
	PipelineLayout& operator=(PipelineLayout&& other) noexcept;

	FX_FORCE_INLINE VkPipelineLayout Get() const
	{
		return (mpRecord != nullptr) ? reinterpret_cast<VkPipelineLayout>(mpRecord->layout) : nullptr;
	}

	FX_FORCE_INLINE bool IsValid() const { return mpRecord != nullptr; }

	~PipelineLayout();

private:
	void Release();

private:
	RxPipelineLayout* mpRecord = nullptr;
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

	Pipeline(const Pipeline&) = delete;
	Pipeline& operator=(const Pipeline&) = delete;

	void Create(ePipelineName name, const Slice<ShaderProgram>& shaders,
				const Slice<VkAttachmentDescription>& attachments,
				const Slice<VkPipelineColorBlendAttachmentState>& color_blend_attachments,
				VertexDescription* vertex_info, const RenderPass& render_pass, const PipelineProperties& properties);

	/**
	 * @brief Creates a compute pipeline from a single compute shader program.
	 */
	void CreateCompute(ePipelineName name, const ShaderProgram& shader);

	FX_FORCE_INLINE void SetLayout(PipelineLayout layout)
	{
		Layout = layout;

		// Layout is referenced from another pipeline or modified externally, do not destroy
		// mbDoNotDestroyLayout = true;
	}

	FX_FORCE_INLINE bool HasLayout() const { return Layout.IsValid(); }

	/// True if this pipeline was created as a compute pipeline
	FX_FORCE_INLINE bool IsCompute() const { return mpRecord != nullptr && mpRecord->is_compute != 0; }

	/// False until the pipeline has been built, and if it could not be
	FX_FORCE_INLINE bool IsBuilt() const { return mpRecord != nullptr; }

	FX_FORCE_INLINE VkPipeline Get() const
	{
		return (mpRecord != nullptr) ? reinterpret_cast<VkPipeline>(mpRecord->pipeline) : nullptr;
	}

	FX_FORCE_INLINE VkPipelineBindPoint GetBindPoint() const
	{
		return (IsCompute()) ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
	}

	/// The cull mode the pipeline was created with. It is dynamic state, so it is set when the pipeline is bound.
	FX_FORCE_INLINE VkCullModeFlags GetDefaultCullMode() const
	{
		return (mpRecord != nullptr) ? static_cast<VkCullModeFlags>(mpRecord->default_cull_mode) : VK_CULL_MODE_NONE;
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
	SizedArray<DescriptorRef> DescriptorIDs;

	ePipelineName Name;

	/// This pipeline's index in the PipelineCache, set by the cache. Lets code that holds a `Pipeline` get the handle
	/// without hashing anything.
	PipelineHandle Handle;

	ShaderProgram VertexShader { nullptr };
	ShaderProgram PixelShader { nullptr };
	ShaderProgram ComputeShader { nullptr };

private:
	GpuDevice* mDevice = nullptr;

	RxPipeline* mpRecord = nullptr;
};

} // namespace fx::renderer
