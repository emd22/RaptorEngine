#include "ProbeClipmap.hpp"

#include "Backend/BarrierHelper.hpp"
#include "Backend/Sampler/SamplerCache.hpp"
#include "Camera/BaseCamera.hpp"
#include "GraphicsBackend.hpp"
#include "PSOBuild.hpp"
#include "PipelineCache.hpp"
#include "ShadowAtlas.hpp"
#include "Target.hpp"

#include <Core/Log.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/Limits.hpp>
#include <Texture/TextureManager.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace fx::renderer {

namespace {

constexpr uint32 scJobColumns = 1024;
constexpr uint32 scDebugGroupSize = 8;
constexpr uint32 scLogInterval = 120;

constexpr uint32 scJobReset = 0x80000000U;
constexpr uint32 scJobLevelShift = 28;
constexpr uint32 scJobDirtyShift = 24;
/// Share of the budget left after the new probes that the dirty ones may take, so the rest keep being refreshed
constexpr uint32 scDirtyBudgetNumerator = 3;
constexpr uint32 scDirtyBudgetDenominator = 4;
constexpr uint32 scStateValid = 0x01;

/// A slot traced for the first time this frame still holds the probe it had before until the blend, so it counts
/// as unfilled for this frame's reads
constexpr uint8 scPending = 2;
/// A slot whose grid point the map's probe mask rules out. It is never traced and reads as unfilled.
constexpr uint8 scDead = 3;

/// Mirror CLIPMAP_FLAG_* in Shaders/ProbeClipmapConsts.hlsli
constexpr uint32 scFlagBounce = 0x01;
constexpr uint32 scFlagRelocate = 0x02;

enum class eProbeState
{
	Pending,
	Dead,
};


/// Share of the budget left after the new probes that each level gets, finest first
constexpr uint32 scLevelShares[ProbeClipmap::scLevelCount] = { 4, 2, 1 };

/// Mirrors ProbeClipmapConsts in Shaders/ProbeClipmapConsts.hlsli
struct ProbeClipmapConsts
{
	SDFPageSceneGpu SDFPages;
	ProbeClipmapLevelGpu Levels[ProbeClipmap::scLevelCount];
	float32 RayRotation[16] = {};
	BakedSunShadowGpu BakedSun;

	uint32 LightIndices[4] = {};

	/// Light count, shading flags
	uint32 Lights[4] = {};

	/// Job count, frame, flags, proxy count
	uint32 Info[4] = {};

	/// Hysteresis, fast hysteresis, change threshold, moment sharpness
	float32 Blend[4] = {};

	/// Per level, how many times less often its probes are refreshed than the hysteresis is meant for. The fourth is
	/// the dirty hysteresis.
	float32 LevelRate[4] = {};
	float32 SkyRadiance[4] = {};
	ProbeClipmapProxyGpu Proxies[ProbeClipmap::scMaxProxies];
};

static_assert(sizeof(ProbeClipmapConsts) <= FrameAllocator::scMaxAllocationSize);

/// Mirrors ProbeClipmapDebugConsts in Shaders/ProbeClipmapDebug.hlsl
struct ProbeClipmapDebugConsts
{
	ProbeClipmapLevelGpu Levels[ProbeClipmap::scLevelCount];
	float32 InvViewProjection[16] = {};
	float32 EyePosition[4] = {};
	/// Target width and height, view
	uint32 Info[4] = {};
	/// Pre-exposure
	float32 Params[4] = {};
};

/// Mirrors ClipmapForwardData in Shaders/Forward.hlsl
struct ProbeClipmapForwardData
{
	ProbeClipmapLevelGpu Levels[ProbeClipmap::scLevelCount];
	BakedSunShadowGpu BakedSun;
	/// Flags (scForwardFlag*)
	uint32 Info[4] = {};
	/// Irradiance over pi where no probe holds a surface. Black unless the map has no SDF pages to trace.
	float32 Fallback[4] = {};
};

constexpr uint32 scForwardFlagBakedSun = 0x01;

static_assert(sizeof(ProbeClipmapForwardData) <= ProbeClipmap::scForwardDataFrameSize);
static_assert(sizeof(ProbeClipmapLevelGpu) == 32);
static_assert(sizeof(ProbeClipmapProxyGpu) == 64);
static_assert(sizeof(ProbeClipmapConsts) == 192 + 96 + 64 + 80 + 16 * 6 + 64 * ProbeClipmap::scMaxProxies);
static_assert(sizeof(ProbeClipmapDebugConsts) == 96 + 64 + 16 * 3);

/// Mirrors SHADE_FLAG_BAKED_SUN in Shaders/SDFPageShade.hlsli
constexpr uint32 scShadeFlagBakedSun = 0x01;

int32 FloorDiv(float32 value, float32 size) { return static_cast<int32>(std::floor(value / size)); }

int32 PositiveMod(int32 value, int32 size) { return ((value % size) + size) % size; }

/// How many dirty updates a change of `strength` in (0, 1] is worth
uint8 DirtyUpdates(float32 strength)
{
	const float32 full = static_cast<float32>(ProbeClipmap::scDirtyUpdates);

	return static_cast<uint8>(std::clamp(std::ceil(strength * full), 1.0f, full));
}

/// The probe a slot holds while its level's window starts at `origin`, see ClipmapSlotProbe
void SlotProbe(uint32 slot, const int32 origin[3], int32 out_probe[3])
{
	const int32 wrapped[3] = {
		static_cast<int32>(slot % ProbeClipmap::scGridX),
		static_cast<int32>((slot / ProbeClipmap::scGridX) % ProbeClipmap::scGridY),
		static_cast<int32>(slot / (ProbeClipmap::scGridX * ProbeClipmap::scGridY)),
	};

	const int32 grid[3] = { ProbeClipmap::scGridX, ProbeClipmap::scGridY, ProbeClipmap::scGridZ };

	for (uint32 axis = 0; axis < 3; axis++) {
		out_probe[axis] = origin[axis] + PositiveMod(wrapped[axis] - origin[axis], grid[axis]);
	}
}

uint32 Hash(uint32 x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

float32 HashToUnit(uint32 x) { return static_cast<float32>(Hash(x) & 0xFFFFFFU) / static_cast<float32>(0x1000000); }

/// A uniformly random rotation as rows that row vectors are multiplied by (Shoemake)
void RandomRotation(uint32 seed, float32 out_matrix[16])
{
	constexpr float32 scTau = FX_2PI;

	const float32 u1 = HashToUnit(seed * 3U + 0U);
	const float32 u2 = HashToUnit(seed * 3U + 1U);
	const float32 u3 = HashToUnit(seed * 3U + 2U);

	const float32 x = std::sqrt(1.0f - u1) * std::sin(scTau * u2);
	const float32 y = std::sqrt(1.0f - u1) * std::cos(scTau * u2);
	const float32 z = std::sqrt(u1) * std::sin(scTau * u3);
	const float32 w = std::sqrt(u1) * std::cos(scTau * u3);

	const float32 rows[3][3] = {
		{ 1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + w * z), 2.0f * (x * z - w * y) },
		{ 2.0f * (x * y - w * z), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + w * x) },
		{ 2.0f * (x * z + w * y), 2.0f * (y * z - w * x), 1.0f - 2.0f * (x * x + y * y) },
	};

	memset(out_matrix, 0, sizeof(float32) * 16);

	for (uint32 row = 0; row < 3; row++) {
		for (uint32 column = 0; column < 3; column++) {
			out_matrix[row * 4 + column] = rows[row][column];
		}
	}

	out_matrix[15] = 1.0f;
}

Image* CreateTileImage(uint32 tile_size, eImageFormat format)
{
	Image* image = gTextureManager->NewTexture();

	image->Create(
		eImageType::Flat,
		UVec2(ProbeClipmap::scTileColumns * tile_size, static_cast<uint32>(ProbeClipmap::scGridZ) * tile_size), 1,
		format, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		eImageAspectFlag::Color, eImageCreateFlags::IsTarget);

	return image;
}

void ImageBarrier(const CommandBuffer& cmd, Image* image, VkImageLayout from, VkImageLayout to)
{
	const VkImageMemoryBarrier2 barrier {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
		.oldLayout = from,
		.newLayout = to,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image->InternalImage,
		.subresourceRange { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 },
	};

	const VkDependencyInfo dep_info {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &barrier,
	};

	vkCmdPipelineBarrier2(cmd.Cmd, &dep_info);

	image->ImageLayout = to;
}

void ComputeBarrier(const CommandBuffer& cmd)
{
	const VkMemoryBarrier2 barrier {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
	};

	const VkDependencyInfo dep_info {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = 1,
		.pMemoryBarriers = &barrier,
	};

	vkCmdPipelineBarrier2(cmd.Cmd, &dep_info);
}

} // namespace

void ProbeClipmap::Create()
{
	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		Level& level = mLevels[level_index];

		level.Spacing = scLevelSpacings[level_index];
		level.Valid.InitSize(scProbesPerLevel, 0);
		level.Dirty.InitSize(scProbesPerLevel, 0);
		level.JobStamp.InitSize(scProbesPerLevel, 0);

		const uint32 moments_size = (level_index == 0) ? scMomentsSize0 : scMomentsSize;

		mpIrradiance[level_index] = CreateTileImage(scIrradianceSize + 2, eImageFormat::RGBA16_Float);
		mpMoments[level_index] = CreateTileImage(moments_size + 2, eImageFormat::RG16_Float);
	}

	mTrackedLights.InitCapacity(Limits::MaxActiveLights);
	mProxies.InitCapacity(scMaxProxies);
	mTrackedProxies.InitCapacity(scMaxProxies);

	mJobs.Create(eGpuBufferType::Storage, static_cast<uint64>(scMaxJobsPerFrame) * sizeof(uint32) * FramesInFlight,
				 VMA_MEMORY_USAGE_CPU_TO_GPU, eGpuBufferFlags::PersistentMapped);

	mRays.Create(eGpuBufferType::Storage, static_cast<uint64>(scMaxJobsPerFrame) * scRayCount * sizeof(float32) * 4,
				 VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, eGpuBufferFlags::None);

	mState.Create(eGpuBufferType::Storage,
				  static_cast<uint64>(scLevelCount) * scProbesPerLevel * sizeof(uint32) * FramesInFlight,
				  VMA_MEMORY_USAGE_CPU_TO_GPU, eGpuBufferFlags::PersistentMapped);

	mProbeData.Create(eGpuBufferType::Storage, scProbeDataSize, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
					  eGpuBufferFlags::TransferReceiver);

	mForwardData.Create(eGpuBufferType::Storage, scForwardDataFrameSize * FramesInFlight, VMA_MEMORY_USAGE_CPU_TO_GPU,
						eGpuBufferFlags::PersistentMapped);

	Reset();
}

void ProbeClipmap::Destroy()
{
	mJobs.Destroy();
	mRays.Destroy();
	mState.Destroy();
	mProbeData.Destroy();
	mForwardData.Destroy();
}

void ProbeClipmap::CreatePipelines(Target* depth, Target* normals, Target* lit)
{
	SDFPageScene& pages = gGraphics->SDFPages;

	Sampler* linear_sampler = gSamplerCache->Request(SamplerProps {
		eSamplerFilter::Linear,
		eSamplerFilter::Linear,
		eSamplerFilter::Nearest,
		eSamplerAddressMode::ClampToEdge,
	});

	Sampler* point_sampler = gSamplerCache->Request(SamplerProps {
		eSamplerFilter::Nearest,
		eSamplerFilter::Nearest,
		eSamplerFilter::Nearest,
		eSamplerAddressMode::ClampToEdge,
	});

	gPSOBuild->BeginPipeline(ePipelineName::ProbeClipmapTrace);
	gPSOBuild->SetShader(eShaderName::ProbeClipmapTrace, {});
	gPSOBuild->AddBuffer(0, 0, eShaderType::Compute, pages.GetIndirectionBuffer(), 0, VK_WHOLE_SIZE);
	gPSOBuild->AddImage(1, 0, eShaderType::Compute, pages.GetAtlas(), linear_sampler);
	gPSOBuild->AddBuffer(2, 0, eShaderType::Compute, pages.GetTriangleBuffer(), 0, VK_WHOLE_SIZE);
	gPSOBuild->AddBuffer(3, 0, eShaderType::Compute, pages.GetRefBuffer(), 0, VK_WHOLE_SIZE);
	gPSOBuild->AddBuffer(4, 0, eShaderType::Compute, pages.GetAtlasFirstRefBuffer(), 0, VK_WHOLE_SIZE);
	gPSOBuild->AddImage(5, 0, eShaderType::Compute, pages.GetNearest(), point_sampler);
	gPSOBuild->AddBuffer(6, 0, eShaderType::Compute, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
						 gGraphics->LightBuffer.PageSize);

	gPSOBuild->AddImageFromTarget(7, 0, eShaderType::Compute, gShadowAtlas->GetTarget(),
								  gSamplerCache->Request({
									  eSamplerFilter::Linear,
									  eSamplerFilter::Linear,
									  eSamplerFilter::Linear,
									  eSamplerAddressMode::ClampToBorder,
									  eSamplerBorderColor::FloatWhite,
									  eSamplerCompareOp::Greater,
								  }));
	gPSOBuild->AddImage(8, 0, eShaderType::Compute, gGraphics->pFogShadowAtlas,
						gSamplerCache->Request({
							eSamplerFilter::Linear,
							eSamplerFilter::Linear,
							eSamplerFilter::Nearest,
							eSamplerAddressMode::ClampToEdge,
							eSamplerBorderColor::FloatWhite,
							eSamplerCompareOp::LessOrEqual,
						}));
	gPSOBuild->AddBuffer(9, 0, eShaderType::Compute, &gGraphics->FogShadowPageBuffer, 0,
						 gGraphics->FogShadowPageBufferSize);
	gPSOBuild->AddBuffer(10, 0, eShaderType::Compute, &mJobs, 0,
						 static_cast<uint64>(scMaxJobsPerFrame) * sizeof(uint32));
	gPSOBuild->AddBuffer(11, 0, eShaderType::Compute, &mRays, 0, VK_WHOLE_SIZE);

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		gPSOBuild->AddImage(12 + level_index, 0, eShaderType::Compute, mpIrradiance[level_index], linear_sampler);
		gPSOBuild->AddImage(15 + level_index, 0, eShaderType::Compute, mpMoments[level_index], linear_sampler);
	}

	gPSOBuild->AddBuffer(18, 0, eShaderType::Compute, &mState, 0,
						 static_cast<uint64>(scLevelCount) * scProbesPerLevel * sizeof(uint32));
	gPSOBuild->AddBuffer(19, 0, eShaderType::Compute, &mProbeData, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddFrameData(1);
	gPSOBuild->EndPipeline();

	gPSOBuild->BeginPipeline(ePipelineName::ProbeClipmapBlend);
	gPSOBuild->SetShader(eShaderName::ProbeClipmapBlend, {});
	gPSOBuild->AddBuffer(0, 0, eShaderType::Compute, &mJobs, 0,
						 static_cast<uint64>(scMaxJobsPerFrame) * sizeof(uint32));
	gPSOBuild->AddBuffer(1, 0, eShaderType::Compute, &mRays, 0, VK_WHOLE_SIZE);

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		gPSOBuild->AddStorageImage(2 + level_index, 0, eShaderType::Compute, mpIrradiance[level_index]);
		gPSOBuild->AddStorageImage(5 + level_index, 0, eShaderType::Compute, mpMoments[level_index]);
	}

	gPSOBuild->AddBuffer(9, 0, eShaderType::Compute, &mProbeData, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddFrameData(1);
	gPSOBuild->EndPipeline();

	gPSOBuild->BeginPipeline(ePipelineName::ProbeClipmapDebug);
	gPSOBuild->SetShader(eShaderName::ProbeClipmapDebug, {});
	gPSOBuild->AddImageFromTarget(0, 0, eShaderType::Compute, depth, point_sampler);
	gPSOBuild->AddImageFromTarget(1, 0, eShaderType::Compute, normals, point_sampler);

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		gPSOBuild->AddImage(2 + level_index, 0, eShaderType::Compute, mpIrradiance[level_index], linear_sampler);
		gPSOBuild->AddImage(5 + level_index, 0, eShaderType::Compute, mpMoments[level_index], linear_sampler);
	}

	gPSOBuild->AddBuffer(8, 0, eShaderType::Compute, &mState, 0,
						 static_cast<uint64>(scLevelCount) * scProbesPerLevel * sizeof(uint32));
	gPSOBuild->AddStorageImage(9, 0, eShaderType::Compute, &lit->GetImage());
	gPSOBuild->AddBuffer(10, 0, eShaderType::Compute, &mProbeData, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddFrameData(1);
	gPSOBuild->EndPipeline();
}

void ProbeClipmap::Reset()
{
	for (Level& level : mLevels) {
		level.bPlaced = false;
		level.Cursor = 0;
		level.InvalidCount = scProbesPerLevel;
		level.DirtyCount = 0;
		level.DirtyCursor = 0;

		for (uint32 slot = 0; slot < scProbesPerLevel; slot++) {
			level.Valid[slot] = 0;
			level.Dirty[slot] = 0;
		}
	}

	mTrackedLights.Clear();
	mbHasTrackedLights = false;
	mTrackedProxies.Clear();
	mbHasTrackedProxies = false;
	mbClearProbeData = true;
}

void ProbeClipmap::SetProbeMask(const CompiledMapProbeMask& mask)
{
	mMask = CompiledMapProbeMask {};

	for (int32& mask_level : mMaskLevels) {
		mask_level = -1;
	}

	if (mask.IsEmpty()) {
		return;
	}

	// Each level uses the mask level built for its spacing, so a map compiled for other spacings keeps the levels
	// that still match
	uint32 matched_count = 0;

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		for (uint32 mask_index = 0; mask_index < mask.Levels.Size; mask_index++) {
			if (std::abs(mask.Levels[mask_index].Spacing - mLevels[level_index].Spacing) <= 1e-4f) {
				mMaskLevels[level_index] = static_cast<int32>(mask_index);
				matched_count++;
				break;
			}
		}
	}

	if (matched_count < scLevelCount) {
		LogWarning(LC_RENDER, "The map's GI probe mask has {} of {} probe spacings, recompile the map", matched_count,
				   scLevelCount);
	}

	if (matched_count == 0) {
		return;
	}

	mMask.bOutsideDead = mask.bOutsideDead;
	mMask.Levels.InitSize(mask.Levels.Size);
	mMask.Words.InitSize(mask.Words.Size);

	memcpy(mMask.Levels.pData, mask.Levels.pData, mask.Levels.Size * sizeof(CompiledMapProbeMaskLevel));
	memcpy(mMask.Words.pData, mask.Words.pData, mask.Words.Size * sizeof(uint32));
}

void ProbeClipmap::ClassifyLevel(uint32 level_index)
{
	const int32 mask_level = mMaskLevels[level_index];

	if (!mbMaskInUse || mMask.IsEmpty() || mask_level < 0) {
		return;
	}

	Level& level = mLevels[level_index];

	for (uint32 slot = 0; slot < scProbesPerLevel; slot++) {
		if (level.Valid[slot] != 0) {
			continue;
		}

		int32 probe[3];
		SlotProbe(slot, level.Origin, probe);

		if (!mMask.IsAlive(static_cast<uint32>(mask_level), probe)) {
			level.Valid[slot] = scDead;
			level.InvalidCount--;
		}
	}
}

void ProbeClipmap::SetDirty(Level& level, uint32 slot, uint8 value)
{
	if (level.Dirty[slot] == 0 && value != 0) {
		level.DirtyCount++;
	}
	else if (level.Dirty[slot] != 0 && value == 0) {
		level.DirtyCount--;
	}

	level.Dirty[slot] = value;
}

void ProbeClipmap::MarkDirty(const FVec3& center, float32 radius, float32 strength)
{
	const uint8 updates = DirtyUpdates(strength);

	const int32 grid[3] = { scGridX, scGridY, scGridZ };
	const float32 position[3] = { center.X, center.Y, center.Z };

	for (Level& level : mLevels) {
		if (!level.bPlaced) {
			continue;
		}

		// A probe just outside the light still sees what it lights
		const float32 reach = radius + level.Spacing;

		int32 first[3];
		int32 last[3];
		bool is_empty = false;

		for (uint32 axis = 0; axis < 3; axis++) {
			first[axis] = std::max(FloorDiv(position[axis] - reach, level.Spacing), level.Origin[axis]);
			last[axis] = std::min(FloorDiv(position[axis] + reach, level.Spacing) + 1,
								  level.Origin[axis] + grid[axis] - 1);
			is_empty = is_empty || (first[axis] > last[axis]);
		}

		if (is_empty) {
			continue;
		}

		for (int32 z = first[2]; z <= last[2]; z++) {
			for (int32 y = first[1]; y <= last[1]; y++) {
				for (int32 x = first[0]; x <= last[0]; x++) {
					const FVec3 probe(static_cast<float32>(x) * level.Spacing, static_cast<float32>(y) * level.Spacing,
									  static_cast<float32>(z) * level.Spacing);

					if (probe.DistanceTo(center) > reach) {
						continue;
					}

					const uint32 slot = static_cast<uint32>(PositiveMod(x, scGridX) +
															PositiveMod(y, scGridY) * scGridX +
															PositiveMod(z, scGridZ) * scGridX * scGridY);

					// Unfilled probes are traced from nothing anyway
					if (level.Valid[slot] != 0 && level.Valid[slot] != scDead && level.Dirty[slot] < updates) {
						SetDirty(level, slot, updates);
					}
				}
			}
		}
	}
}

void ProbeClipmap::MarkAllDirty(float32 strength)
{
	const uint8 updates = DirtyUpdates(strength);

	for (Level& level : mLevels) {
		for (uint32 slot = 0; slot < scProbesPerLevel; slot++) {
			if (level.Valid[slot] != 0 && level.Valid[slot] != scDead && level.Dirty[slot] < updates) {
				SetDirty(level, slot, updates);
			}
		}
	}
}

void ProbeClipmap::MarkLightDirty(const TrackedLight& light, float32 strength)
{
	if (light.bDirectional) {
		MarkAllDirty(strength);
	}
	else {
		MarkDirty(light.Position, light.Radius, strength);
	}
}

void ProbeClipmap::TrackLights(const SizedArray<ShadeLightCandidate>& lights)
{
	// A light that moved this much of its radius, turned this far (radians), or changed its intensity by all of it
	// counts as fully changed
	constexpr float32 scFullMove = 0.1f;
	constexpr float32 scFullTurn = 0.35f;
	/// Less than this is rounding
	constexpr float32 scMinChange = 1e-3f;

	// The first lights after a reset light probes that have not been traced yet, so there is nothing to compare
	if (mbHasTrackedLights) {
		for (const ShadeLightCandidate& light : lights) {
			const TrackedLight* previous = nullptr;

			for (const TrackedLight& tracked : mTrackedLights) {
				if (tracked.ID == light.ID) {
					previous = &tracked;
					break;
				}
			}

			TrackedLight current;
			current.bDirectional = light.bDirectional;
			current.Position = light.Position;
			current.Radius = light.Radius;

			if (previous == nullptr) {
				MarkLightDirty(current, 1.0f);
				continue;
			}

			float32 change = 0.0f;

			// Small angles are taken from the distance between the unit vectors. An arc cosine of their dot product
			// reads rounding error as a turn of a few ten thousandths, every frame.
			if (light.bDirectional) {
				// Its position is the direction towards it
				change += (light.Position.Normalize() - previous->Position.Normalize()).Length() / scFullTurn;
			}
			else {
				change += light.Position.DistanceTo(previous->Position) / std::max(light.Radius * scFullMove, 1e-3f);
				change += std::abs(light.Radius - previous->Radius) /
						  std::max(std::max(light.Radius, previous->Radius), 1e-3f);

				// A quaternion and its negative are the same rotation
				float32 same_sq = 0.0f;
				float32 negated_sq = 0.0f;

				for (uint32 lane = 0; lane < 4; lane++) {
					const float32 difference = light.Rotation[lane] - previous->Rotation[lane];
					const float32 sum = light.Rotation[lane] + previous->Rotation[lane];

					same_sq += difference * difference;
					negated_sq += sum * sum;
				}

				change += 2.0f * std::sqrt(std::min(same_sq, negated_sq)) / scFullTurn;
			}

			change += std::abs(light.Intensity - previous->Intensity) /
					  std::max(std::max(light.Intensity, previous->Intensity), 1e-6f);

			if (light.Color != previous->Color) {
				change += 1.0f;
			}

			if (change > scMinChange) {
				// Where it was as well as where it is now
				MarkLightDirty(*previous, std::min(change, 1.0f));
				MarkLightDirty(current, std::min(change, 1.0f));
			}
		}

		// Lights that are gone
		for (const TrackedLight& tracked : mTrackedLights) {
			bool is_present = false;

			for (const ShadeLightCandidate& light : lights) {
				is_present = is_present || (light.ID == tracked.ID);
			}

			if (!is_present) {
				MarkLightDirty(tracked, 1.0f);
			}
		}
	}

	mTrackedLights.Clear();

	for (const ShadeLightCandidate& light : lights) {
		if (mTrackedLights.Size >= mTrackedLights.Capacity) {
			break;
		}

		TrackedLight tracked;
		tracked.ID = light.ID;
		tracked.bDirectional = light.bDirectional;
		tracked.Position = light.Position;
		tracked.Radius = light.Radius;
		memcpy(tracked.Rotation, light.Rotation, sizeof(tracked.Rotation));
		tracked.Intensity = light.Intensity;
		tracked.Color = light.Color;

		mTrackedLights.Insert(std::move(tracked));
	}

	mbHasTrackedLights = true;
}

void ProbeClipmap::SetDynamicProxies(const SizedArray<DynamicGIProxy>& proxies, bool track)
{
	// A box that moved this much of its radius, or turned this far (radians), counts as fully changed
	constexpr float32 scFullMove = 0.25f;
	constexpr float32 scFullTurn = 0.35f;
	/// Less than this is rounding
	constexpr float32 scMinChange = 1e-3f;
	/// Probes this many of its radii away still see enough of a box to be lit differently
	constexpr float32 scReach = 2.0f;

	mProxies.Clear();

	for (const DynamicGIProxy& proxy : proxies) {
		if (mProxies.Size >= mProxies.Capacity) {
			break;
		}

		mProxies.Insert(proxy);
	}

	if (!track) {
		mTrackedProxies.Clear();
		mbHasTrackedProxies = false;
		return;
	}

	const auto get_reach = [&](const DynamicGIProxy& proxy) { return proxy.HalfExtents.Length() * scReach; };

	// The first boxes after a reset are seen by probes that have not been traced yet, so there is nothing to compare
	if (mbHasTrackedProxies) {
		for (const DynamicGIProxy& proxy : mProxies) {
			const DynamicGIProxy* previous = nullptr;

			for (const DynamicGIProxy& tracked : mTrackedProxies) {
				if (tracked.ID == proxy.ID) {
					previous = &tracked;
					break;
				}
			}

			if (previous == nullptr) {
				MarkDirty(proxy.Center, get_reach(proxy), 1.0f);
				continue;
			}

			const float32 radius = std::max(proxy.HalfExtents.Length(), 1e-3f);
			const float32 move = proxy.Center.DistanceTo(previous->Center);

			float32 change = move / (radius * scFullMove);

			// Small angles are taken from the distance between the unit axes, as an arc cosine reads rounding as a turn
			for (uint32 axis = 0; axis < 3; axis++) {
				change += (proxy.Axes[axis] - previous->Axes[axis]).Length() / scFullTurn;
			}

			change += (proxy.HalfExtents - previous->HalfExtents).Length() / radius;

			if (change > scMinChange) {
				// The reach around where it is now, widened by how far it moved, holds where it was as well
				MarkDirty(proxy.Center, get_reach(proxy) + move, std::min(change, 1.0f));
			}
		}

		// Boxes that are gone
		for (const DynamicGIProxy& tracked : mTrackedProxies) {
			bool is_present = false;

			for (const DynamicGIProxy& proxy : mProxies) {
				is_present = is_present || (proxy.ID == tracked.ID);
			}

			if (!is_present) {
				MarkDirty(tracked.Center, get_reach(tracked), 1.0f);
			}
		}
	}

	mTrackedProxies.Clear();

	for (const DynamicGIProxy& proxy : mProxies) {
		mTrackedProxies.Insert(proxy);
	}

	mbHasTrackedProxies = true;
}

uint32 ProbeClipmap::ScheduleDirtyJobs(uint32 level_index, uint32* jobs, uint32 job_count, uint32 limit)
{
	Level& level = mLevels[level_index];

	for (uint32 index = 0; index < scProbesPerLevel && level.DirtyCount > 0 && job_count < limit; index++) {
		const uint32 slot = level.DirtyCursor;
		level.DirtyCursor = (level.DirtyCursor + 1) % scProbesPerLevel;

		const uint8 dirty = level.Dirty[slot];

		if (dirty == 0 || level.JobStamp[slot] == mUpdateCount) {
			continue;
		}

		if (level.Valid[slot] != 1) {
			SetDirty(level, slot, 0);
			continue;
		}

		jobs[job_count++] = slot | (level_index << scJobLevelShift) | (static_cast<uint32>(dirty) << scJobDirtyShift);
		level.JobStamp[slot] = mUpdateCount;
		SetDirty(level, slot, dirty - 1);
	}

	return job_count;
}

void ProbeClipmap::PlaceLevel(uint32 level_index, const FVec3& center)
{
	Level& level = mLevels[level_index];

	const int32 grid[3] = { scGridX, scGridY, scGridZ };
	const float32 position[3] = { center.X, center.Y, center.Z };

	int32 origin[3];

	for (uint32 axis = 0; axis < 3; axis++) {
		origin[axis] = FloorDiv(position[axis], level.Spacing) - grid[axis] / 2;
	}

	if (level.bPlaced && origin[0] == level.Origin[0] && origin[1] == level.Origin[1] && origin[2] == level.Origin[2]) {
		return;
	}

	if (level.bPlaced) {
		for (uint32 slot = 0; slot < scProbesPerLevel; slot++) {
			int32 old_probe[3];
			int32 new_probe[3];

			SlotProbe(slot, level.Origin, old_probe);
			SlotProbe(slot, origin, new_probe);

			const bool same = old_probe[0] == new_probe[0] && old_probe[1] == new_probe[1] &&
							  old_probe[2] == new_probe[2];

			if (!same && level.Valid[slot] != 0) {
				level.Valid[slot] = 0;
				level.InvalidCount++;
				SetDirty(level, slot, 0);
			}
		}
	}

	level.Origin[0] = origin[0];
	level.Origin[1] = origin[1];
	level.Origin[2] = origin[2];
	level.bPlaced = true;

	ClassifyLevel(level_index);
}

uint32 ProbeClipmap::ScheduleJobs(uint32* jobs, const Settings& settings)
{
	const uint32 budget = std::min(settings.Budget, scMaxJobsPerFrame);
	uint32 job_count = 0;

	// Probes that came into view (or were never traced) first, finest level first
	for (uint32 level_index = 0; level_index < scLevelCount && job_count < budget; level_index++) {
		Level& level = mLevels[level_index];

		for (uint32 slot = 0; slot < scProbesPerLevel && level.InvalidCount > 0 && job_count < budget; slot++) {
			if (level.Valid[slot] != 0) {
				continue;
			}

			jobs[job_count++] = slot | (level_index << scJobLevelShift) | scJobReset;
			level.Valid[slot] = scPending;
			level.InvalidCount--;
			level.JobStamp[slot] = mUpdateCount;
		}
	}

	uint32 share_total = 0;

	for (const uint32 share : scLevelShares) {
		share_total += share;
	}

	// Then the probes whose light changed: each level up to its share, and what that leaves to whichever still has
	// some, finest first
	const uint32 first_dirty = job_count;
	const uint32 dirty_budget = (budget - job_count) * scDirtyBudgetNumerator / scDirtyBudgetDenominator;
	const uint32 dirty_limit = job_count + dirty_budget;

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		const uint32 quota = dirty_budget * scLevelShares[level_index] / share_total;
		job_count = ScheduleDirtyJobs(level_index, jobs, job_count, std::min(job_count + quota, dirty_limit));
	}

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		job_count = ScheduleDirtyJobs(level_index, jobs, job_count, dirty_limit);
	}

	mLastDirtyJobCount = job_count - first_dirty;

	// The rest of the budget refreshes the oldest probes, round robin, shared between the levels
	const uint32 remaining = budget - job_count;

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		Level& level = mLevels[level_index];
		const uint32 quota = std::min(remaining * scLevelShares[level_index] / share_total, scProbesPerLevel);

		// One lap at most, past the slots that hold nothing to refresh, so a level with few live probes still gets
		// its share of jobs
		uint32 scheduled = 0;

		for (uint32 index = 0; index < scProbesPerLevel && scheduled < quota && job_count < budget; index++) {
			const uint32 slot = level.Cursor;
			level.Cursor = (level.Cursor + 1) % scProbesPerLevel;

			if (level.Valid[slot] != 1 || level.JobStamp[slot] == mUpdateCount) {
				continue;
			}

			jobs[job_count++] = slot | (level_index << scJobLevelShift);
			level.JobStamp[slot] = mUpdateCount;
			scheduled++;
		}
	}

	return job_count;
}

void ProbeClipmap::GetLevelConsts(ProbeClipmapLevelGpu* out_levels, const Settings& settings) const
{
	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		const Level& level = mLevels[level_index];
		ProbeClipmapLevelGpu& gpu = out_levels[level_index];

		gpu.Origin[0] = level.Origin[0];
		gpu.Origin[1] = level.Origin[1];
		gpu.Origin[2] = level.Origin[2];

		gpu.Params[0] = level.Spacing;
		gpu.Params[1] = 1.0f / level.Spacing;
		gpu.Params[2] = settings.RayLength;
		gpu.Params[3] = settings.ConeSlope;
	}
}


void ProbeClipmap::Update(CommandBuffer& cmd, const FVec3& center, const Settings& settings)
{
	SDFPageScene& pages = gGraphics->SDFPages;

	const uint32 frame = gGraphics->GetFrameNumber();

	if (!mbImagesInitialized) {
		InitializeImages(cmd);
	}

	// A map that was not compiled has nothing to trace. Its surfaces are lit by the sky alone.
	if (!pages.IsTraceable()) {
		const uint32 state_size = scLevelCount * scProbesPerLevel * sizeof(uint32);

		memset(static_cast<uint8*>(mState.pMappedBuffer) + frame * state_size, 0, state_size);
		mState.FlushToGpu(frame * state_size, state_size);

		WriteForwardData(frame, settings, settings.SkyRadiance);
		return;
	}


	mUpdateCount++;

	if (settings.bUseMask != mbMaskInUse) {
		mbMaskInUse = settings.bUseMask;
		Reset();
	}

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		PlaceLevel(level_index, center);
	}

	uint32* jobs = reinterpret_cast<uint32*>(mJobs.pMappedBuffer) + static_cast<uint64>(frame) * scMaxJobsPerFrame;
	const uint32 job_count = ScheduleJobs(jobs, settings);
	mLastJobCount = job_count;

	uint32 live_count[scLevelCount] = {};

	uint32* state = reinterpret_cast<uint32*>(mState.pMappedBuffer) +
					static_cast<uint64>(frame) * scLevelCount * scProbesPerLevel;

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		Level& level = mLevels[level_index];
		uint32* level_state = state + static_cast<uint64>(level_index) * scProbesPerLevel;

		for (uint32 slot = 0; slot < scProbesPerLevel; slot++) {
			level_state[slot] = (level.Valid[slot] == 1) ? scStateValid : 0U;
			live_count[level_index] += (level.Valid[slot] != 0 && level.Valid[slot] != scDead) ? 1U : 0U;

			// A probe's first trace is all of its history, so it blends its next updates in fast, as if its light
			// had changed
			if (level.Valid[slot] == scPending) {
				level.Valid[slot] = 1;
				SetDirty(level, slot, scDirtyUpdates);
			}
		}
	}

	const uint32 state_size = scLevelCount * scProbesPerLevel * sizeof(uint32);
	mState.FlushToGpu(frame * state_size, state_size);

	WriteForwardData(frame, settings, 0.0f);

	if (job_count == 0) {
		return;
	}

	mJobs.FlushToGpu(frame * scMaxJobsPerFrame * sizeof(uint32), job_count * sizeof(uint32));

	ProbeClipmapConsts consts;
	pages.GetSceneConsts(consts.SDFPages);
	GetLevelConsts(consts.Levels, settings);
	RandomRotation(gGraphics->GetElapsedFrameCount(), consts.RayRotation);

	gGraphics->SelectShadeLights(center, settings.RayLength, consts.LightIndices, consts.Lights[0]);

	if (gGraphics->IsBakedSunShadowActive()) {
		consts.Lights[1] |= scShadeFlagBakedSun;
		consts.BakedSun = gGraphics->GetBakedSunShadowGpu();
	}

	consts.Info[0] = job_count;
	consts.Info[1] = gGraphics->GetElapsedFrameCount();
	consts.Info[3] = mProxies.Size;

	for (uint32 index = 0; index < mProxies.Size; index++) {
		const DynamicGIProxy& proxy = mProxies[index];
		ProbeClipmapProxyGpu& gpu = consts.Proxies[index];

		for (uint32 axis = 0; axis < 3; axis++) {
			const FVec3& direction = proxy.Axes[axis];

			gpu.Axes[axis][0] = direction.X;
			gpu.Axes[axis][1] = direction.Y;
			gpu.Axes[axis][2] = direction.Z;
			gpu.Axes[axis][3] = -direction.Dot(proxy.Center);
		}

		const auto to_byte = [](float32 value)
		{ return static_cast<uint32>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };

		const uint32 albedo = to_byte(proxy.Albedo.X) | (to_byte(proxy.Albedo.Y) << 8) |
							  (to_byte(proxy.Albedo.Z) << 16);

		gpu.Extents[0] = proxy.HalfExtents.X;
		gpu.Extents[1] = proxy.HalfExtents.Y;
		gpu.Extents[2] = proxy.HalfExtents.Z;
		memcpy(&gpu.Extents[3], &albedo, sizeof(albedo));
	}

	if (settings.bBounce) {
		consts.Info[2] |= scFlagBounce;
	}

	if (settings.bRelocate) {
		consts.Info[2] |= scFlagRelocate;
	}

	consts.SkyRadiance[0] = settings.SkyRadiance;
	consts.SkyRadiance[1] = settings.SkyRadiance;
	consts.SkyRadiance[2] = settings.SkyRadiance;

	consts.Blend[0] = settings.Hysteresis;
	consts.Blend[1] = settings.FastHysteresis;
	consts.Blend[2] = settings.ChangeThreshold;
	consts.Blend[3] = settings.MomentSharpness;

	// The hysteresis is for a level 0 probe that is refreshed once every lap of a full window. A level whose probes
	// come round more or less often than that blends less or more per update, so they all follow the light equally
	// fast, and a map with few live probes gets less noise out of the same budget.
	uint32 share_total = 0;

	for (const uint32 share : scLevelShares) {
		share_total += share;
	}

	const float32 budget = static_cast<float32>(std::max(std::min(settings.Budget, scMaxJobsPerFrame), 1U));
	const float32 nominal_period = static_cast<float32>(scProbesPerLevel) /
								   (budget * static_cast<float32>(scLevelShares[0]) /
									static_cast<float32>(share_total));

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		const float32 quota = budget * static_cast<float32>(scLevelShares[level_index]) /
							  static_cast<float32>(share_total);
		const float32 period = std::max(static_cast<float32>(live_count[level_index]) / std::max(quota, 1.0f), 1.0f);

		consts.LevelRate[level_index] = std::clamp(period / nominal_period, 1.0f / 16.0f, 8.0f);
	}

	consts.LevelRate[3] = settings.DirtyHysteresis;

	const uint32 consts_offset = gGraphics->FrameAlloc.Push(consts);

	const uint32 columns = std::min(job_count, scJobColumns);
	const uint32 rows = (job_count + scJobColumns - 1) / scJobColumns;

	if (mbClearProbeData) {
		vkCmdFillBuffer(cmd.Get(), mProbeData.Buffer, 0, VK_WHOLE_SIZE, 0);
		mbClearProbeData = false;
	}

	ComputeBarrier(cmd);

	// Trace. Every buffer binding is dynamic, so each takes an offset in binding order.
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, gGraphics->LightBuffer.GetBaseOffset());
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, frame * scMaxJobsPerFrame * sizeof(uint32));
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, frame * scLevelCount * scProbesPerLevel * sizeof(uint32));
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->SetFrameData(consts_offset);
	gPipelineCache->Bind(ePipelineName::ProbeClipmapTrace, cmd);

	vkCmdDispatch(cmd.Get(), columns, rows, 1);

	ComputeBarrier(cmd);

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		ImageBarrier(cmd, mpIrradiance[level_index], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
		ImageBarrier(cmd, mpMoments[level_index], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
	}

	// Blend
	gPipelineCache->AddBufferOffset(0, frame * scMaxJobsPerFrame * sizeof(uint32));
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, 0);

	gPipelineCache->SetFrameData(consts_offset);
	gPipelineCache->Bind(ePipelineName::ProbeClipmapBlend, cmd);

	vkCmdDispatch(cmd.Get(), columns, rows, 1);

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		ImageBarrier(cmd, mpIrradiance[level_index], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		ImageBarrier(cmd, mpMoments[level_index], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
}

void ProbeClipmap::WriteForwardData(uint32 frame, const Settings& settings, float32 fallback)
{
	ProbeClipmapForwardData data;
	GetLevelConsts(data.Levels, settings);

	data.Fallback[0] = fallback;
	data.Fallback[1] = fallback;
	data.Fallback[2] = fallback;

	if (gGraphics->IsBakedSunShadowActive()) {
		data.Info[0] |= scForwardFlagBakedSun;
		data.BakedSun = gGraphics->GetBakedSunShadowGpu();
	}

	uint8* mapped = static_cast<uint8*>(mForwardData.pMappedBuffer) + frame * scForwardDataFrameSize;
	memcpy(mapped, &data, sizeof(data));
	mForwardData.FlushToGpu(static_cast<uint32>(frame * scForwardDataFrameSize), sizeof(data));
}

void ProbeClipmap::InitializeImages(CommandBuffer& cmd)
{
	const VkClearColorValue zero {};
	const VkImageSubresourceRange range { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 };

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		Image* images[2] = { mpIrradiance[level_index], mpMoments[level_index] };

		for (Image* image : images) {
			ImageBarrier(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
			vkCmdClearColorImage(cmd.Get(), image->InternalImage, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
			ImageBarrier(cmd, image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
	}

	vkCmdFillBuffer(cmd.Get(), mProbeData.Buffer, 0, VK_WHOLE_SIZE, 0);
	ComputeBarrier(cmd);

	mbImagesInitialized = true;
}

void ProbeClipmap::RenderDebug(CommandBuffer& cmd, const BaseCamera& camera, eDebugView view)
{
	if (!mbImagesInitialized || view == eDebugView::None) {
		return;
	}

	const uint32 frame = gGraphics->GetFrameNumber();

	ProbeClipmapDebugConsts consts;

	Settings settings;
	GetLevelConsts(consts.Levels, settings);

	const FMat4 inverse = camera.GetCameraMatrix(eObjectLayer::WorldLayer).Inverse();
	memcpy(consts.InvViewProjection, inverse.RawData, sizeof(FMat4));

	const FVec3 eye = camera.GetPosition();
	consts.EyePosition[0] = eye.X;
	consts.EyePosition[1] = eye.Y;
	consts.EyePosition[2] = eye.Z;
	consts.EyePosition[3] = 1.0f;

	const UVec2 extent = gGraphics->Swapchain.Extent;
	consts.Info[0] = extent.X;
	consts.Info[1] = extent.Y;
	consts.Info[2] = static_cast<uint32>(view);
	consts.Params[0] = gGraphics->PreExposure;

	Image* lit = &gGraphics->pRenderer->ForwardPass.GetTarget(eImageFormat::RGBA16_Float)->GetImage();

	ComputeBarrier(cmd);

	// The spheres are drawn over what the lit target holds, so its contents are kept
	ImageBarrier(cmd, lit, lit->ImageLayout, VK_IMAGE_LAYOUT_GENERAL);

	gPipelineCache->AddBufferOffset(0, frame * scLevelCount * scProbesPerLevel * sizeof(uint32));
	gPipelineCache->AddBufferOffset(0, 0);

	gPipelineCache->SetFrameData(gGraphics->FrameAlloc.Push(consts));
	gPipelineCache->Bind(ePipelineName::ProbeClipmapDebug, cmd);

	UVec2 groups_extent = (extent + UVec2(scDebugGroupSize - 1)) / UVec2(scDebugGroupSize);
	vkCmdDispatch(cmd.Get(), groups_extent.X, groups_extent.Y, 1);

	BarrierHelper::ImageComputeWriteToRead(cmd, lit);
}

} // namespace fx::renderer
