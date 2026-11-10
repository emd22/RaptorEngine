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
constexpr uint32 scStateValid = 0x01;

/// A slot traced for the first time this frame still holds the probe it had before until the blend, so it counts
/// as unfilled for this frame's reads
constexpr uint8 scPending = 2;

/// Mirror CLIPMAP_FLAG_* in Shaders/ProbeClipmapConsts.hlsli
constexpr uint32 scFlagBounce = 0x01;
constexpr uint32 scFlagRelocate = 0x02;

/// Mirrors STAT_* in Shaders/ProbeClipmapBlend.hlsl
constexpr uint32 scStatBlended = 0;
constexpr uint32 scStatChange = 1;
constexpr uint32 scStatReset = 2;
constexpr uint32 scStatBlack = 3;
constexpr uint32 scStatBackfaces = 4;
constexpr uint32 scStatInactive = 5;
constexpr uint32 scStatMoved = 6;
constexpr uint32 scStatCount = 8;
constexpr float64 scStatChangeScale = 10000.0;

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
	/// Job count, frame
	uint32 Info[4] = {};
	/// Hysteresis, fast hysteresis, change threshold, moment sharpness
	float32 Blend[4] = {};
	float32 SkyRadiance[4] = {};
};

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

static_assert(sizeof(ProbeClipmapLevelGpu) == 32);
static_assert(sizeof(ProbeClipmapConsts) == 192 + 96 + 64 + 80 + 16 * 5);
static_assert(sizeof(ProbeClipmapDebugConsts) == 96 + 64 + 16 * 3);

/// Mirrors SHADE_FLAG_BAKED_SUN in Shaders/SDFPageShade.hlsli
constexpr uint32 scShadeFlagBakedSun = 0x01;

int32 FloorDiv(float32 value, float32 size) { return static_cast<int32>(std::floor(value / size)); }

int32 PositiveMod(int32 value, int32 size) { return ((value % size) + size) % size; }

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
	constexpr float32 scTau = 6.28318531f;

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

	image->Create(eImageType::Flat,
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

		level.Spacing = scLevel0Spacing * static_cast<float32>(1U << level_index);
		level.Valid.InitSize(scProbesPerLevel, 0);

		const uint32 moments_size = (level_index == 0) ? scMomentsSize0 : scMomentsSize;

		mpIrradiance[level_index] = CreateTileImage(scIrradianceSize + 2, eImageFormat::RGBA16_Float);
		mpMoments[level_index] = CreateTileImage(moments_size + 2, eImageFormat::RG16_Float);
	}

	mJobs.Create(eGpuBufferType::Storage, static_cast<uint64>(scMaxJobsPerFrame) * sizeof(uint32) * FramesInFlight,
				 VMA_MEMORY_USAGE_CPU_TO_GPU, eGpuBufferFlags::PersistentMapped);

	mRays.Create(eGpuBufferType::Storage, static_cast<uint64>(scMaxJobsPerFrame) * scRayCount * sizeof(float32) * 4,
				 VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, eGpuBufferFlags::None);

	mState.Create(eGpuBufferType::Storage,
				  static_cast<uint64>(scLevelCount) * scProbesPerLevel * sizeof(uint32) * FramesInFlight,
				  VMA_MEMORY_USAGE_CPU_TO_GPU, eGpuBufferFlags::PersistentMapped);

	mStats.Create(eGpuBufferType::Storage, static_cast<uint64>(scStatCount) * sizeof(uint32) * FramesInFlight,
				  VMA_MEMORY_USAGE_GPU_TO_CPU, eGpuBufferFlags::PersistentMapped | eGpuBufferFlags::TransferReceiver);

	mProbeData.Create(eGpuBufferType::Storage, static_cast<uint64>(scLevelCount) * scProbesPerLevel * sizeof(float32) * 4,
					  VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, eGpuBufferFlags::TransferReceiver);

	Reset();
}

void ProbeClipmap::Destroy()
{
	mJobs.Destroy();
	mRays.Destroy();
	mState.Destroy();
	mStats.Destroy();
	mProbeData.Destroy();
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
	gPSOBuild->AddBuffer(10, 0, eShaderType::Compute, &mJobs, 0, static_cast<uint64>(scMaxJobsPerFrame) * sizeof(uint32));
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
	gPSOBuild->AddBuffer(0, 0, eShaderType::Compute, &mJobs, 0, static_cast<uint64>(scMaxJobsPerFrame) * sizeof(uint32));
	gPSOBuild->AddBuffer(1, 0, eShaderType::Compute, &mRays, 0, VK_WHOLE_SIZE);

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		gPSOBuild->AddStorageImage(2 + level_index, 0, eShaderType::Compute, mpIrradiance[level_index]);
		gPSOBuild->AddStorageImage(5 + level_index, 0, eShaderType::Compute, mpMoments[level_index]);
	}

	gPSOBuild->AddBuffer(8, 0, eShaderType::Compute, &mStats, 0, scStatCount * sizeof(uint32));
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

		for (uint32 slot = 0; slot < scProbesPerLevel; slot++) {
			level.Valid[slot] = 0;
		}
	}
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

			const bool same = old_probe[0] == new_probe[0] && old_probe[1] == new_probe[1] && old_probe[2] == new_probe[2];

			if (!same && level.Valid[slot] != 0) {
				level.Valid[slot] = 0;
				level.InvalidCount++;
			}
		}
	}

	level.Origin[0] = origin[0];
	level.Origin[1] = origin[1];
	level.Origin[2] = origin[2];
	level.bPlaced = true;
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
		}
	}

	// The rest of the budget refreshes the oldest probes, round robin, shared between the levels
	const uint32 remaining = budget - job_count;

	uint32 share_total = 0;

	for (const uint32 share : scLevelShares) {
		share_total += share;
	}

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		Level& level = mLevels[level_index];
		const uint32 quota = std::min(remaining * scLevelShares[level_index] / share_total, scProbesPerLevel);

		for (uint32 index = 0; index < quota && job_count < budget; index++) {
			const uint32 slot = level.Cursor;
			level.Cursor = (level.Cursor + 1) % scProbesPerLevel;

			if (level.Valid[slot] != 1) {
				continue;
			}

			jobs[job_count++] = slot | (level_index << scJobLevelShift);
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
		gpu.Params[2] = settings.RayLengths[level_index];
		gpu.Params[3] = settings.ConeSlope;
	}
}

void ProbeClipmap::LogStats(uint32 frame, uint32 job_count)
{
	const uint32* stats = reinterpret_cast<const uint32*>(mStats.pMappedBuffer) + frame * scStatCount;

	if ((mUpdateCount % scLogInterval) != 0) {
		return;
	}

	const float64 blended = std::max(1.0, static_cast<float64>(stats[scStatBlended]));
	const float64 updated = std::max(1.0, static_cast<float64>(stats[scStatBlended] + stats[scStatReset]));

	LogInfo(LC_RENDER,
			"GI clipmap: {} probes traced ({} new), unfilled {} / {} / {}, mean change {:.4f}, {} black, {:.1f}% "
			"backface rays, {} switched off, {} moved",
			job_count, stats[scStatReset], mLevels[0].InvalidCount, mLevels[1].InvalidCount, mLevels[2].InvalidCount,
			static_cast<float64>(stats[scStatChange]) / scStatChangeScale / blended, stats[scStatBlack],
			100.0 * static_cast<float64>(stats[scStatBackfaces]) / (updated * scRayCount), stats[scStatInactive],
			stats[scStatMoved]);
}

void ProbeClipmap::Update(CommandBuffer& cmd, const FVec3& center, const Settings& settings)
{
	SDFPageScene& pages = gGraphics->SDFPages;

	if (!pages.IsTraceable()) {
		return;
	}

	const uint32 frame = gGraphics->GetFrameNumber();

	// This frame slot's fence has been waited on, so the counts its last use wrote are complete
	mStats.InvalidateFromGpu();
	LogStats(frame, mLastJobCount);
	mUpdateCount++;

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		PlaceLevel(level_index, center);
	}

	uint32* jobs = reinterpret_cast<uint32*>(mJobs.pMappedBuffer) + static_cast<uint64>(frame) * scMaxJobsPerFrame;
	const uint32 job_count = ScheduleJobs(jobs, settings);
	mLastJobCount = job_count;

	uint32* state = reinterpret_cast<uint32*>(mState.pMappedBuffer) +
					static_cast<uint64>(frame) * scLevelCount * scProbesPerLevel;

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		Level& level = mLevels[level_index];
		uint32* level_state = state + static_cast<uint64>(level_index) * scProbesPerLevel;

		for (uint32 slot = 0; slot < scProbesPerLevel; slot++) {
			level_state[slot] = (level.Valid[slot] == 1) ? scStateValid : 0U;

			if (level.Valid[slot] == scPending) {
				level.Valid[slot] = 1;
			}
		}
	}

	const uint32 state_size = scLevelCount * scProbesPerLevel * sizeof(uint32);
	mState.FlushToGpu(frame * state_size, state_size);

	vkCmdFillBuffer(cmd.Get(), mStats.Buffer, static_cast<VkDeviceSize>(frame) * scStatCount * sizeof(uint32),
					scStatCount * sizeof(uint32), 0);

	if (job_count == 0) {
		return;
	}

	mJobs.FlushToGpu(frame * scMaxJobsPerFrame * sizeof(uint32), job_count * sizeof(uint32));

	ProbeClipmapConsts consts;
	pages.GetSceneConsts(consts.SDFPages);
	GetLevelConsts(consts.Levels, settings);
	RandomRotation(gGraphics->GetElapsedFrameCount(), consts.RayRotation);

	gGraphics->SelectShadeLights(center, settings.RayLengths[scLevelCount - 1], consts.LightIndices, consts.Lights[0]);

	if (gGraphics->IsBakedSunShadowActive()) {
		consts.Lights[1] |= scShadeFlagBakedSun;
		consts.BakedSun = gGraphics->GetBakedSunShadowGpu();
	}

	consts.Info[0] = job_count;
	consts.Info[1] = gGraphics->GetElapsedFrameCount();

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

	const uint32 consts_offset = gGraphics->FrameAlloc.Push(consts);

	const uint32 columns = std::min(job_count, scJobColumns);
	const uint32 rows = (job_count + scJobColumns - 1) / scJobColumns;

	if (!mbImagesInitialized) {
		InitializeImages(cmd);
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
	gPipelineCache->AddBufferOffset(0, frame * scStatCount * sizeof(uint32));
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->SetFrameData(consts_offset);
	gPipelineCache->Bind(ePipelineName::ProbeClipmapBlend, cmd);

	vkCmdDispatch(cmd.Get(), columns, rows, 1);

	for (uint32 level_index = 0; level_index < scLevelCount; level_index++) {
		ImageBarrier(cmd, mpIrradiance[level_index], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		ImageBarrier(cmd, mpMoments[level_index], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
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

	vkCmdDispatch(cmd.Get(), (extent.X + scDebugGroupSize - 1) / scDebugGroupSize,
				  (extent.Y + scDebugGroupSize - 1) / scDebugGroupSize, 1);

	BarrierHelper::ImageComputeWriteToRead(cmd, lit);
}

} // namespace fx::renderer
