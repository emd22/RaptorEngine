#pragma once

#include <Core/FreeArray.hpp>
#include <Core/PagedArray.hpp>
#include <Core/SizedArray.hpp>
#include <Math/BoundingBox.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>
#include <unordered_set>

namespace fx {


using TileIndex = uint32;

static constexpr TileIndex TileIndexNull = UINT32_MAX;


struct Tile
{
public:
	uint32 FindObject(ObjectID id);

public:
	FreeArray<ObjectID> Objects;
};


class Object;

class WorldGrid
{
public:
	static constexpr uint32 scMaxObjectsPerTile = 64;
	static constexpr uint32 scMaxGlobalObjects = 64;

	static constexpr TileIndex scGlobalTileIndex = UINT32_MAX - 1;

public:
	WorldGrid() = default;

	void Create(const Vec2u grid_size);

	/**
	 * @brief Inserts an object into its respective tile.
	 * @returns The tile index that it was added to.
	 */
	void AddObject(ObjectID id);

	TileIndex WorldToTile(const Vec3f& position) const;
	TileIndex TileFromTileXY(const Vec2u& xy) const;

	const SizedArray<ObjectID>& GetNearbyObjects();

	/**
	 * @brief Updates an object to a new tile if the object has moved into another tile boundary.
	 */
	void UpdateObject(ObjectID id, bool update_attached = true);

	/**
	 * @brief Remove an object from its assigned tile.
	 */
	void RemoveObject(ObjectID id);

	Vec2u TileToTileXY(TileIndex tile_index) const;
	Vec3f GetTileWorldPosition(TileIndex tile_index) const;

	Vec3f TileXYToWorldCenter(Vec2u tile_xy) const;

	Tile* GetTile(TileIndex index);
	const Tile* GetTile(TileIndex index) const;

	void SetViewTileIndex(TileIndex view_tile_index);


	FX_FORCE_INLINE AABB GetTileAABB(TileIndex ti) const
	{
		Vec3f tile_position = GetTileWorldPosition(ti);
		Vec3f half(mTileSize.X * 0.5f, 1.0f, mTileSize.Y * 0.5f);
		return AABB(tile_position - half, tile_position + half);
	}

	FX_FORCE_INLINE Vec2u GetGridSize() const { return mGridSize; }

	FX_FORCE_INLINE uint32 GetNumTiles() const { return mTileBuffer.Size; }

	~WorldGrid() = default;

private:
	/// Computes the rectangle of tiles (start tile + width/height in tiles) that the object's world-space
	/// bounds overlap.
	void GetObjectTileRect(const Object* object, TileIndex* out_start, Vec2u* out_span) const;

	void InsertObjectIntoRect(ObjectID id, TileIndex start, Vec2u span);
	void RemoveObjectFromRect(ObjectID id, TileIndex start, Vec2u span);

	TileIndex InsertInto(TileIndex tile_index, ObjectID id);

	void AddObjectsFromTile(std::unordered_set<ObjectID>& object_buffer, const Tile* tile) const;

public:
	Vec2u mGridSize;
	Vec2f mTileSize = Vec2f(10.0f, 10.0f);
	Vec3f mPositionOffset = Vec3f::sZero;

	TileIndex ViewTileIndex = TileIndexNull;

private:
	SizedArray<Tile> mTileBuffer;

	/// A tile for objects that are always visible.
	Tile GlobalTile;

	SizedArray<ObjectID> mNearbyObjectCache;
	bool mbNearbyObjectCacheValid = false;
};

} // namespace fx
