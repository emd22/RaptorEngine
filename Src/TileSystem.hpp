#pragma once

#include <Core/FreeArray.hpp>
#include <Core/SizedArray.hpp>
#include <Math/Vec2.hpp>
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
	uint32 Insert(ObjectID id);


	/**
	 * @brief Updates an object to a new tile if the object has moved into another tile boundary.
	 */
	void Update(ObjectID id, bool update_attached = true);

	void Remove(ObjectID id);


	~TileSystem() = default;

private:
	Tile* GetObjectTile(const Object* object, uint32* out_tile_index);
	uint32 InsertInto(uint32 tile_index, ObjectID id);

	/**
	 * @brief A shallower position update that avoids extraneous checks and retrieves.
	 */
	void UpdateAttached(Object* base_object, ObjectID attached_id);

public:
	Vec2f TileSize = Vec2f(5.0f, 5.0f);


private:
	SizedArray<Tile> mTileBuffer;
	Vec2u mGridSize;
};

} // namespace fx
