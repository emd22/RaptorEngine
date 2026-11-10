#pragma once

#include <Core/FreeArray.hpp>
#include <Core/PagedArray.hpp>
#include <Core/SizedArray.hpp>
#include <Map/AreaGraph.hpp>
#include <Math/BBox.hpp>
#include <Math/FVec3.hpp>
#include <Math/Vec2.hpp>
#include <Object/ObjectID.hpp>
#include <Renderer/LightID.hpp>
#include <unordered_set>

namespace fx {


using TileIndex = uint32;

static constexpr TileIndex TileIndexNull = UINT32_MAX;


struct Tile
{
public:
	uint32 FindObject(ObjectID id);
	uint32 FindLight(LightID id);

public:
	FreeArray<ObjectID> Objects;
	FreeArray<LightID> Lights;

	/// WorldGrid::GetChangeSerial() at the last time a shadow casting object in this tile was added, removed, moved,
	/// rotated, scaled or reposed. When this changes we need to rebuild the shadows for the lights that cross these
	/// tiles.
	uint64 ChangeSerial = 0;
};


class Object;
class LightBase;
class CompiledMap;

class WorldGrid
{
public:
	static constexpr uint32 scMaxObjectsPerTile = 256;
	static constexpr uint32 scMaxGlobalObjects = 64;
	static constexpr uint32 scMaxLightsPerTile = 64;
	static constexpr uint32 scMaxGlobalLights = 64;
	static constexpr uint32 scMaxLightTileCount = 64;

	static constexpr TileIndex scGlobalTileIndex = UINT32_MAX - 1;

public:
	WorldGrid() = default;

	void Create(const UVec2 grid_size);

	/**
	 * @brief Inserts an object into its respective tile.
	 * @returns The tile index that it was added to.
	 */
	void AddObject(ObjectID id);

	TileIndex WorldToTile(const FVec3 position) const;

	/**
	 * @brief Gets the rectangle of tiles (inclusive) that a sphere reaches.
	 */
	void GetSphereTileRange(const FVec3 center, float32 radius, UVec2* out_min, UVec2* out_max) const;
	TileIndex TileFromTileXY(const UVec2& xy) const;

	const SizedArray<ObjectID>& GetNearbyObjects();

	/**
	 * @brief Updates an object to a new tile if the object has moved into another tile boundary.
	 */
	void UpdateObject(Object* object, bool update_attached = true);

	/**
	 * @brief Remove an object from its assigned tile.
	 */
	void RemoveObject(ObjectID id);

	/**
	 * @brief Marks every tile a shadow casting object spans as changed. Does nothing for objects that are not shadow
	 * casters or are not in the grid.
	 */
	void MarkObjectDirty(const Object* object);

	/**
	 * @brief True if a shadow caster changed in any tile the sphere reaches (or in the global tile) after `serial`.
	 */
	bool HasChangedSince(const FVec3 center, float32 radius, uint64 serial) const;

	/// Bumped every time a tile is marked as changed. Store this when baking, and pass it to `HasChangedSince`.
	FX_FORCE_INLINE uint64 GetChangeSerial() const { return mChangeSerial; }

	/**
	 * @brief Inserts a light into every tile its radius reaches. Directional lights, and lights covering more than
	 * `scMaxLightTileCount` tiles, go into the global tile.
	 */
	void AddLight(LightBase* light);

	/**
	 * @brief Moves a light to the tiles it now reaches, if they changed.
	 */
	void UpdateLight(LightBase* light);

	void RemoveLight(LightBase* light);

	UVec2 TileToTileXY(TileIndex tile_index) const;
	FVec3 GetTileWorldPosition(TileIndex tile_index) const;

	FVec3 TileXYToWorldCenter(UVec2 tile_xy) const;

	Tile* GetTile(TileIndex index);
	const Tile* GetTile(TileIndex index) const;

	void SetViewTileIndex(TileIndex view_tile_index);


	FX_FORCE_INLINE BBox GetTileAABB(TileIndex ti) const
	{
		FVec3 tile_position = GetTileWorldPosition(ti);
		FVec3 half(mTileSize.X * 0.5f, 1.0f, mTileSize.Y * 0.5f);
		return BBox(tile_position - half, tile_position + half);
	}

	FX_FORCE_INLINE UVec2 GetGridSize() const { return mGridSize; }

	FX_FORCE_INLINE uint32 GetNumTiles() const { return mTileBuffer.Size; }

	void SetAreaGraph(const CompiledMap& map);
	void ClearAreaGraph();

	FX_FORCE_INLINE const AreaGraph& GetAreaGraph() const { return mAreaGraph; }
	FX_FORCE_INLINE AreaGraph& GetAreaGraph() { return mAreaGraph; }


	~WorldGrid() = default;

private:
	/// Computes the rectangle of tiles (start tile + width/height in tiles) that the object's world-space
	/// bounds overlap.

	void GetObjectTileRect(Object* object, TileIndex* out_start, UVec2* out_span) const;

	void InsertObjectIntoRect(ObjectID id, TileIndex start, UVec2 span);
	void RemoveObjectFromRect(ObjectID id, TileIndex start, UVec2 span);

	bool GetLightPlacement(const LightBase* light, TileIndex* out_start, UVec2* out_span) const;
	void InsertLightIntoRect(LightID id, TileIndex start, UVec2 span);
	void RemoveLightFromRect(LightID id, TileIndex start, UVec2 span);

	TileIndex InsertInto(TileIndex tile_index, ObjectID id);

	void RelocateObject(Object* object, bool update_attached);

	void UpdateObjectAreas(Object* object, bool update_attached);
	void UpdateLightAreas(LightBase* light) const;
	void UpdateAllAreas();

	void AddObjectsFromTile(std::unordered_set<ObjectID>& object_buffer, const Tile* tile) const;


public:
	UVec2 mGridSize;

	/// The size of a single tile in metres. Note that since there is an object cap on world tiles, make sure these are
	/// small enough even though compiled maps have significantly less objects.
	FVec2 mTileSize = FVec2(100.0f);
	FVec3 mPositionOffset = FVec3::sZero;

	TileIndex ViewTileIndex = TileIndexNull;

private:
	SizedArray<Tile> mTileBuffer;

	/// A tile for objects that are always visible.
	Tile GlobalTile;

	uint64 mChangeSerial = 0;

	SizedArray<ObjectID> mNearbyObjectCache;
	bool mbNearbyObjectCacheValid = false;

	AreaGraph mAreaGraph;
};

} // namespace fx
