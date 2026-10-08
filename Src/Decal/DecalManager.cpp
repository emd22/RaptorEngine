#include "DecalManager.hpp"

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Core/Random.hpp>
#include <Engine.hpp>
#include <Math/Frustum.hpp>
#include <Math/MathUtil.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/Limits.hpp>
#include <Renderer/TiledForwardRenderer.hpp>
#include <algorithm>
#include <cmath>

namespace fx {

FX_SET_MODULE_NAME("DecalManager")

/// Uniform random value in [0, 1)
static float32 RandomUnit() { return static_cast<float32>(FastRand32() >> 8) * (1.0f / 16777216.0f); }

static void StoreFloat3(const Vec3f value, float32* out)
{
	out[0] = value.X;
	out[1] = value.Y;
	out[2] = value.Z;
}

void DecalManager::Create()
{
	mDecals.InitCapacity(Limits::MaxDecals);
	mVisibleSlots.InitCapacity(Limits::MaxDecals);
	mSkinnedDecals.InitCapacity(Limits::MaxSkinnedDecals);

	const String atlas_path = LoadAtlasConfig();

	mAtlasTicket = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_SRGB, atlas_path.CStr(),
											eImageCreateFlags::None);

	mNormalAtlasTicket = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm, scNormalAtlasPath,
												  eImageCreateFlags::None);

	// Runs on the asset thread, the renderer picks the atlases up in Update()
	mAtlasTicket.OnLoaded([this](void* data) { mpAtlas.store(static_cast<Image*>(data)); });
	mNormalAtlasTicket.OnLoaded([this](void* data) { mpNormalAtlas.store(static_cast<Image*>(data)); });
}

static bool ReadIntPair(const ConfigEntry* entry, float32* out)
{
	if (entry == nullptr || entry->GetArrayData().Size() < 2) {
		return false;
	}

	out[0] = entry->GetArrayData()[0].Get<float32>();
	out[1] = entry->GetArrayData()[1].Get<float32>();
	return true;
}

String DecalManager::LoadAtlasConfig()
{
	ConfigFile config;
	config.Load(scAtlasConfigPath);

	String atlas_path = scAtlasDirectory;

	const ConfigEntry* image_entry = config.GetEntry(HashStr32("image"));
	atlas_path += (image_entry != nullptr) ? image_entry->GetValue<const char*>() : scDefaultAtlasImage;

	const float32 tile_size = config.GetEntryValue<float32>(HashStr32("tile_size"), 0.0f);
	const ConfigEntry* groups = config.GetEntry(HashStr32("groups"));

	float32 atlas_size[2];
	if (!ReadIntPair(config.GetEntry(HashStr32("atlas_size")), atlas_size) || tile_size <= 0.0f || groups == nullptr) {
		LogError(LC_ASSET, "Could not read the decal atlas layout from {}", scAtlasConfigPath);
		return atlas_path;
	}

	const auto read_region = [&](const char* name, AtlasRegion& region) {
		const ConfigEntry* group = groups->GetMember(HashStr32(name));

		float32 tile[2];
		float32 tiles[2];
		if (group == nullptr || !ReadIntPair(group->GetMember(HashStr32("tile")), tile) ||
			!ReadIntPair(group->GetMember(HashStr32("tiles")), tiles)) {
			LogError(LC_ASSET, "Decal atlas {} has no group '{}'", scAtlasConfigPath, name);
			return;
		}

		region.ScaleU = (tiles[0] * tile_size) / atlas_size[0];
		region.ScaleV = (tiles[1] * tile_size) / atlas_size[1];
		region.OffsetU = (tile[0] * tile_size) / atlas_size[0];
		region.OffsetV = (tile[1] * tile_size) / atlas_size[1];
	};

	read_region(scBulletHoleGroup, mBulletHoleRegion);
	read_region(scBloodGroup, mBloodRegion);

	return atlas_path;
}

DecalManager::DecalEntry DecalManager::BuildEntry(const DecalDesc& desc) const
{
	const Vec3f forward = desc.Direction.Normalize();

	// Anything that isn't parallel to the projection works as a reference for the decal's up axis
	const Vec3f reference = (std::abs(forward.Y) < 0.99f) ? Vec3f::sUp : Vec3f::sForward;

	const Vec3f unrolled_right = reference.Cross(forward).Normalize();
	const Vec3f unrolled_up = forward.Cross(unrolled_right);

	float32 roll_sin, roll_cos;
	MathUtil::SinCos(desc.Roll, &roll_sin, &roll_cos);

	const Vec3f right = (unrolled_right * roll_cos) + (unrolled_up * roll_sin);
	const Vec3f up = (unrolled_up * roll_cos) - (unrolled_right * roll_sin);

	DecalEntry entry {};
	renderer::DecalGpuData& gpu_data = entry.GpuData;

	const Vec3f scaled_axes[3] = {
		right * (1.0f / desc.Width),
		up * (1.0f / desc.Height),
		forward * (1.0f / desc.Depth),
	};

	for (uint32 axis = 0; axis < 3; axis++) {
		const Vec3f scaled_axis = scaled_axes[axis];

		gpu_data.WorldToDecal[(0 * 4) + axis] = scaled_axis.X;
		gpu_data.WorldToDecal[(1 * 4) + axis] = scaled_axis.Y;
		gpu_data.WorldToDecal[(2 * 4) + axis] = scaled_axis.Z;
		gpu_data.WorldToDecal[(3 * 4) + axis] = -scaled_axis.Dot(desc.Position);

		gpu_data.WorldToDecal[(axis * 4) + 3] = 0.0f;
	}

	gpu_data.WorldToDecal[15] = 1.0f;

	StoreFloat3(desc.Position, gpu_data.Center);

	StoreFloat3(right, gpu_data.AxisX);
	StoreFloat3(up, gpu_data.AxisY);
	StoreFloat3(forward, gpu_data.AxisZ);

	gpu_data.HalfExtents[0] = desc.Width * 0.5f;
	gpu_data.HalfExtents[1] = desc.Height * 0.5f;
	gpu_data.HalfExtents[2] = desc.Depth * 0.5f;
	gpu_data.HalfExtents[3] = 0.0f;

	gpu_data.Color = desc.Color;
	gpu_data.Roughness = desc.Roughness;
	gpu_data.RoughnessWeight = desc.RoughnessWeight;
	gpu_data.NormalStrength = desc.NormalStrength;

	memcpy(gpu_data.AtlasRect, desc.AtlasRect, sizeof(gpu_data.AtlasRect));

	entry.BoundsRadius = 0.5f *
						 std::sqrt((desc.Width * desc.Width) + (desc.Height * desc.Height) + (desc.Depth * desc.Depth));

	return entry;
}

void DecalManager::AddDecal(const DecalDesc& desc)
{
	const DecalEntry entry = BuildEntry(desc);

	ReplaceCoveredDecals(desc, Vec3f(entry.GpuData.AxisZ));

	if (mDecals.Size < mDecals.Capacity) {
		mDecals.Insert(entry);
		mNextSlot = static_cast<uint32>(mDecals.Size % mDecals.Capacity);
		return;
	}

	// Full, replace the oldest
	mDecals[mNextSlot] = entry;
	mNextSlot = static_cast<uint32>((mNextSlot + 1) % mDecals.Capacity);
}

void DecalManager::ReplaceCoveredDecals(const DecalDesc& desc, const Vec3f forward)
{
	const float32 replace_distance = scReplaceDistanceFraction * std::min(desc.Width, desc.Height);
	const float32 replace_distance_sq = replace_distance * replace_distance;

	const float32 max_half_width = desc.Width * 0.5f * scReplaceMaxSizeRatio;
	const float32 max_half_height = desc.Height * 0.5f * scReplaceMaxSizeRatio;

	for (DecalEntry& other : mDecals) {
		if (other.bReplaced) {
			continue;
		}

		const renderer::DecalGpuData& other_data = other.GpuData;

		if (other_data.HalfExtents[0] > max_half_width || other_data.HalfExtents[1] > max_half_height) {
			continue;
		}

		const Vec3f offset = Vec3f(other_data.Center) - desc.Position;

		if (offset.Dot(offset) > replace_distance_sq) {
			continue;
		}

		if (Vec3f(other_data.AxisZ).Dot(forward) < scReplaceMinFacing) {
			continue;
		}

		other.bReplaced = true;
	}
}

void DecalManager::AddBulletHole(const Vec3f hit_point, const Vec3f hit_normal)
{
	constexpr uint32 variant_count = scBulletHoleAtlasColumns * scBulletHoleAtlasRows;
	const float32 cell_width = mBulletHoleRegion.ScaleU / static_cast<float32>(scBulletHoleAtlasColumns);
	const float32 cell_height = mBulletHoleRegion.ScaleV / static_cast<float32>(scBulletHoleAtlasRows);

	const uint32 variant = FastRand32() % variant_count;
	const float32 size = scBulletHoleSize * (1.0f + (0.15f * RandomUnit()));

	uint32 rect_column = variant % scBulletHoleAtlasColumns;
	uint32 rect_row = variant / scBulletHoleAtlasColumns;

	const DecalDesc desc {
		.Position = hit_point,
		.Direction = -hit_normal,
		.Width = size,
		.Height = size,
		.Depth = scBulletHoleDepth,
		.Roll = RandomUnit() * 2.0f * static_cast<float32>(M_PI),
		.AtlasRect = {
			cell_width,
			cell_height,
			mBulletHoleRegion.OffsetU + (static_cast<float32>(rect_column) * cell_width),
			mBulletHoleRegion.OffsetV + (static_cast<float32>(rect_row) * cell_height),
		},
		.Roughness = 0.4f,
		.RoughnessWeight = 1.0f,
		.NormalStrength = scBulletHoleNormalStrength,
	};

	AddDecal(desc);
}

void DecalManager::AddBloodSplat(const Vec3f hit_point, const Vec3f hit_normal, float32 size)
{
	const float32 width = size * (0.8f + (0.4f * RandomUnit()));
	const float32 height = width * (0.85f + (0.3f * RandomUnit()));

	const float32 shade = 0.65f + (0.35f * RandomUnit());
	const uint32 shade_byte = static_cast<uint32>(shade * 255.0f);

	const DecalDesc desc {
		.Position = hit_point,
		.Direction = -hit_normal,
		.Width = width,
		.Height = height,
		.Depth = scBloodDepth,
		.Roll = RandomUnit() * 2.0f * static_cast<float32>(M_PI),
		.AtlasRect = { mBloodRegion.ScaleU, mBloodRegion.ScaleV, mBloodRegion.OffsetU, mBloodRegion.OffsetV },
		.Color = shade_byte | (shade_byte << 8) | (shade_byte << 16) | 0xFF000000u,
		.Roughness = 0.2f,
		.RoughnessWeight = 0.85f,
		.NormalStrength = 0.0f,
	};

	AddDecal(desc);
}

void DecalManager::AddSkinnedDecal(const Skeleton* skeleton, const Mat4f& rest_to_world, const DecalDesc& desc)
{
	if (skeleton == nullptr) {
		return;
	}

	SkinnedDecalEntry entry { .GpuData = BuildEntry(desc).GpuData, .pSkeleton = skeleton };
	renderer::DecalGpuData& gpu_data = entry.GpuData;

	const Mat4f rest_to_decal = rest_to_world * Mat4f::FromRows(gpu_data.WorldToDecal);
	memcpy(gpu_data.WorldToDecal, rest_to_decal.RawData, sizeof(gpu_data.WorldToDecal));

	float32* axes[3] = { gpu_data.AxisX, gpu_data.AxisY, gpu_data.AxisZ };

	for (uint32 axis = 0; axis < 3; axis++) {
		const Vec3f rest_axis(rest_to_decal.RawData[(0 * 4) + axis], rest_to_decal.RawData[(1 * 4) + axis],
							  rest_to_decal.RawData[(2 * 4) + axis]);

		StoreFloat3(rest_axis.Normalize(), axes[axis]);
	}

	gpu_data.NormalStrength = 0.0f;

	uint32 first = 0;
	uint32 count = 0;

	const auto find_run = [&]() {
		first = static_cast<uint32>(mSkinnedDecals.Size);
		count = 0;

		for (uint32 i = 0; i < mSkinnedDecals.Size; i++) {
			if (mSkinnedDecals[i].pSkeleton == skeleton) {
				first = std::min(first, i);
				count++;
			}
		}
	};

	const auto erase_at = [this](uint32 index) {
		for (uint32 i = index; i + 1 < mSkinnedDecals.Size; i++) {
			mSkinnedDecals[i] = mSkinnedDecals[i + 1];
		}
		mSkinnedDecals.Size--;
	};

	find_run();

	if (count >= Limits::MaxSkinnedDecalsPerSkeleton) {
		erase_at(first);
		find_run();
	}
	else if (mSkinnedDecals.Size >= mSkinnedDecals.Capacity) {
		erase_at(0);
		find_run();
	}

	const uint32 insert_at = first + count;

	mSkinnedDecals.Insert(entry);

	for (uint32 i = static_cast<uint32>(mSkinnedDecals.Size) - 1; i > insert_at; i--) {
		mSkinnedDecals[i] = mSkinnedDecals[i - 1];
	}

	mSkinnedDecals[insert_at] = entry;
}

void DecalManager::AddSkinnedBloodSplat(const Skeleton* skeleton, const Mat4f& rest_to_world, const Vec3f hit_point,
										const Vec3f direction, float32 size)
{
	const float32 width = size * (0.8f + (0.4f * RandomUnit()));
	const float32 height = width * (0.85f + (0.3f * RandomUnit()));

	const float32 shade = 0.65f + (0.35f * RandomUnit());
	const uint32 shade_byte = static_cast<uint32>(shade * 255.0f);

	const DecalDesc desc {
		.Position = hit_point,
		.Direction = direction,
		.Width = width,
		.Height = height,
		.Depth = scSkinnedBloodDepth,
		.Roll = RandomUnit() * 2.0f * static_cast<float32>(M_PI),
		.AtlasRect = { mBloodRegion.ScaleU, mBloodRegion.ScaleV, mBloodRegion.OffsetU, mBloodRegion.OffsetV },
		.Color = shade_byte | (shade_byte << 8) | (shade_byte << 16) | 0xFF000000u,
		.Roughness = 0.2f,
		.RoughnessWeight = 0.85f,
		.NormalStrength = 0.0f,
	};

	AddSkinnedDecal(skeleton, rest_to_world, desc);
}

void DecalManager::RemoveSkinnedDecals(const Skeleton* skeleton)
{
	uint32 kept = 0;

	for (uint32 i = 0; i < mSkinnedDecals.Size; i++) {
		if (mSkinnedDecals[i].pSkeleton != skeleton) {
			mSkinnedDecals[kept++] = mSkinnedDecals[i];
		}
	}

	mSkinnedDecals.Size = kept;
}

void DecalManager::GetSkinnedDecalRange(const Skeleton* skeleton, uint32& out_start, uint32& out_count) const
{
	out_start = 0;
	out_count = 0;

	for (uint32 i = 0; i < mSkinnedUploadedCount; i++) {
		if (mSkinnedDecals[i].pSkeleton != skeleton) {
			continue;
		}

		if (out_count == 0) {
			out_start = Limits::MaxVisibleDecals + i;
		}

		out_count++;
	}
}

void DecalManager::Clear()
{
	mDecals.Clear();
	mNextSlot = 0;

	mSkinnedDecals.Clear();
	mSkinnedUploadedCount = 0;
}

void DecalManager::Update(const PerspectiveCamera& camera)
{
	using namespace renderer;

	mVisibleSlots.Clear();
	mVisibleCount = 0;
	mSkinnedUploadedCount = 0;

	Image* atlas = mpAtlas.load();
	Image* normal_atlas = mpNormalAtlas.load();

	gGraphics->pRenderer->SetDecalAtlases(atlas, normal_atlas);

	// Until the atlas is in, the shader would sample the (opaque white) null image
	if (atlas == nullptr) {
		return;
	}

	Frustum frustum;
	frustum.Rebuild(camera);

	const uint32 decal_count = static_cast<uint32>(mDecals.Size);

	for (uint32 i = 0; i < decal_count; i++) {
		// Oldest first. Once the ring is full the oldest is at `mNextSlot`, before then `mNextSlot` equals the count.
		const uint32 slot = (mNextSlot + i) % decal_count;
		const DecalEntry& entry = mDecals[slot];

		if (!entry.bReplaced && frustum.IntersectsSphere(Vec3f(entry.GpuData.Center), entry.BoundsRadius)) {
			mVisibleSlots.Insert(slot);
		}
	}

	// Past the limit, the oldest visible decals are the ones left out
	const uint32 visible_count = static_cast<uint32>(mVisibleSlots.Size);
	const uint32 first_visible = (visible_count > Limits::MaxVisibleDecals) ? (visible_count - Limits::MaxVisibleDecals)
																			: 0;

	const uint32 page_offset = gGraphics->GetDecalFrameOffset();
	DecalGpuData* page = reinterpret_cast<DecalGpuData*>(static_cast<uint8*>(gGraphics->DecalBuffer.pMappedBuffer) +
														 page_offset);

	for (uint32 i = first_visible; i < visible_count; i++) {
		DecalGpuData& gpu_data = page[mVisibleCount++];
		gpu_data = mDecals[mVisibleSlots[i]].GpuData;

		// The blank image bound in place of a missing normal atlas isn't a flat normal
		if (normal_atlas == nullptr) {
			gpu_data.NormalStrength = 0.0f;
		}
	}

	if (mVisibleCount > 0) {
		gGraphics->DecalBuffer.FlushToGpu(page_offset, mVisibleCount * sizeof(DecalGpuData));
	}

	mSkinnedUploadedCount = static_cast<uint32>(mSkinnedDecals.Size);

	for (uint32 i = 0; i < mSkinnedUploadedCount; i++) {
		page[Limits::MaxVisibleDecals + i] = mSkinnedDecals[i].GpuData;
	}

	if (mSkinnedUploadedCount > 0) {
		gGraphics->DecalBuffer.FlushToGpu(page_offset + (Limits::MaxVisibleDecals * sizeof(DecalGpuData)),
										  mSkinnedUploadedCount * sizeof(DecalGpuData));
	}
}

} // namespace fx
