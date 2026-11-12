/*
 * File:        SDFPageScene.hpp
 * Author:      emd22
 * Created:     08/11/2026
 * Description: The SDF that the GI traces against
 */

#pragma once

#include "Backend/GpuBuffer.hpp"

#include <Core/DynArray.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Map/CompiledMap.hpp>
#include <Math/FVec3.hpp>

namespace fx {
class Image;
class BaseCamera;
} // namespace fx

namespace fx::renderer {

class CommandBuffer;
struct Target;

struct SDFPageLevelGpu
{
	int32 Origin[4] = {};
	/// Page size, its reciprocal, the distances margin, and the texel size
	float32 Params[4] = {};
};

/// What a ray's hit needs of a triangle. Laid out to match SDFPageHitRecord in Shaders/SDFPageCommon.hlsli.
struct SDFPageHitRecordGpu
{
	/// Unit face normal, octahedral (Z at the centre), as two 16 bit snorms
	uint32 Normal = 0;
	/// dot(normal, the triangle's corners)
	float32 Plane = 0.0f;
	/// Linear albedo as RGB8, and scHasFace in bit 24
	uint32 Albedo = 0;
	uint32 Padding = 0;

	static constexpr uint32 scHasFace = 0x01000000u;
};

static_assert(sizeof(SDFPageHitRecordGpu) == 16);

/// Laid out to match SDFPageScene in Shaders/SDFPageCommon.hlsli
struct SDFPageSceneGpu
{
	static constexpr uint32 scMaxLevels = 5;

	SDFPageLevelGpu Levels[scMaxLevels];
	/// Level count, then the atlas size in pages
	uint32 Info[4] = {};
	float32 InvAtlasSize[4] = {};
};

struct SDFPageCheckStats
{
	uint32 Match = 0;
	uint32 Early = 0;
	uint32 Late = 0;
	uint32 Sky = 0;
	uint32 Skipped = 0;
};


struct SDFPageID
{
public:
	static const SDFPageID scEmpty;
	static const SDFPageID scPending;

public:
	constexpr SDFPageID(const uint32 value) : ID(value) {}
	constexpr SDFPageID(const SDFPageID& other) : ID(other.ID) {}

	constexpr static SDFPageID MakeID(uint32 level_index, uint32 slot) { return ((level_index << 24) | slot); }

	SDFPageID& operator=(const SDFPageID& other)
	{
		ID = other.ID;
		return *this;
	};

	bool IsEmpty() const { return (ID == 0xFFFFFFFF); }
	bool IsNotEmpty() const { return (ID != 0xFFFFFFFF); }

	bool IsPending() const { return (ID == 0xFFFFFFFE); }
	bool IsNotPending() const { return (ID != 0xFFFFFFFE); }

	uint32 GetLevelIndex() const { return (ID >> 24); }
	uint32 GetSlot() const { return (ID & 0x00FFFFFF); }

	uint32 GetValue() const { return ID; }

	~SDFPageID() = default;

public:
	uint32 ID = 0;
};

inline const SDFPageID SDFPageID::scEmpty = SDFPageID(0xFFFFFFFF);
inline const SDFPageID SDFPageID::scPending = SDFPageID(0xFFFFFFFE);


class SDFPageScene
{
public:
	static constexpr uint32 scClipSDFPages = 64;
	static constexpr uint32 scSlotsPerLevel = scClipSDFPages * scClipSDFPages * scClipSDFPages;
	static constexpr uint32 scMaxLevels = SDFPageSceneGpu::scMaxLevels;
	static constexpr uint32 scTexelsPerSDFPage = 8;

	static constexpr uint32 scAtlasSDFPagesX = 64;
	static constexpr uint32 scAtlasSDFPagesY = 32;
	static constexpr uint32 scAtlasSDFPagesZ = 32;
	static constexpr uint32 scAtlasSDFPageCount = scAtlasSDFPagesX * scAtlasSDFPagesY * scAtlasSDFPagesZ;

	// SDF_PAGE_ATLAS_SHIFT_X/Y in Shaders/SDFPageCommon.hlsli
	static_assert(scAtlasSDFPagesX == 64 && scAtlasSDFPagesY == 32);

	static constexpr uint32 scMaxJobsPerFrame = 65536;

	enum class eDebugView : uint32
	{
		None,
		Normals,
		Steps,
		GiRays,
		DepthCheck,
		Motion,
		Roughness,
		Levels,
		GiSteps,
		ProbeRays,
		ProbeShade,
		HitTriangle,
		Count,
	};

public:
	/// Makes the fixed size buffers and the atlas, and placeholders for the per map ones
	void Create();
	void CreatePipelines(Target* depth, Target* normals, Target* motion, Target* lit);
	void Destroy();

	bool Upload(const CompiledMapSDFPages& pages);
	void Clear();

	/**
	 * @brief Moves the clip windows to `center` and records the pass that fills the pages that came into view
	 * @param eval_budget The most pages to compute distances for this frame
	 */
	void Update(CommandBuffer& cmd, const FVec3& center, uint32 eval_budget);

	/// Traces from the camera (or from the prepass surfaces) and overwrites the lit target with the result
	void RenderDebug(CommandBuffer& cmd, const BaseCamera& camera, eDebugView view, float32 cone_slope,
					 float32 ray_length, uint32 probe_stride);

	/// Mirrors SHADE_MAX_LIGHTS in Shaders/SDFPageShade.hlsli
	static constexpr uint32 scShadeMaxLights = 16;

	bool IsActive() const { return mbActive; }

	/// True once some pages have distances to trace against
	bool IsTraceable() const { return mbActive && mbAtlasInitialized; }

	RawGpuBuffer* GetIndirectionBuffer() { return &mIndirection; }
	RawGpuBuffer* GetTriangleBuffer() { return &mTriangles; }
	/// One SDFPageHitRecordGpu per triangle, what the traces read at a hit
	RawGpuBuffer* GetHitRecordBuffer() { return &mHitRecords; }
	RawGpuBuffer* GetRefBuffer() { return &mRefs; }
	RawGpuBuffer* GetAtlasFirstRefBuffer() { return &mAtlasFirstRef; }
	Image* GetAtlas() { return mpAtlas; }
	Image* GetNearest() { return mpNearest; }

	void GetSceneConsts(SDFPageSceneGpu& out_consts) const;

	uint32 GetResidentCount() const { return scAtlasSDFPageCount - static_cast<uint32>(mFreeAtlas.Size); }
	uint32 GetPendingCount() const;
	const SDFPageCheckStats& GetCheckStats() const { return mCheckStats; }

private:
	struct Level
	{
		float32 SDFPageSize = 0.0f;
		uint32 FirstSDFPage = 0;
		uint32 SDFPageCount = 0;

		int32 Origin[3] = {};
		bool bPlaced = false;

		/// Atlas page of every slot
		SizedArray<SDFPageID> SlotAtlas;

		/// The compiled page the slot should hold
		SizedArray<SDFPageID> SlotSDFPage;

		SizedArray<uint8> SlotDirty;

		/// Slots waiting for an atlas page, read from `PendingRead` on
		DynArray<uint32, GrowthFunctions::Double> Pending;
		uint32 PendingRead = 0;
	};

	struct Job
	{
		uint32 Slot = 0;
		SDFPageID Atlas = SDFPageID::scEmpty;
		uint32 FirstRef = 0;
		uint32 RefCount = 0;
		float32 OriginAndTexel[4] = {};
	};

	void CreateBuffer(RawGpuBuffer& buffer, uint64 size, const void* data);

	/// Moves a level's window, returning true if it moved too far to keep any of its pages
	bool PlaceLevel(uint32 level_index, const FVec3& center);
	void ResetLevel(uint32 level_index);
	void MarkDirty(uint32 level_index, uint32 slot, uint8 value);
	void FreeSlot(Level& level, uint32 slot);

	/// The range of a level's pages whose X coordinate is in [min_x, max_x)
	void FindSDFPageRange(const Level& level, int32 min_x, int32 max_x, uint32& out_begin, uint32& out_end) const;

	uint32 GetSlot(const int32 coord[3]) const;

private:
	RawGpuBuffer mRefs;
	RawGpuBuffer mTriangles;
	RawGpuBuffer mHitRecords;
	RawGpuBuffer mIndirection;
	RawGpuBuffer mJobs;

	Image* mpAtlas = nullptr;
	/// Index into the page's refs of the triangle nearest each block of SDFPageNearestBlock texels a side
	Image* mpNearest = nullptr;
	/// First ref of the page in each atlas slot, to find its triangles from the nearest index
	RawGpuBuffer mAtlasFirstRef;
	bool mbAtlasInitialized = false;

	SizedArray<SDFPageID> mFreeAtlas;

	/// CPU copy of the compiled pages, to find the ones that come into view
	SizedArray<CompiledMapSDFPage> mSDFPages;

	Level mLevels[scMaxLevels];
	uint32 mLevelCount = 0;
	float32 mMarginScale = 0.5f;

	DynArray<SDFPageID, GrowthFunctions::Double> mDirty;

	SDFPageCheckStats mCheckStats;
	uint32 mDebugFrame = 0;

	bool mbActive = false;
};

} // namespace fx::renderer
