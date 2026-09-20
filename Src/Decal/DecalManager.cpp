#include "DecalManager.hpp"

#include <Asset/AssetManager.hpp>
#include <Core/Random.hpp>
#include <Engine.hpp>
#include <Math/Frustum.hpp>
#include <Math/MathUtil.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/Limits.hpp>
#include <Renderer/TiledForwardRenderer.hpp>
#include <cmath>

namespace fx {

FX_SET_MODULE_NAME("DecalManager")

/// Uniform random value in [0, 1)
static float32 RandomUnit() { return static_cast<float32>(FastRand32() >> 8) * (1.0f / 16777216.0f); }

static void StoreFloat3(const Vec3f& value, float32* out)
{
	out[0] = value.X;
	out[1] = value.Y;
	out[2] = value.Z;
}

void DecalManager::Create()
{
	mDecals.InitCapacity(Limits::MaxDecals);
	mVisibleSlots.InitCapacity(Limits::MaxDecals);

	mAtlasTicket = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm, scBulletHoleAtlasPath,
											eImageCreateFlags::None);

	mNormalAtlasTicket = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm,
												  scBulletHoleNormalAtlasPath, eImageCreateFlags::None);

	// Runs on the asset thread, the renderer picks the atlases up in Update()
	mAtlasTicket.OnLoaded([this](void* data) { mpAtlas.store(static_cast<Image*>(data)); });
	mNormalAtlasTicket.OnLoaded([this](void* data) { mpNormalAtlas.store(static_cast<Image*>(data)); });
}

void DecalManager::AddDecal(const DecalDesc& desc)
{
	const Vec3f forward = desc.Direction.Normalize();

	// Anything that isn't parallel to the projection works as a reference for the decal's up axis
	const Vec3f& reference = (std::abs(forward.Y) < 0.99f) ? Vec3f::sUp : Vec3f::sForward;

	const Vec3f unrolled_right = reference.Cross(forward).Normalize();
	const Vec3f unrolled_up = forward.Cross(unrolled_right);

	float32 roll_sin, roll_cos;
	MathUtil::SinCos(desc.Roll, &roll_sin, &roll_cos);

	const Vec3f right = (unrolled_right * roll_cos) + (unrolled_up * roll_sin);
	const Vec3f up = (unrolled_up * roll_cos) - (unrolled_right * roll_sin);

	DecalEntry entry {};
	renderer::DecalGpuData& gpu_data = entry.GpuData;

	// World to decal space, v * M. Column N takes a point to its distance from the center along axis N, measured in
	// box sizes, so the box ends up spanning [-0.5, 0.5].
	const Vec3f scaled_axes[3] = {
		right * (1.0f / desc.Width),
		up * (1.0f / desc.Height),
		forward * (1.0f / desc.Depth),
	};

	for (uint32 axis = 0; axis < 3; axis++) {
		const Vec3f& scaled_axis = scaled_axes[axis];

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

	if (mDecals.Size < mDecals.Capacity) {
		mDecals.Insert(entry);
		mNextSlot = static_cast<uint32>(mDecals.Size % mDecals.Capacity);
		return;
	}

	// Full, replace the oldest
	mDecals[mNextSlot] = entry;
	mNextSlot = static_cast<uint32>((mNextSlot + 1) % mDecals.Capacity);
}

void DecalManager::AddBulletHole(const Vec3f& hit_point, const Vec3f& hit_normal)
{
	constexpr uint32 variant_count = scBulletHoleAtlasColumns * scBulletHoleAtlasRows;
	constexpr float32 cell_width = 1.0f / static_cast<float32>(scBulletHoleAtlasColumns);
	constexpr float32 cell_height = 1.0f / static_cast<float32>(scBulletHoleAtlasRows);

	const uint32 variant = FastRand32() % variant_count;
	const float32 size = scBulletHoleSize * (0.85f + (0.15f * RandomUnit()));

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
			static_cast<float32>(rect_column) * cell_width,
			static_cast<float32>(rect_row) * cell_height,
		},
		.Roughness = 0.9f,
		.RoughnessWeight = 1.0f,
		.NormalStrength = scBulletHoleNormalStrength,
	};

	AddDecal(desc);
}

void DecalManager::Clear()
{
	mDecals.Clear();
	mNextSlot = 0;
}

void DecalManager::Update(const PerspectiveCamera& camera)
{
	using namespace renderer;

	mVisibleSlots.Clear();
	mVisibleCount = 0;

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

		if (frustum.IntersectsSphere(Vec3f(entry.GpuData.Center), entry.BoundsRadius)) {
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
}

} // namespace fx
