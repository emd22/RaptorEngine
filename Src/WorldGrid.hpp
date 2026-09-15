#pragma once

#include <Core/FreeArray.hpp>
#include <Core/PagedArray.hpp>
#include <Core/SizedArray.hpp>
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
	static constexpr uint32 scMaxObjectsPerTile = 64;

public:
	WorldGrid() = default;

	void Create(const Vec2u grid_size);

	/**
	 * @brief Inserts an object into its respective tile.
	 * @returns The tile index that it was added to.
	 */
	void AddObject(ObjectID id);

	TileIndex WorldToTile(const Vec3f& position) const;
	TileIndex GetTileIndexXY(const Vec2u& xy) const;

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
	Vec3f GetTileWorldPosition(TileIndex tile_index);

	Vec3f TileXYToWorldCenter(Vec2u tile_xy) const;

	Tile* GetTile(TileIndex index);
	const Tile* GetTile(TileIndex index) const;

	void SetViewTileIndex(TileIndex view_tile_index);

	FX_FORCE_INLINE Vec2u GetGridSize() const { return mGridSize; }

	FX_FORCE_INLINE uint32 GetNumTiles() const { return mTileBuffer.Size; }

	~WorldGrid() = default;

private:
	Tile* GetObjectTile(const Object* object, TileIndex* out_tile_index);
	TileIndex InsertInto(TileIndex tile_index, ObjectID id);

	TileIndex InsertDirect(ObjectID id);

	void AddObjectsFromTile(std::unordered_set<ObjectID>& object_buffer, const Tile* tile) const;

public:
	Vec2u mGridSize;
	Vec2f mTileSize = Vec2f(10.0f, 10.0f);
	Vec3f mPositionOffset = Vec3f::sZero;

	TileIndex ViewTileIndex = TileIndexNull;

private:
	SizedArray<Tile> mTileBuffer;

	SizedArray<ObjectID> mNearbyObjectCache;
	bool mbNearbyObjectCacheValid = false;
};

} // namespace fx
