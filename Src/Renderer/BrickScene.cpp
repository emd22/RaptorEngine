#include "BrickScene.hpp"

#include "Backend/BarrierHelper.hpp"
#include "Backend/DescriptorCache.hpp"
#include "Backend/Sampler/SamplerCache.hpp"
#include "Camera/BaseCamera.hpp"
#include "GraphicsBackend.hpp"
#include "PSOBuild.hpp"
#include "PipelineCache.hpp"
#include "Target.hpp"

#include <Core/Log.hpp>
#include <Renderer/Globals.hpp>
#include <Texture/TextureManager.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace fx::renderer {

namespace {

constexpr uint64 scPlaceholderBytes = 64;
constexpr uint32 scUpdateGroupColumns = 1024;
constexpr uint32 scDebugGroupSize = 8;
constexpr uint32 scStatCount = 8;
constexpr uint32 scLogInterval = 120;

constexpr uint8 scDirtyIndirection = 1;
constexpr uint8 scDirtyEval = 2;

struct BrickUpdateConsts
{
	/// Job count, then the atlas size in bricks
	uint32 Info[4] = {};
	/// Margin as a fraction of a brick
	float32 Params[4] = {};
};

struct BrickDebugConsts
{
	BrickSceneGpu Scene;
	float32 InvViewProjection[16] = {};
	float32 EyePosition[4] = {};
	/// Target width and height, view, frame
	uint32 Info[4] = {};
	/// Cone slope and length of the GI rays
	float32 Params[4] = {};
};

static_assert(sizeof(BrickLevelGpu) == 32);
static_assert(sizeof(BrickSceneGpu) == 192);
static_assert(sizeof(CompiledMapBrickTriangle) == 48);
static_assert(sizeof(BrickUpdateConsts) == 32);
static_assert(sizeof(BrickDebugConsts) == 192 + 64 + 16 + 16 + 16);

/// Every buffer binding is dynamic, so each takes an offset in binding order
void AddZeroOffsets(uint32 count)
{
	for (uint32 i = 0; i < count; i++) {
		gPipelineCache->AddBufferOffset(0, 0);
	}
}

void ComputeBarrier(const CommandBuffer& cmd)
{
	const VkMemoryBarrier2 barrier {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
	};

	const VkDependencyInfo dep_info {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = 1,
		.pMemoryBarriers = &barrier,
	};

	vkCmdPipelineBarrier2(cmd.Cmd, &dep_info);
}

/// The atlas keeps its bricks between frames, so its layout changes must not discard them
void AtlasTransition(const CommandBuffer& cmd, Image* atlas, VkImageLayout from, VkImageLayout to)
{
	const VkImageMemoryBarrier2 barrier {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
		.oldLayout = from,
		.newLayout = to,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = atlas->InternalImage,
		.subresourceRange { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 },
	};

	const VkDependencyInfo dep_info {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &barrier,
	};

	vkCmdPipelineBarrier2(cmd.Cmd, &dep_info);
}

int32 FloorDiv(float32 value, float32 size) { return static_cast<int32>(std::floor(value / size)); }

} // namespace

void BrickScene::CreateBuffer(RawGpuBuffer& buffer, uint64 size, const void* data)
{
	buffer.Destroy();

	const uint64 buffer_size = std::max(size, scPlaceholderBytes);

	buffer.Create(eGpuBufferType::Storage, buffer_size, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
				  eGpuBufferFlags::PersistentMapped);

	uint8* mapped = static_cast<uint8*>(buffer.pMappedBuffer);

	memset(mapped, 0, buffer_size);

	if (data != nullptr && size > 0) {
		memcpy(mapped, data, size);
	}

	buffer.FlushToGpu(0, static_cast<uint32>(buffer_size));
}

void BrickScene::Create()
{
	CreateBuffer(mRefs, 0, nullptr);
	CreateBuffer(mTriangles, 0, nullptr);

	mIndirection.Create(eGpuBufferType::Storage, static_cast<uint64>(scSlotsPerLevel) * scMaxLevels * sizeof(uint32),
						VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, eGpuBufferFlags::TransferReceiver);

	mJobs.Create(eGpuBufferType::Storage, static_cast<uint64>(scMaxJobsPerFrame) * sizeof(Job) * FramesInFlight,
				 VMA_MEMORY_USAGE_CPU_TO_GPU, eGpuBufferFlags::PersistentMapped);

	mStats.Create(eGpuBufferType::Storage, static_cast<uint64>(scStatCount) * sizeof(uint32) * FramesInFlight,
				  VMA_MEMORY_USAGE_GPU_TO_CPU, eGpuBufferFlags::PersistentMapped | eGpuBufferFlags::TransferReceiver);

	mpAtlas = gTextureManager->NewTexture();
	mpAtlas->CreateVolume(Vec2u(scAtlasBricksX * scTexelsPerBrick, scAtlasBricksY * scTexelsPerBrick),
						  scAtlasBricksZ * scTexelsPerBrick, eImageFormat::R8_UNorm,
						  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	mbAtlasInitialized = false;

	mFreeAtlas.InitCapacity(scAtlasBrickCount);

	for (uint32 level = 0; level < scMaxLevels; level++) {
		mLevels[level].SlotAtlas.InitSize(scSlotsPerLevel, scEmpty);
		mLevels[level].SlotBrick.InitSize(scSlotsPerLevel, scEmpty);
		mLevels[level].SlotDirty.InitSize(scSlotsPerLevel, 0);
	}
}

void BrickScene::CreatePipelines(Target* depth, Target* normals, Target* motion, Target* lit)
{
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

	gPSOBuild->BeginPipeline(ePipelineName::BrickUpdate);
	gPSOBuild->SetShader(eShaderName::BrickUpdate, {});
	gPSOBuild->AddBuffer(0, 0, eShaderType::Compute, &mJobs, 0, static_cast<uint64>(scMaxJobsPerFrame) * sizeof(Job));
	gPSOBuild->AddBuffer(1, 0, eShaderType::Compute, &mRefs, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddBuffer(2, 0, eShaderType::Compute, &mTriangles, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddBuffer(3, 0, eShaderType::Compute, &mIndirection, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddStorageImage(4, 0, eShaderType::Compute, mpAtlas);
	gPSOBuild->AddFrameData(1);
	gPSOBuild->EndPipeline();

	gPSOBuild->BeginPipeline(ePipelineName::BrickDebug);
	gPSOBuild->SetShader(eShaderName::BrickDebug, {});
	gPSOBuild->AddBuffer(0, 0, eShaderType::Compute, &mIndirection, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddImage(1, 0, eShaderType::Compute, mpAtlas, linear_sampler);
	gPSOBuild->AddImageFromTarget(2, 0, eShaderType::Compute, depth, point_sampler);
	gPSOBuild->AddImageFromTarget(3, 0, eShaderType::Compute, normals, point_sampler);
	gPSOBuild->AddImageFromTarget(4, 0, eShaderType::Compute, motion, point_sampler);
	gPSOBuild->AddStorageImage(5, 0, eShaderType::Compute, &lit->GetImage());
	gPSOBuild->AddBuffer(6, 0, eShaderType::Compute, &mStats, 0, scStatCount * sizeof(uint32));
	gPSOBuild->AddFrameData(1);
	gPSOBuild->EndPipeline();
}

void BrickScene::Destroy()
{
	mRefs.Destroy();
	mTriangles.Destroy();
	mIndirection.Destroy();
	mJobs.Destroy();
	mStats.Destroy();

	mbActive = false;
}

bool BrickScene::Upload(const CompiledMapBricks& bricks)
{
	if (bricks.IsEmpty()) {
		Clear();
		return false;
	}

	gGraphics->GetDevice()->WaitForIdle();

	CreateBuffer(mRefs, bricks.Refs.GetSizeInBytes(), bricks.Refs.pData);
	CreateBuffer(mTriangles, bricks.Triangles.GetSizeInBytes(), bricks.Triangles.pData);

	const RawGpuBuffer* buffers[] = { &mRefs, &mTriangles };

	gDescriptorCache->RebuildUsing(Slice<const RawGpuBuffer* const>(buffers, std::size(buffers)),
								   Slice<Image* const>(nullptr, 0));

	mBricks.Free();
	mBricks.InitSize(bricks.Bricks.Size);
	memcpy(mBricks.pData, bricks.Bricks.pData, bricks.Bricks.GetSizeInBytes());

	mLevelCount = std::min(static_cast<uint32>(bricks.Levels.Size), scMaxLevels);
	mMarginScale = bricks.MarginScale;

	for (uint32 level_index = 0; level_index < mLevelCount; level_index++) {
		const CompiledMapBrickLevel& source = bricks.Levels[level_index];
		Level& level = mLevels[level_index];

		level.BrickSize = source.BrickSize;
		level.FirstBrick = source.FirstBrick;
		level.BrickCount = source.BrickCount;
		level.bPlaced = false;
	}

	mFreeAtlas.Clear();

	for (uint32 atlas = scAtlasBrickCount; atlas > 0; atlas--) {
		mFreeAtlas.Insert(atlas - 1);
	}

	for (uint32 level_index = 0; level_index < scMaxLevels; level_index++) {
		Level& level = mLevels[level_index];

		for (uint32 slot = 0; slot < scSlotsPerLevel; slot++) {
			level.SlotAtlas[slot] = scEmpty;
			level.SlotBrick[slot] = scEmpty;
			level.SlotDirty[slot] = 0;
		}

		level.Pending.Clear();
		level.PendingRead = 0;
	}

	mDirty.Clear();
	mbActive = true;

	uint64 total_bricks = 0;

	for (uint32 level_index = 0; level_index < mLevelCount; level_index++) {
		total_bricks += mLevels[level_index].BrickCount;
	}

	LogInfo(LC_RENDER, "Brick scene: {} levels, {} bricks, {} triangles ({:.1f} MB)", mLevelCount, total_bricks,
			bricks.Triangles.Size,
			static_cast<double>(bricks.Triangles.GetSizeInBytes() + bricks.Refs.GetSizeInBytes()) / (1024.0 * 1024.0));

	return true;
}

void BrickScene::Clear()
{
	if (!mbActive) {
		return;
	}

	gGraphics->GetDevice()->WaitForIdle();

	mbActive = false;
	mBricks.Free();
	mLevelCount = 0;
}

uint32 BrickScene::GetSlot(const int32 coord[3]) const
{
	constexpr uint32 scMask = scClipBricks - 1;

	const uint32 x = static_cast<uint32>(coord[0]) & scMask;
	const uint32 y = static_cast<uint32>(coord[1]) & scMask;
	const uint32 z = static_cast<uint32>(coord[2]) & scMask;

	return x + y * scClipBricks + z * scClipBricks * scClipBricks;
}

uint32 BrickScene::GetPendingCount() const
{
	uint32 count = 0;

	for (uint32 level_index = 0; level_index < mLevelCount; level_index++) {
		count += static_cast<uint32>(mLevels[level_index].Pending.Size) - mLevels[level_index].PendingRead;
	}

	return count;
}

void BrickScene::FindBrickRange(const Level& level, int32 min_x, int32 max_x, uint32& out_begin,
								uint32& out_end) const
{
	const CompiledMapBrick* first = mBricks.pData + level.FirstBrick;
	const CompiledMapBrick* last = first + level.BrickCount;

	const CompiledMapBrick* begin = std::lower_bound(first, last, min_x, [](const CompiledMapBrick& brick, int32 x)
													 { return brick.Coord[0] < x; });
	const CompiledMapBrick* end = std::lower_bound(begin, last, max_x, [](const CompiledMapBrick& brick, int32 x)
												   { return brick.Coord[0] < x; });

	out_begin = static_cast<uint32>(begin - mBricks.pData);
	out_end = static_cast<uint32>(end - mBricks.pData);
}

void BrickScene::MarkDirty(uint32 level_index, uint32 slot, uint8 value)
{
	Level& level = mLevels[level_index];

	if (level.SlotDirty[slot] == 0) {
		mDirty.Insert((level_index << 24) | slot);
	}

	level.SlotDirty[slot] = std::max(level.SlotDirty[slot], value);
}

void BrickScene::FreeSlot(Level& level, uint32 slot)
{
	if (level.SlotAtlas[slot] != scEmpty) {
		mFreeAtlas.Insert(level.SlotAtlas[slot]);
		level.SlotAtlas[slot] = scEmpty;
	}

	level.SlotBrick[slot] = scEmpty;
}

void BrickScene::ResetLevel(uint32 level_index)
{
	Level& level = mLevels[level_index];

	for (uint32 slot = 0; slot < scSlotsPerLevel; slot++) {
		FreeSlot(level, slot);
		level.SlotDirty[slot] = 0;
	}

	level.Pending.Clear();
	level.PendingRead = 0;

	// Dirty slots of this level are now covered by the GPU clear
	uint32 kept = 0;

	for (uint32 index = 0; index < mDirty.Size; index++) {
		if ((mDirty[index] >> 24) != level_index) {
			mDirty[kept++] = mDirty[index];
		}
	}

	mDirty.Size = kept;
}

bool BrickScene::PlaceLevel(uint32 level_index, const Vec3f& center)
{
	Level& level = mLevels[level_index];

	constexpr int32 scHalfClip = static_cast<int32>(scClipBricks / 2);

	const int32 new_origin[3] = {
		FloorDiv(center.X, level.BrickSize) - scHalfClip,
		FloorDiv(center.Y, level.BrickSize) - scHalfClip,
		FloorDiv(center.Z, level.BrickSize) - scHalfClip,
	};

	const bool was_placed = level.bPlaced;
	bool reset = !was_placed;

	if (was_placed && new_origin[0] == level.Origin[0] && new_origin[1] == level.Origin[1] &&
		new_origin[2] == level.Origin[2]) {
		return false;
	}

	for (uint32 axis = 0; axis < 3; axis++) {
		if (std::abs(new_origin[axis] - level.Origin[axis]) >= static_cast<int32>(scClipBricks)) {
			reset = true;
		}
	}

	const int32 old_origin[3] = { level.Origin[0], level.Origin[1], level.Origin[2] };
	const int32 clip = static_cast<int32>(scClipBricks);

	const auto is_inside = [clip](const int32 coord[3], const int32 origin[3])
	{
		return coord[0] >= origin[0] && coord[0] < origin[0] + clip && coord[1] >= origin[1] &&
			   coord[1] < origin[1] + clip && coord[2] >= origin[2] && coord[2] < origin[2] + clip;
	};

	if (reset) {
		ResetLevel(level_index);
	}
	else {
		uint32 begin = 0;
		uint32 end = 0;
		FindBrickRange(level, old_origin[0], old_origin[0] + clip, begin, end);

		for (uint32 brick = begin; brick < end; brick++) {
			const int32* coord = mBricks[brick].Coord;

			if (is_inside(coord, old_origin) && !is_inside(coord, new_origin)) {
				const uint32 slot = GetSlot(coord);

				FreeSlot(level, slot);
				MarkDirty(level_index, slot, scDirtyIndirection);
			}
		}
	}

	level.Origin[0] = new_origin[0];
	level.Origin[1] = new_origin[1];
	level.Origin[2] = new_origin[2];
	level.bPlaced = true;

	uint32 begin = 0;
	uint32 end = 0;
	FindBrickRange(level, new_origin[0], new_origin[0] + clip, begin, end);

	for (uint32 brick = begin; brick < end; brick++) {
		const int32* coord = mBricks[brick].Coord;

		if (!is_inside(coord, new_origin) || (!reset && is_inside(coord, old_origin))) {
			continue;
		}

		const uint32 slot = GetSlot(coord);

		level.SlotBrick[slot] = brick;
		level.Pending.Insert(slot);
		MarkDirty(level_index, slot, scDirtyIndirection);
	}

	return reset;
}

void BrickScene::GetSceneConsts(BrickSceneGpu& out_consts) const
{
	out_consts = BrickSceneGpu {};

	for (uint32 level_index = 0; level_index < mLevelCount; level_index++) {
		const Level& level = mLevels[level_index];
		BrickLevelGpu& gpu = out_consts.Levels[level_index];

		gpu.Origin[0] = level.Origin[0];
		gpu.Origin[1] = level.Origin[1];
		gpu.Origin[2] = level.Origin[2];

		gpu.Params[0] = level.BrickSize;
		gpu.Params[1] = 1.0f / level.BrickSize;
		gpu.Params[2] = level.BrickSize * mMarginScale;
		gpu.Params[3] = level.BrickSize / static_cast<float32>(scTexelsPerBrick);
	}

	out_consts.Info[0] = mLevelCount;
	out_consts.Info[1] = scAtlasBricksX;
	out_consts.Info[2] = scAtlasBricksY;
	out_consts.Info[3] = scAtlasBricksZ;

	out_consts.InvAtlasSize[0] = 1.0f / static_cast<float32>(scAtlasBricksX * scTexelsPerBrick);
	out_consts.InvAtlasSize[1] = 1.0f / static_cast<float32>(scAtlasBricksY * scTexelsPerBrick);
	out_consts.InvAtlasSize[2] = 1.0f / static_cast<float32>(scAtlasBricksZ * scTexelsPerBrick);
}

void BrickScene::Update(CommandBuffer& cmd, const Vec3f& center, uint32 eval_budget)
{
	if (!mbActive) {
		return;
	}

	bool reset_levels[scMaxLevels] = {};
	bool any_reset = false;

	for (uint32 level_index = 0; level_index < mLevelCount; level_index++) {
		reset_levels[level_index] = PlaceLevel(level_index, center);
		any_reset = any_reset || reset_levels[level_index];
	}

	// Coarse levels first, so the whole view is covered before the fine detail arrives
	uint32 evaluated = 0;

	for (uint32 level_index = mLevelCount; level_index > 0 && evaluated < eval_budget; level_index--) {
		Level& level = mLevels[level_index - 1];

		while (level.PendingRead < level.Pending.Size && evaluated < eval_budget && !mFreeAtlas.IsEmpty()) {
			const uint32 slot = level.Pending[level.PendingRead++];

			if (level.SlotBrick[slot] == scEmpty || level.SlotAtlas[slot] != scEmpty) {
				continue;
			}

			level.SlotAtlas[slot] = mFreeAtlas[mFreeAtlas.Size - 1];
			mFreeAtlas.Size--;

			MarkDirty(level_index - 1, slot, scDirtyEval);
			evaluated++;
		}

		if (level.PendingRead == level.Pending.Size) {
			level.Pending.Clear();
			level.PendingRead = 0;
		}
	}

	const uint32 frame = gGraphics->GetFrameNumber();
	Job* jobs = reinterpret_cast<Job*>(mJobs.pMappedBuffer) + static_cast<uint64>(frame) * scMaxJobsPerFrame;
	uint32 job_count = 0;
	uint32 kept = 0;

	for (uint32 index = 0; index < mDirty.Size; index++) {
		const uint32 entry = mDirty[index];
		const uint32 level_index = entry >> 24;
		const uint32 slot = entry & 0xFFFFFF;

		// Whatever did not fit waits for the next frame
		if (job_count == scMaxJobsPerFrame) {
			mDirty[kept++] = entry;
			continue;
		}

		Level& level = mLevels[level_index];
		Job& job = jobs[job_count++];

		job = Job {};
		job.Slot = level_index * scSlotsPerLevel + slot;

		if (level.SlotAtlas[slot] != scEmpty) {
			job.Atlas = level.SlotAtlas[slot];

			if (level.SlotDirty[slot] == scDirtyEval) {
				const CompiledMapBrick& brick = mBricks[level.SlotBrick[slot]];

				job.FirstRef = brick.FirstRef;
				job.RefCount = brick.RefCount;
				job.OriginAndTexel[0] = static_cast<float32>(brick.Coord[0]) * level.BrickSize;
				job.OriginAndTexel[1] = static_cast<float32>(brick.Coord[1]) * level.BrickSize;
				job.OriginAndTexel[2] = static_cast<float32>(brick.Coord[2]) * level.BrickSize;
				job.OriginAndTexel[3] = level.BrickSize / static_cast<float32>(scTexelsPerBrick);
			}
		}
		else if (level.SlotBrick[slot] != scEmpty) {
			job.Atlas = scPending;
		}

		level.SlotDirty[slot] = 0;
	}

	mDirty.Size = kept;

	if (job_count > 0) {
		mJobs.FlushToGpu(frame * scMaxJobsPerFrame * sizeof(Job), job_count * sizeof(Job));
	}

	if (!any_reset && job_count == 0) {
		return;
	}

	ComputeBarrier(cmd);

	for (uint32 level_index = 0; level_index < mLevelCount; level_index++) {
		if (reset_levels[level_index]) {
			vkCmdFillBuffer(cmd.Get(), mIndirection.Buffer,
							static_cast<VkDeviceSize>(level_index) * scSlotsPerLevel * sizeof(uint32),
							static_cast<VkDeviceSize>(scSlotsPerLevel) * sizeof(uint32), scEmpty);
		}
	}

	if (job_count == 0) {
		ComputeBarrier(cmd);
		return;
	}

	ComputeBarrier(cmd);

	const VkImageLayout atlas_from = mbAtlasInitialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
														: VK_IMAGE_LAYOUT_UNDEFINED;
	AtlasTransition(cmd, mpAtlas, atlas_from, VK_IMAGE_LAYOUT_GENERAL);
	mbAtlasInitialized = true;

	BrickUpdateConsts consts {
		.Info = { job_count, scAtlasBricksX, scAtlasBricksY, scAtlasBricksZ },
		.Params = { mMarginScale, 0.0f, 0.0f, 0.0f },
	};

	gPipelineCache->AddBufferOffset(0, frame * scMaxJobsPerFrame * sizeof(Job));
	AddZeroOffsets(3);
	gPipelineCache->SetFrameData(gGraphics->FrameAlloc.Push(consts));
	gPipelineCache->Bind(ePipelineName::BrickUpdate, cmd);

	const uint32 columns = std::min(job_count, scUpdateGroupColumns);
	const uint32 rows = (job_count + scUpdateGroupColumns - 1) / scUpdateGroupColumns;

	vkCmdDispatch(cmd.Get(), columns, rows, 1);

	AtlasTransition(cmd, mpAtlas, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	ComputeBarrier(cmd);
}

void BrickScene::RenderDebug(CommandBuffer& cmd, const BaseCamera& camera, eDebugView view, float32 cone_slope, float32 ray_length)
{
	if (!mbActive || !mbAtlasInitialized || view == eDebugView::None) {
		return;
	}

	const uint32 frame = gGraphics->GetFrameNumber();
	uint32* stats = reinterpret_cast<uint32*>(mStats.pMappedBuffer) + frame * scStatCount;

	// This frame slot's fence has been waited on, so the counts its last use wrote are complete
	mStats.InvalidateFromGpu();

	if (view == eDebugView::DepthCheck) {
		mCheckStats = BrickCheckStats {
			.Match = stats[0],
			.Early = stats[1],
			.Late = stats[2],
			.Sky = stats[3],
			.Skipped = stats[4],
		};
	}

	const uint32 ray_count = stats[6];
	const double mean_steps = static_cast<double>(stats[5]) / std::max(1.0, static_cast<double>(ray_count));
	const uint32 max_step_rays = stats[7];

	if ((mDebugFrame++ % scLogInterval) == 0) {
		const BrickCheckStats& check = mCheckStats;
		const double surfaces = std::max(1.0, static_cast<double>(check.Match + check.Early + check.Late));

		LogInfo(LC_RENDER,
				"Bricks: view {} cone {:.3f}, {} resident, {} pending, update {:.3f} ms, debug {:.3f} ms, {} rays, "
				"{:.1f} steps, {} at the step limit, check {:.2f}% match {:.2f}% early {:.2f}% late ({} surface, {} "
				"sky, {} skipped pixels)",
				static_cast<uint32>(view), cone_slope, GetResidentCount(), GetPendingCount(),
				gGraphics->Profiler.GetMs(eGpuMarker::BrickScene), gGraphics->Profiler.GetMs(eGpuMarker::BrickDebug),
				ray_count, mean_steps, max_step_rays,
				100.0 * check.Match / surfaces, 100.0 * check.Early / surfaces, 100.0 * check.Late / surfaces,
				check.Match + check.Early + check.Late, check.Sky, check.Skipped);
	}

	vkCmdFillBuffer(cmd.Get(), mStats.Buffer, static_cast<VkDeviceSize>(frame) * scStatCount * sizeof(uint32),
					scStatCount * sizeof(uint32), 0);

	BrickDebugConsts consts;
	GetSceneConsts(consts.Scene);

	const Mat4f inverse = camera.GetCameraMatrix(eObjectLayer::WorldLayer).Inverse();
	memcpy(consts.InvViewProjection, inverse.RawData, sizeof(Mat4f));

	const Vec3f eye = camera.GetPosition();
	consts.EyePosition[0] = eye.X;
	consts.EyePosition[1] = eye.Y;
	consts.EyePosition[2] = eye.Z;
	consts.EyePosition[3] = 1.0f;

	const Vec2u extent = gGraphics->Swapchain.Extent;
	consts.Info[0] = extent.X;
	consts.Info[1] = extent.Y;
	consts.Info[2] = static_cast<uint32>(view);
	consts.Info[3] = gGraphics->GetElapsedFrameCount();
	consts.Params[0] = cone_slope;
	consts.Params[1] = ray_length;

	Image* lit = &gGraphics->pRenderer->ForwardPass.GetTarget(eImageFormat::RGBA16_Float)->GetImage();

	ComputeBarrier(cmd);
	BarrierHelper::ImageBeginComputeWrite(cmd, lit);

	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, frame * scStatCount * sizeof(uint32));
	gPipelineCache->SetFrameData(gGraphics->FrameAlloc.Push(consts));
	gPipelineCache->Bind(ePipelineName::BrickDebug, cmd);

	vkCmdDispatch(cmd.Get(), (extent.X + scDebugGroupSize - 1) / scDebugGroupSize,
				  (extent.Y + scDebugGroupSize - 1) / scDebugGroupSize, 1);

	BarrierHelper::ImageComputeWriteToRead(cmd, lit);
}

} // namespace fx::renderer
