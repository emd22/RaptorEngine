#pragma once

#include <Core/FreeArray.hpp>
#include <Core/SizedArray.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>

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

class TileSystem
{
	// About half KiB in total per tile. This should be raised when needed.
	static constexpr uint32 scMaxObjectsPerTile = 64;

public:
	TileSystem() = default;

	void Create(const Vec2u grid_size);

	/**
	 * @brief Inserts an object into its respective tile.
	 * @returns The tile index that it was added to.
	 */
	void Insert(ObjectID id);

	TileIndex GetTileIndex(const Vec3f& position) const;
	TileIndex GetTileIndexXY(const Vec2u& xy) const;

	/**
	 * @brief Updates an object to a new tile if the object has moved into another tile boundary.
	 */
	void Update(ObjectID id, bool update_attached = true);

	/**
	 * @brief Remove an object from its assigned tile.
	 */
	void Remove(ObjectID id);

	Vec2u GetTileXY(TileIndex tile_index);
	Tile* GetTile(TileIndex index);

	FX_FORCE_INLINE Vec2u GetGridSize() const { return mGridSize; }

	~TileSystem() = default;

private:
	Tile* GetObjectTile(const Object* object, TileIndex* out_tile_index);
	TileIndex InsertInto(TileIndex tile_index, ObjectID id);

	TileIndex InsertDirect(ObjectID id);

public:
	Vec2u mGridSize;
	Vec2f mTileSize = Vec2f(10.0f, 10.0f);
	Vec3f mPositionOffset = Vec3f::sZero;

private:
	SizedArray<Tile> mTileBuffer;
};

} // namespace fx
