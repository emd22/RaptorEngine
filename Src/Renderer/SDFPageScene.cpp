#include "SDFPageScene.hpp"

#include "Backend/BarrierHelper.hpp"
#include "Backend/DescriptorCache.hpp"
#include "Backend/Sampler/SamplerCache.hpp"
#include "Camera/BaseCamera.hpp"
#include "GraphicsBackend.hpp"
#include "PSOBuild.hpp"
#include "PipelineCache.hpp"
#include "ShadowAtlas.hpp"
#include "Target.hpp"

#include <Core/Log.hpp>
#include <Core/Memory.hpp>
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

/// Mirrors SDF_PAGE_NEAREST_BLOCK in Shaders/SDFPageCommon.hlsli
constexpr uint32 scNearestBlock = 2;

constexpr uint32 scShadeFlagBakedSun = 0x01;
constexpr uint32 scLogInterval = 120;

constexpr uint8 scDirtyIndirection = 1;
constexpr uint8 scDirtyEval = 2;

struct SDFPageUpdateConsts
{
	/// Job count, then the atlas size in pages
	uint32 Info[4] = {};
	/// Margin as a fraction of a page
	float32 Params[4] = {};
};

struct SDFPageDebugConsts
{
	SDFPageSceneGpu Scene;
	float32 InvViewProjection[16] = {};
	float32 EyePosition[4] = {};
	/// Target width and height, view, frame
	uint32 Info[4] = {};
	/// Cone slope and length of the GI rays, pre-exposure
	float32 Params[4] = {};
	BakedSunShadowGpu BakedSun;
	/// Light buffer slots shaded at each hit
	uint32 LightIndices[SDFPageScene::scShadeMaxLights] = {};
	/// Light count, flags, probe stride
	uint32 Lights[4] = {};
	/// Radiance of the rays that miss
	float32 SkyRadiance[4] = {};
};

static_assert(sizeof(SDFPageLevelGpu) == 32);
static_assert(sizeof(SDFPageSceneGpu) == 192);
static_assert(sizeof(CompiledMapSDFPageTriangle) == 48);
static_assert(sizeof(SDFPageUpdateConsts) == 32);
static_assert(sizeof(BakedSunShadowGpu) == 80);
static_assert(sizeof(SDFPageDebugConsts) == 192 + 64 + 16 + 16 + 16 + 80 + 64 + 16 + 16);


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

/// The atlas keeps its pages between frames, so its layout changes must not discard them
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

/// The triangle's face normal, plane and albedo, as SDFPageRecordFace and SDFPageRecordAlbedo read them
SDFPageHitRecordGpu MakeHitRecord(const CompiledMapSDFPageTriangle& page_triangle)
{
	const float32(&corners)[3][4] = page_triangle.Corners;

	float32 edge_b[3];
	float32 edge_c[3];

	for (uint32 axis = 0; axis < 3; axis++) {
		edge_b[axis] = corners[1][axis] - corners[0][axis];
		edge_c[axis] = corners[2][axis] - corners[0][axis];
	}

	const float32 normal[3] = {
		edge_b[1] * edge_c[2] - edge_b[2] * edge_c[1],
		edge_b[2] * edge_c[0] - edge_b[0] * edge_c[2],
		edge_b[0] * edge_c[1] - edge_b[1] * edge_c[0],
	};

	const float32 length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);

	uint32 albedo = 0;
	memcpy(&albedo, &corners[0][3], sizeof(albedo));

	SDFPageHitRecordGpu record;
	record.Albedo = albedo & 0xFFFFFFu;

	// The same threshold the shaders used on the cross product before
	if (!(length > 1e-10f)) {
		return record;
	}

	const float32 unit[3] = { normal[0] / length, normal[1] / length, normal[2] / length };
	const float32 l1 = std::abs(unit[0]) + std::abs(unit[1]) + std::abs(unit[2]);
	const auto sign = [](float32 value) { return (value >= 0.0f) ? 1.0f : -1.0f; };

	float32 oct_x = unit[0] / l1;
	float32 oct_y = unit[1] / l1;

	if (unit[2] < 0.0f) {
		const float32 folded_x = (1.0f - std::abs(oct_y)) * sign(oct_x);
		const float32 folded_y = (1.0f - std::abs(oct_x)) * sign(oct_y);
		oct_x = folded_x;
		oct_y = folded_y;
	}

	const auto to_snorm = [](float32 value)
	{ return static_cast<int32>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f)); };

	const int32 snorm_x = to_snorm(oct_x);
	const int32 snorm_y = to_snorm(oct_y);

	record.Normal = (static_cast<uint32>(snorm_x) & 0xFFFFu) | ((static_cast<uint32>(snorm_y) & 0xFFFFu) << 16);
	record.Albedo |= SDFPageHitRecordGpu::scHasFace;

	// The plane goes by the normal as the shaders decode it, so it holds the corners to within rounding
	const float32 e_x = static_cast<float32>(snorm_x) / 32767.0f;
	const float32 e_y = static_cast<float32>(snorm_y) / 32767.0f;
	const float32 e_z = 1.0f - std::abs(e_x) - std::abs(e_y);

	float32 decoded[3] = { e_x, e_y, e_z };

	if (e_z < 0.0f) {
		decoded[0] = (1.0f - std::abs(e_y)) * sign(e_x);
		decoded[1] = (1.0f - std::abs(e_x)) * sign(e_y);
	}

	const float32 decoded_length =
		std::sqrt(decoded[0] * decoded[0] + decoded[1] * decoded[1] + decoded[2] * decoded[2]);

	record.Plane = (decoded[0] * corners[0][0] + decoded[1] * corners[0][1] + decoded[2] * corners[0][2]) /
				   decoded_length;

	return record;
}

} // namespace

void SDFPageScene::CreateBuffer(RawGpuBuffer& buffer, uint64 size, const void* data)
{
	buffer.Destroy();

	const uint64 buffer_size = std::max(size, scPlaceholderBytes);

	buffer.Create(eGpuBufferType::Storage, buffer_size, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
				  eGpuBufferFlags::PersistentMapped);

	uint8* mapped = static_cast<uint8*>(buffer.pMappedBuffer);
	MemorySetRaw(mapped, 0, buffer_size);

	if (data != nullptr && size > 0) {
		MemoryCopyRaw(mapped, static_cast<const uint8*>(data), size);
	}

	buffer.FlushToGpu(0, static_cast<uint32>(buffer_size));
}

void SDFPageScene::Create()
{
	CreateBuffer(mRefs, 0, nullptr);
	CreateBuffer(mTriangles, 0, nullptr);
	CreateBuffer(mHitRecords, 0, nullptr);

	mIndirection.Create(eGpuBufferType::Storage, static_cast<uint64>(scSlotsPerLevel) * scMaxLevels * sizeof(uint32),
						VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, eGpuBufferFlags::TransferReceiver);

	mJobs.Create(eGpuBufferType::Storage, static_cast<uint64>(scMaxJobsPerFrame) * sizeof(Job) * FramesInFlight,
				 VMA_MEMORY_USAGE_CPU_TO_GPU, eGpuBufferFlags::PersistentMapped);

	mpAtlas = gTextureManager->NewTexture();
	mpAtlas->CreateVolume(UVec2(scAtlasSDFPagesX * scTexelsPerSDFPage, scAtlasSDFPagesY * scTexelsPerSDFPage),
						  scAtlasSDFPagesZ * scTexelsPerSDFPage, eImageFormat::R8_UNorm,
						  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

	mpNearest = gTextureManager->NewTexture();
	mpNearest->CreateVolume(UVec2(scAtlasSDFPagesX * scTexelsPerSDFPage / scNearestBlock,
								  scAtlasSDFPagesY * scTexelsPerSDFPage / scNearestBlock),
							scAtlasSDFPagesZ * scTexelsPerSDFPage / scNearestBlock, eImageFormat::R16_UInt,
							VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

	mAtlasFirstRef.Create(eGpuBufferType::Storage, static_cast<uint64>(scAtlasSDFPageCount) * sizeof(uint32),
						  VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, eGpuBufferFlags::None);

	mbAtlasInitialized = false;

	mFreeAtlas.InitCapacity(scAtlasSDFPageCount);

	for (uint32 level = 0; level < scMaxLevels; level++) {
		mLevels[level].SlotAtlas.InitSize(scSlotsPerLevel, SDFPageID::scEmpty);
		mLevels[level].SlotSDFPage.InitSize(scSlotsPerLevel, SDFPageID::scEmpty);
		mLevels[level].SlotDirty.InitSize(scSlotsPerLevel, 0);
	}
}

void SDFPageScene::CreatePipelines(Target* depth, Target* normals, Target* motion, Target* lit)
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

	{
		gPSOBuild->BeginPipeline(ePipelineName::SDFPageUpdate);
		gPSOBuild->SetShader(eShaderName::SDFPageUpdate, {});
		gPSOBuild->AddBuffer(0, 0, eShaderType::Compute, &mJobs, 0,
							 static_cast<uint64>(scMaxJobsPerFrame) * sizeof(Job));
		gPSOBuild->AddBuffer(1, 0, eShaderType::Compute, &mRefs, 0, VK_WHOLE_SIZE);
		gPSOBuild->AddBuffer(2, 0, eShaderType::Compute, &mTriangles, 0, VK_WHOLE_SIZE);
		gPSOBuild->AddBuffer(3, 0, eShaderType::Compute, &mIndirection, 0, VK_WHOLE_SIZE);
		gPSOBuild->AddStorageImage(4, 0, eShaderType::Compute, mpAtlas);
		gPSOBuild->AddStorageImage(5, 0, eShaderType::Compute, mpNearest);
		gPSOBuild->AddBuffer(6, 0, eShaderType::Compute, &mAtlasFirstRef, 0, VK_WHOLE_SIZE);
		gPSOBuild->AddFrameData(1);
		gPSOBuild->EndPipeline();
	}

	{
		gPSOBuild->BeginPipeline(ePipelineName::SDFPageDebug);
		gPSOBuild->SetShader(eShaderName::SDFPageDebug, {});
		gPSOBuild->AddBuffer(0, 0, eShaderType::Compute, &mIndirection, 0, VK_WHOLE_SIZE);
		gPSOBuild->AddImage(1, 0, eShaderType::Compute, mpAtlas, linear_sampler);
		gPSOBuild->AddImageFromTarget(2, 0, eShaderType::Compute, depth, point_sampler);
		gPSOBuild->AddImageFromTarget(3, 0, eShaderType::Compute, normals, point_sampler);
		gPSOBuild->AddImageFromTarget(4, 0, eShaderType::Compute, motion, point_sampler);
		gPSOBuild->AddStorageImage(5, 0, eShaderType::Compute, &lit->GetImage());
		gPSOBuild->AddBuffer(7, 0, eShaderType::Compute, &mHitRecords, 0, VK_WHOLE_SIZE);
		gPSOBuild->AddBuffer(8, 0, eShaderType::Compute, &mRefs, 0, VK_WHOLE_SIZE);
		gPSOBuild->AddBuffer(9, 0, eShaderType::Compute, &mAtlasFirstRef, 0, VK_WHOLE_SIZE);
		gPSOBuild->AddImage(10, 0, eShaderType::Compute, mpNearest, point_sampler);
		gPSOBuild->AddBuffer(11, 0, eShaderType::Compute, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
							 gGraphics->LightBuffer.PageSize);
		gPSOBuild->AddImageFromTarget(12, 0, eShaderType::Compute, gShadowAtlas->GetTarget(),
									  gSamplerCache->Request({
										  eSamplerFilter::Linear,
										  eSamplerFilter::Linear,
										  eSamplerFilter::Linear,
										  eSamplerAddressMode::ClampToBorder,
										  eSamplerBorderColor::FloatWhite,
										  eSamplerCompareOp::Greater,
									  }));
		gPSOBuild->AddImage(13, 0, eShaderType::Compute, gGraphics->pFogShadowAtlas,
							gSamplerCache->Request({
								eSamplerFilter::Linear,
								eSamplerFilter::Linear,
								eSamplerFilter::Nearest,
								eSamplerAddressMode::ClampToEdge,
								eSamplerBorderColor::FloatWhite,
								eSamplerCompareOp::LessOrEqual,
							}));
		gPSOBuild->AddBuffer(14, 0, eShaderType::Compute, &gGraphics->FogShadowPageBuffer, 0,
							 gGraphics->FogShadowPageBufferSize);

		gPSOBuild->AddFrameData(1);

		gPSOBuild->EndPipeline();
	}
}

void SDFPageScene::Destroy()
{
	mRefs.Destroy();
	mTriangles.Destroy();
	mHitRecords.Destroy();
	mIndirection.Destroy();
	mJobs.Destroy();
	mAtlasFirstRef.Destroy();

	mbActive = false;
}

bool SDFPageScene::Upload(const CompiledMapSDFPages& pages)
{
	if (pages.IsEmpty()) {
		Clear();
		return false;
	}

	gGraphics->GetDevice()->WaitForIdle();

	CreateBuffer(mRefs, pages.Refs.GetSizeInBytes(), pages.Refs.pData);
	CreateBuffer(mTriangles, pages.Triangles.GetSizeInBytes(), pages.Triangles.pData);

	{
		SizedArray<SDFPageHitRecordGpu> records;
		records.InitSize(pages.Triangles.Size);

		for (uint32 index = 0; index < pages.Triangles.Size; index++) {
			records[index] = MakeHitRecord(pages.Triangles[index]);
		}

		CreateBuffer(mHitRecords, records.GetSizeInBytes(), records.pData);
	}

	const RawGpuBuffer* buffers[] = { &mRefs, &mTriangles, &mHitRecords };

	gDescriptorCache->RebuildUsing(Slice<const RawGpuBuffer* const>(buffers, std::size(buffers)),
								   Slice<Image* const>(nullptr, 0));

	mSDFPages.Free();

	mSDFPages.InitSize(pages.SDFPages.Size);
	MemoryCopyRaw(mSDFPages.pData, pages.SDFPages.pData, pages.SDFPages.GetSizeInBytes());

	mLevelCount = std::min(static_cast<uint32>(pages.Levels.Size), scMaxLevels);
	mMarginScale = pages.MarginScale;

	for (uint32 level_index = 0; level_index < mLevelCount; level_index++) {
		const CompiledMapSDFPageLevel& source = pages.Levels[level_index];
		Level& level = mLevels[level_index];

		level.SDFPageSize = source.SDFPageSize;
		level.FirstSDFPage = source.FirstSDFPage;
		level.SDFPageCount = source.SDFPageCount;
		level.bPlaced = false;
	}

	mFreeAtlas.Clear();

	for (uint32 atlas = scAtlasSDFPageCount; atlas > 0; atlas--) {
		mFreeAtlas.Insert(atlas - 1);
	}

	for (uint32 level_index = 0; level_index < scMaxLevels; level_index++) {
		Level& level = mLevels[level_index];

		for (uint32 slot = 0; slot < scSlotsPerLevel; slot++) {
			level.SlotAtlas[slot] = SDFPageID::scEmpty;
			level.SlotSDFPage[slot] = SDFPageID::scEmpty;
			level.SlotDirty[slot] = 0;
		}

		level.Pending.Clear();
		level.PendingRead = 0;
	}

	mDirty.Clear();
	mbActive = true;

	uint64 total_sdf_pages = 0;

	for (uint32 level_index = 0; level_index < mLevelCount; level_index++) {
		total_sdf_pages += mLevels[level_index].SDFPageCount;
	}

	LogInfo(LC_RENDER, "SDF Page Scene {} levels, {} pages, {} triangles ({:.1f} MB)", mLevelCount, total_sdf_pages,
			pages.Triangles.Size,
			static_cast<double>(pages.Triangles.GetSizeInBytes() + pages.Refs.GetSizeInBytes()) / (1024.0 * 1024.0));

	return true;
}

void SDFPageScene::Clear()
{
	if (!mbActive) {
		return;
	}

	gGraphics->GetDevice()->WaitForIdle();

	mbActive = false;
	mSDFPages.Free();
	mLevelCount = 0;
}

uint32 SDFPageScene::GetSlot(const int32 coord[3]) const
{
	constexpr uint32 scMask = scClipSDFPages - 1;

	const uint32 x = static_cast<uint32>(coord[0]) & scMask;
	const uint32 y = static_cast<uint32>(coord[1]) & scMask;
	const uint32 z = static_cast<uint32>(coord[2]) & scMask;

	return x + y * scClipSDFPages + z * scClipSDFPages * scClipSDFPages;
}

uint32 SDFPageScene::GetPendingCount() const
{
	uint32 count = 0;

	for (uint32 level_index = 0; level_index < mLevelCount; level_index++) {
		count += static_cast<uint32>(mLevels[level_index].Pending.Size) - mLevels[level_index].PendingRead;
	}

	return count;
}

void SDFPageScene::FindSDFPageRange(const Level& level, int32 min_x, int32 max_x, uint32& out_begin,
									uint32& out_end) const
{
	const CompiledMapSDFPage* first = mSDFPages.pData + level.FirstSDFPage;
	const CompiledMapSDFPage* last = first + level.SDFPageCount;

	const CompiledMapSDFPage* begin = std::lower_bound(first, last, min_x, [](const CompiledMapSDFPage& page, int32 x)
													   { return page.Coord[0] < x; });
	const CompiledMapSDFPage* end = std::lower_bound(begin, last, max_x, [](const CompiledMapSDFPage& page, int32 x)
													 { return page.Coord[0] < x; });

	out_begin = static_cast<uint32>(begin - mSDFPages.pData);
	out_end = static_cast<uint32>(end - mSDFPages.pData);
}

void SDFPageScene::MarkDirty(uint32 level_index, uint32 slot, uint8 value)
{
	Level& level = mLevels[level_index];

	if (level.SlotDirty[slot] == 0) {
		mDirty.Insert(SDFPageID::MakeID(level_index, slot));
	}

	level.SlotDirty[slot] = std::max(level.SlotDirty[slot], value);
}

void SDFPageScene::FreeSlot(Level& level, uint32 slot)
{
	if (level.SlotAtlas[slot].IsNotEmpty()) {
		mFreeAtlas.Insert(level.SlotAtlas[slot]);
		level.SlotAtlas[slot] = SDFPageID::scEmpty;
	}

	level.SlotSDFPage[slot] = SDFPageID::scEmpty;
}

void SDFPageScene::ResetLevel(uint32 level_index)
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
		if (mDirty[index].GetLevelIndex() != level_index) {
			mDirty[kept++] = mDirty[index];
		}
	}

	mDirty.Size = kept;
}

bool SDFPageScene::PlaceLevel(uint32 level_index, const FVec3& center)
{
	Level& level = mLevels[level_index];

	constexpr int32 scHalfClip = static_cast<int32>(scClipSDFPages / 2);

	const int32 new_origin[3] = {
		FloorDiv(center.X, level.SDFPageSize) - scHalfClip,
		FloorDiv(center.Y, level.SDFPageSize) - scHalfClip,
		FloorDiv(center.Z, level.SDFPageSize) - scHalfClip,
	};

	const bool was_placed = level.bPlaced;
	bool reset = !was_placed;

	if (was_placed && new_origin[0] == level.Origin[0] && new_origin[1] == level.Origin[1] &&
		new_origin[2] == level.Origin[2]) {
		return false;
	}

	for (uint32 axis = 0; axis < 3; axis++) {
		if (std::abs(new_origin[axis] - level.Origin[axis]) >= static_cast<int32>(scClipSDFPages)) {
			reset = true;
		}
	}

	const int32 old_origin[3] = { level.Origin[0], level.Origin[1], level.Origin[2] };
	const int32 clip = static_cast<int32>(scClipSDFPages);

	const auto IsInside = [clip](const int32 coord[3], const int32 origin[3])
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
		FindSDFPageRange(level, old_origin[0], old_origin[0] + clip, begin, end);

		for (uint32 page = begin; page < end; page++) {
			const int32* coord = mSDFPages[page].Coord;

			if (IsInside(coord, old_origin) && !IsInside(coord, new_origin)) {
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
	FindSDFPageRange(level, new_origin[0], new_origin[0] + clip, begin, end);

	for (uint32 page = begin; page < end; page++) {
		const int32* coord = mSDFPages[page].Coord;

		if (!IsInside(coord, new_origin) || (!reset && IsInside(coord, old_origin))) {
			continue;
		}

		const uint32 slot = GetSlot(coord);

		level.SlotSDFPage[slot] = page;
		level.Pending.Insert(slot);
		MarkDirty(level_index, slot, scDirtyIndirection);
	}

	return reset;
}

void SDFPageScene::GetSceneConsts(SDFPageSceneGpu& out_consts) const
{
	out_consts = SDFPageSceneGpu {};

	for (uint32 level_index = 0; level_index < mLevelCount; level_index++) {
		const Level& level = mLevels[level_index];
		SDFPageLevelGpu& gpu = out_consts.Levels[level_index];

		gpu.Origin[0] = level.Origin[0];
		gpu.Origin[1] = level.Origin[1];
		gpu.Origin[2] = level.Origin[2];

		gpu.Params[0] = level.SDFPageSize;
		gpu.Params[1] = 1.0f / level.SDFPageSize;
		gpu.Params[2] = level.SDFPageSize * mMarginScale;
		gpu.Params[3] = level.SDFPageSize / static_cast<float32>(scTexelsPerSDFPage);
	}

	out_consts.Info[0] = mLevelCount;
	out_consts.Info[1] = scAtlasSDFPagesX;
	out_consts.Info[2] = scAtlasSDFPagesY;
	out_consts.Info[3] = scAtlasSDFPagesZ;

	out_consts.InvAtlasSize[0] = 1.0f / static_cast<float32>(scAtlasSDFPagesX * scTexelsPerSDFPage);
	out_consts.InvAtlasSize[1] = 1.0f / static_cast<float32>(scAtlasSDFPagesY * scTexelsPerSDFPage);
	out_consts.InvAtlasSize[2] = 1.0f / static_cast<float32>(scAtlasSDFPagesZ * scTexelsPerSDFPage);
}

void SDFPageScene::Update(CommandBuffer& cmd, const FVec3& center, uint32 eval_budget)
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

	// Coarse levels first so the whole view is covered before the finer detail cascades
	uint32 evaluated = 0;

	for (uint32 level_index = mLevelCount; level_index > 0 && evaluated < eval_budget; level_index--) {
		Level& level = mLevels[level_index - 1];

		while (level.PendingRead < level.Pending.Size && evaluated < eval_budget && !mFreeAtlas.IsEmpty()) {
			const uint32 slot = level.Pending[level.PendingRead++];

			if (level.SlotSDFPage[slot].IsEmpty() || level.SlotAtlas[slot].IsNotEmpty()) {
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

	for (const SDFPageID page_id : mDirty) {
		const uint32 slot = page_id.GetSlot();
		const uint32 level_index = page_id.GetLevelIndex();

		// Whatever does not fit waits for the next frame
		if (job_count == scMaxJobsPerFrame) {
			mDirty[kept++] = page_id.GetValue();
			continue;
		}

		Level& level = mLevels[level_index];
		Job& job = jobs[job_count++];

		job = Job {};
		job.Slot = level_index * scSlotsPerLevel + slot;

		if (level.SlotAtlas[slot].IsNotEmpty()) {
			job.Atlas = level.SlotAtlas[slot];

			if (level.SlotDirty[slot] == scDirtyEval) {
				const CompiledMapSDFPage& page = mSDFPages[level.SlotSDFPage[slot].GetSlot()];

				job.FirstRef = page.FirstRef;
				job.RefCount = page.RefCount;

				job.OriginAndTexel[0] = static_cast<float32>(page.Coord[0]) * level.SDFPageSize;
				job.OriginAndTexel[1] = static_cast<float32>(page.Coord[1]) * level.SDFPageSize;
				job.OriginAndTexel[2] = static_cast<float32>(page.Coord[2]) * level.SDFPageSize;
				job.OriginAndTexel[3] = level.SDFPageSize / static_cast<float32>(scTexelsPerSDFPage);
			}
		}
		else if (level.SlotSDFPage[slot].IsNotEmpty()) {
			job.Atlas = SDFPageID::scPending;
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
							static_cast<VkDeviceSize>(scSlotsPerLevel) * sizeof(uint32), SDFPageID::scEmpty.GetValue());
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
	AtlasTransition(cmd, mpNearest, atlas_from, VK_IMAGE_LAYOUT_GENERAL);
	mbAtlasInitialized = true;

	SDFPageUpdateConsts consts {
		.Info = { job_count, scAtlasSDFPagesX, scAtlasSDFPagesY, scAtlasSDFPagesZ },
		.Params = { mMarginScale, 0.0f, 0.0f, 0.0f },
	};

	gPipelineCache->AddBufferOffset(0, frame * scMaxJobsPerFrame * sizeof(Job));
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, 0);

	gPipelineCache->SetFrameData(gGraphics->FrameAlloc.Push(consts));
	gPipelineCache->Bind(ePipelineName::SDFPageUpdate, cmd);

	const uint32 columns = std::min(job_count, scUpdateGroupColumns);
	const uint32 rows = (job_count + scUpdateGroupColumns - 1) / scUpdateGroupColumns;

	vkCmdDispatch(cmd.Get(), columns, rows, 1);

	AtlasTransition(cmd, mpAtlas, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	AtlasTransition(cmd, mpNearest, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	ComputeBarrier(cmd);
}

void SDFPageScene::RenderDebug(CommandBuffer& cmd, const BaseCamera& camera, eDebugView view, float32 cone_slope,
							   float32 ray_length, uint32 probe_stride)
{
	if (!mbActive || !mbAtlasInitialized || view == eDebugView::None) {
		return;
	}

	const uint32 frame = gGraphics->GetFrameNumber();

	SDFPageDebugConsts consts;
	GetSceneConsts(consts.Scene);

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
	consts.Info[3] = gGraphics->GetElapsedFrameCount();

	consts.Params[0] = cone_slope;
	consts.Params[1] = ray_length;
	consts.Params[2] = gGraphics->PreExposure;

	gGraphics->SelectShadeLights(eye, ray_length, consts.LightIndices, consts.Lights[0]);
	consts.Lights[2] = std::max(probe_stride, 1U);

	if (gGraphics->IsBakedSunShadowActive()) {
		consts.Lights[1] |= scShadeFlagBakedSun;
		consts.BakedSun = gGraphics->GetBakedSunShadowGpu();
	}

	Target* lit_target = gGraphics->pRenderer->ForwardPass.GetTarget(eImageFormat::RGBA16_Float);
	Assert(lit_target != nullptr);

	Image* lit_image = &lit_target->GetImage();

	{
		ComputeBarrier(cmd);
		BarrierHelper::ImageBeginComputeWrite(cmd, lit_image);

		gPipelineCache->AddBufferOffset(0, 0);
		gPipelineCache->AddBufferOffset(0, 0);
		gPipelineCache->AddBufferOffset(0, 0);
		gPipelineCache->AddBufferOffset(0, 0);
		gPipelineCache->AddBufferOffset(0, gGraphics->LightBuffer.GetBaseOffset());
		gPipelineCache->AddBufferOffset(0, 0);

		gPipelineCache->SetFrameData(gGraphics->FrameAlloc.Push(consts));
		gPipelineCache->Bind(ePipelineName::SDFPageDebug, cmd);

		vkCmdDispatch(cmd.Get(), (extent.X + scDebugGroupSize - 1) / scDebugGroupSize,
					  (extent.Y + scDebugGroupSize - 1) / scDebugGroupSize, 1);

		BarrierHelper::ImageComputeWriteToRead(cmd, lit_image);
	}
}

} // namespace fx::renderer
