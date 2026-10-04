#pragma once

#include <Asset/AssetTicket.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/Backend/Pipeline.hpp>
#include <atomic>

namespace fx {

class Image;
class PerspectiveCamera;

enum class eDecalAtlas : uint32
{
	BulletHoles = 0,
	Blood = 1,
};

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

	eDecalAtlas Atlas = eDecalAtlas::BulletHoles;
};

class DecalManager
{
public:
	static constexpr const char* scBulletHoleAtlasPath = "Textures/bulletholes.png";
	/// Tangent space normals (green up) laid out the same as the atlas above. Optional, without it decals are flat.
	static constexpr const char* scBulletHoleNormalAtlasPath = "Textures/bulletholes_normal.png";
	static constexpr uint32 scBulletHoleAtlasColumns = 4;
	static constexpr uint32 scBulletHoleAtlasRows = 4;

	/// Width and height of a bullet hole decal, before its random scale
	static constexpr float32 scBulletHoleSize = 0.20f;
	static constexpr float32 scBulletHoleDepth = 0.1f;
	static constexpr float32 scBulletHoleNormalStrength = 1.0f;

	static constexpr const char* scBloodAtlasPath = "Textures/blood_splat.png";
	static constexpr float32 scBloodDepth = 0.2f;

public:
	DecalManager() = default;

	void Create();

	void AddDecal(const DecalDesc& desc);
	void AddBulletHole(const Vec3f& hit_point, const Vec3f& hit_normal);

	void AddBloodSplat(const Vec3f& hit_point, const Vec3f& hit_normal, float32 size);

	void Clear();

	void Update(const PerspectiveCamera& camera);

	FX_FORCE_INLINE uint32 GetVisibleCount() const { return mVisibleCount; }
	FX_FORCE_INLINE uint32 GetCount() const { return mDecals.Size; }

private:
	struct DecalEntry
	{
		renderer::DecalGpuData GpuData;
		/// Radius of the sphere around the decal's box, for frustum culling
		float32 BoundsRadius = 0.0f;
	};

private:
	/// Ring buffer of decals
	SizedArray<DecalEntry> mDecals;
	uint32 mNextSlot = 0;

	/// Visible slots in `mDecals` for this frame, oldest first
	SizedArray<uint32> mVisibleSlots;
	uint32 mVisibleCount = 0;

	AssetTicket mAtlasTicket { nullptr };
	AssetTicket mNormalAtlasTicket { nullptr };
	AssetTicket mBloodAtlasTicket { nullptr };

	/// Set from the asset thread once each atlas is on the GPU
	std::atomic<Image*> mpAtlas = nullptr;
	std::atomic<Image*> mpNormalAtlas = nullptr;
	std::atomic<Image*> mpBloodAtlas = nullptr;
};

} // namespace fx
