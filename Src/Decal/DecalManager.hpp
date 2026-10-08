#pragma once

#include <Asset/AssetTicket.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/Mat4.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/Backend/Pipeline.hpp>
#include <atomic>

namespace fx {

class Image;
class PerspectiveCamera;
class Skeleton;

/**
 * @brief A decal to project onto the world, see DecalManager::AddDecal().
 */
struct DecalDesc
{
	Vec3f Position = Vec3f::sZero;
	Vec3f Direction = Vec3f::sForward;

	/// Size of the projection in world units
	float32 Width = 0.25f;
	float32 Height = 0.25f;

	float32 Depth = 0.1f;

	/// Rotation around `Direction` in radians
	float32 Roll = 0.0f;

	float32 AtlasRect[4] = { 1.0f, 1.0f, 0.0f, 0.0f };

	/// RGBA8 tint with R in the low byte, the alpha scales the decal's opacity
	uint32 Color = 0xFFFFFFFF;

	float32 Roughness = 0.5f;
	float32 RoughnessWeight = 0.4f;

	/// How far the normal atlas bends the lighting, 0 for a flat decal
	float32 NormalStrength = 0.6f;
};

class DecalManager
{
public:
	static constexpr const char* scAtlasDirectory = "Textures/";
	static constexpr const char* scAtlasConfigPath = "Textures/decals.conf";
	static constexpr const char* scDefaultAtlasImage = "decals.png";
	static constexpr const char* scNormalAtlasPath = "Textures/decals_normal.png";

	static constexpr const char* scBulletHoleGroup = "bulletholes";
	static constexpr const char* scBloodGroup = "blood_splat";

	static constexpr uint32 scBulletHoleAtlasColumns = 4;
	static constexpr uint32 scBulletHoleAtlasRows = 4;

	/// Width and height of a bullet hole decal, before its random scale
	static constexpr float32 scBulletHoleSize = 0.20f;
	static constexpr float32 scBulletHoleDepth = 0.1f;
	static constexpr float32 scBulletHoleNormalStrength = 1.0f;

	static constexpr float32 scBloodDepth = 0.2f;

	static constexpr float32 scSkinnedBloodDepth = 0.6f;

	static constexpr float32 scReplaceDistanceFraction = 0.3f;
	static constexpr float32 scReplaceMaxSizeRatio = 1.25f;
	static constexpr float32 scReplaceMinFacing = 0.7f;

public:
	DecalManager() = default;

	void Create();

	void AddDecal(const DecalDesc& desc);
	void AddBulletHole(const Vec3f hit_point, const Vec3f hit_normal);

	void AddBloodSplat(const Vec3f hit_point, const Vec3f hit_normal, float32 size);

	void AddSkinnedDecal(const Skeleton* skeleton, const Mat4f& rest_to_world, const DecalDesc& desc);
	void AddSkinnedBloodSplat(const Skeleton* skeleton, const Mat4f& rest_to_world, const Vec3f hit_point,
							  const Vec3f direction, float32 size);
	void RemoveSkinnedDecals(const Skeleton* skeleton);

	void GetSkinnedDecalRange(const Skeleton* skeleton, uint32& out_start, uint32& out_count) const;

	void Clear();

	void Update(const PerspectiveCamera& camera);

	FX_FORCE_INLINE uint32 GetVisibleCount() const { return mVisibleCount; }
	FX_FORCE_INLINE uint32 GetCount() const { return mDecals.Size; }

private:
	struct AtlasRegion
	{
		float32 ScaleU = 1.0f;
		float32 ScaleV = 1.0f;
		float32 OffsetU = 0.0f;
		float32 OffsetV = 0.0f;
	};

	struct DecalEntry
	{
		renderer::DecalGpuData GpuData;
		/// Radius of the sphere around the decal's box, for frustum culling
		float32 BoundsRadius = 0.0f;
		bool bReplaced = false;
	};

	struct SkinnedDecalEntry
	{
		renderer::DecalGpuData GpuData;
		const Skeleton* pSkeleton = nullptr;
	};

private:
	String LoadAtlasConfig();
	DecalEntry BuildEntry(const DecalDesc& desc) const;
	void ReplaceCoveredDecals(const DecalDesc& desc, const Vec3f forward);

private:
	/// Ring buffer of decals
	SizedArray<DecalEntry> mDecals;
	uint32 mNextSlot = 0;

	/// Visible slots in `mDecals` for this frame, oldest first
	SizedArray<uint32> mVisibleSlots;
	uint32 mVisibleCount = 0;

	SizedArray<SkinnedDecalEntry> mSkinnedDecals;
	uint32 mSkinnedUploadedCount = 0;

	AssetTicket mAtlasTicket { nullptr };
	AssetTicket mNormalAtlasTicket { nullptr };

	AtlasRegion mBulletHoleRegion;
	AtlasRegion mBloodRegion;

	/// Set from the asset thread once each atlas is on the GPU
	std::atomic<Image*> mpAtlas = nullptr;
	std::atomic<Image*> mpNormalAtlas = nullptr;
};

} // namespace fx
