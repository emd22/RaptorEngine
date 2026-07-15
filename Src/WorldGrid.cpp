/*
 * File:        WorldGrid.cpp
 * Author:      emd22
 * Created:     06/07/2026
 * Description: Breaks scenes into 2D tiles to optimize object collect operations and visibility checks.
 */

#include "WorldGrid.hpp"

#include <Engine.hpp>
#include <Material/MaterialManagerFwd.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>

namespace fx {

/////////////////////////////////////
// Tile functions
/////////////////////////////////////


uint32 Tile::FindObject(ObjectID id)
{
	uint32 index = 0;
	while (true) {
		index = Objects.SlotsInUse.FindNextSetBit(index);
		if (index == Bitset::scNoFreeBits) {
			break;
		}

		ObjectID object_id = Objects.pPtr[index];
		if (object_id == id) {
			return index;
		}

		++index;
	}

	return UINT32_MAX;
}


/////////////////////////////////////
// Tile System
/////////////////////////////////////

void WorldGrid::Create(const Vec2u grid_size)
{
	mGridSize = grid_size;

	uint32 num_tiles = grid_size.X * grid_size.Y;
	mTileBuffer.InitSize(num_tiles);

	Vec2u half_grid = grid_size / Vec2u(2U);

	// Center the grid about zero.
	mPositionOffset = ((Vec3f(half_grid.X, 0.0f, half_grid.Y) * Vec3f(mTileSize.X, 0.0f, mTileSize.Y)));
}

Tile* WorldGrid::GetObjectTile(const Object* object, TileIndex* out_tile_index)
{
	// TODO: Check based on AABB or radius.

	const Vec3f tile_size_v(mTileSize.X, 0.0f, mTileSize.Y);
	const Vec3f position_tile_local = (object->mPosition) / tile_size_v;

	if (position_tile_local.X >= mGridSize.X || position_tile_local.Z >= mGridSize.Y) {
#ifdef FX_TILE_SYSTEM_LOG_ERRORS
		LogError(LC_CORE, "Object out of tile grid range");
#endif
		return nullptr;
	}

	const uint32 tile_index = GetTileIndex(object->mPosition);

	if (out_tile_index != nullptr) {
		(*out_tile_index) = tile_index;
	}

	return &mTileBuffer[tile_index];
}

void WorldGrid::AddObject(ObjectID id)
{
	Object* object = gObjectManager->GetObject(id);

	const Vec3f object_size = object->Bounds.GetSize();

	Vec2u tile_index_start = GetTileXY(GetTileIndex(object->mPosition + object->Bounds.Min));
	Vec2u tile_index_end = GetTileXY(GetTileIndex(object->mPosition + object->Bounds.GetSize()));

	uint32 width_in_tiles = std::clamp(tile_index_end.X - tile_index_start.X + 1, 1U, mGridSize.X);
	uint32 height_in_tiles = std::clamp(tile_index_end.Y - tile_index_start.Y + 1, 1U, mGridSize.Y);

	Vec2u n_offset = Vec2u(width_in_tiles / 2, height_in_tiles / 2);

	for (uint32 y = 0; y < height_in_tiles; y++) {
		for (uint32 x = 0; x < width_in_tiles; x++) {
			TileIndex tile_index = GetTileIndexXY(Vec2u(x + tile_index_start.X, y + tile_index_start.Y));
			InsertInto(tile_index, id);
		}
	}
}

void WorldGrid::AddObjectsFromTile(std::unordered_set<ObjectID>& object_buffer, const Tile* tile) const
{
	uint32 index = 0;

	while (true) {
		index = tile->Objects.SlotsInUse.FindNextSetBit(index);
		if (index == Bitset::scNoFreeBits) {
			break;
		}

		const ObjectID* object_id = tile->Objects.GetItem(index);
		if (!object_id || object_buffer.contains(*object_id)) {
			++index;
			continue;
		}

		object_buffer.emplace(*object_id);
		++index;
	}
}


const SizedArray<ObjectID>& WorldGrid::GetNearbyObjects()
{
	/*
		+--------+--------+--------+-----
		|		 |        |        |
		| -1,  1 |  0,  1 |  1,  1 |  ...
		|		 |        |        |
		+--------+--------+--------+-----
		|		 |        |        |
		| -1,  0 |  VIEW  |  1,  0 |  ...
		|		 |        |        |
		+--------+--------+--------+-----
		|		 |        |        |
		| -1, -1 |  0, -1 |  1, -1 |  ...
		|		 |        |        |
		+--------+--------+--------+-----
		| ...    |  ...   |  ...   |
	*/

	if (mbNearbyObjectCacheValid) {
		return mNearbyObjectCache;
	}

	Vec2u view_xy = GetTileXY(ViewTileIndex);
	const Tile* view_tile = GetTile(GetTileIndexXY(view_xy));

	const Tile* surrounding_tiles[] = {
		// Get the immediate surrounding tiles (up, left, down, right)
		GetTile(GetTileIndexXY(view_xy + Vec2u(1, 0))),
		GetTile(GetTileIndexXY(view_xy + Vec2u(-1, 0))),
		GetTile(GetTileIndexXY(view_xy + Vec2u(0, -1))),
		GetTile(GetTileIndexXY(view_xy + Vec2u(0, 1))),

		// Get the diagonal surrounding tiles
		GetTile(GetTileIndexXY(view_xy + Vec2u(1, 1))),
		GetTile(GetTileIndexXY(view_xy + Vec2u(1, -1))),
		GetTile(GetTileIndexXY(view_xy + Vec2u(-1, -1))),
		GetTile(GetTileIndexXY(view_xy + Vec2u(-1, 1))),
	};

	std::unordered_set<ObjectID> objects_list;

	AddObjectsFromTile(objects_list, view_tile);

	for (uint32 index = 0; index < std::size(surrounding_tiles); index++) {
		AddObjectsFromTile(objects_list, surrounding_tiles[index]);
	}

	mbNearbyObjectCacheValid = true;

	uint32 objects_found_count = objects_list.size();
	if (objects_found_count < mNearbyObjectCache.Capacity) {
		mNearbyObjectCache.Size = 0;
	}
	else {
		mNearbyObjectCache.Free();
		mNearbyObjectCache.InitCapacity(objects_found_count);
	}

	for (ObjectID id : objects_list) {
		mNearbyObjectCache.Insert(id);
	}

	return mNearbyObjectCache;
}

void WorldGrid::SetViewTileIndex(TileIndex view_tile_index)
{
	if (ViewTileIndex != view_tile_index) {
		mbNearbyObjectCacheValid = false;
	}

	ViewTileIndex = view_tile_index;
}

TileIndex WorldGrid::InsertDirect(ObjectID id)
{
	mbNearbyObjectCacheValid = false;

	Object* object = gObjectManager->GetObject(id);

	TileIndex tile_index = 0;

	Tile* tile = GetObjectTile(object, &tile_index);
	object->mTileIndex = tile_index;

	if (!tile->Objects.IsInited()) {
		tile->Objects.Init(scMaxObjectsPerTile);
	}

	fx::ObjectID* out_id = tile->Objects.NewItem();
	(*out_id) = id;

	Vec2u tile_xy = GetTileXY(tile_index);

	LogInfo("Inserting object '{}' into tile index {}, {}", object->Name.Get(), tile_xy.X, tile_xy.Y);

	return tile_index;
}

TileIndex WorldGrid::InsertInto(TileIndex tile_index, ObjectID id)
{
	mbNearbyObjectCacheValid = false;

	Object* object = gObjectManager->GetObject(id);

	Tile& tile = mTileBuffer[tile_index];
	object->mTileIndex = tile_index;

	if (!tile.Objects.IsInited()) {
		tile.Objects.Init(scMaxObjectsPerTile);
	}

	fx::ObjectID* out_id = tile.Objects.NewItem();
	(*out_id) = id;

	return tile_index;
}


void WorldGrid::UpdateObject(ObjectID id, bool update_attached)
{
	mbNearbyObjectCacheValid = false;

	Object* object = gObjectManager->GetObject(id);

	TileIndex new_tile_index = 0;
	Tile* new_tile = GetObjectTile(object, &new_tile_index);

	// The object hasn't moved past boundaries, leave it where it is
	if (new_tile_index == object->mTileIndex) {
		return;
	}

	// Object has not been added to tile map, ignore
	if (object->mTileIndex == TileIndexNull) {
#ifdef FX_TILE_SYSTEM_LOG_ERRORS
		LogError(LC_CORE, "Object ({}) has not been added to tile system!", id);
#endif
		return;
	}

	// Find the tile that the object is currently in. If it has been found, remove it from the current tile and add it
	// to the tile in the new location.
	Tile& old_tile = mTileBuffer[object->mTileIndex];
	const TileIndex old_index = old_tile.FindObject(id);

	if (old_index == TileIndexNull) {
#ifdef FX_TILE_SYSTEM_LOG_ERRORS
		LogWarning(LC_CORE, "Could not find object ({}) in previous tile", id);
#endif
	}
	else {
		old_tile.Objects.FreeItem(old_index);
	}

	// Finally, insert the object into the new tile.
	InsertInto(new_tile_index, id);
	object->mTileIndex = new_tile_index;

	// If requested, update the objects attached to the current object.
	if (update_attached) {
		for (ObjectID attached_id : object->AttachedNodes) {
			Object* attached_object = gObjectManager->GetObject(attached_id);
			if (object->mTileIndex == attached_object->mTileIndex) {
				continue;
			}

			UpdateObject(attached_id, true);
		}
	}
}

Vec2u WorldGrid::GetTileXY(TileIndex tile_index) const
{
	const uint32 tile_x = tile_index % mGridSize.X;
	const uint32 tile_y = (tile_index - tile_x) / mGridSize.X;
	return Vec2u(tile_x, tile_y);
}


Tile* WorldGrid::GetTile(TileIndex index)
{
	if (index > mTileBuffer.Capacity) {
		return nullptr;
	}

	return &mTileBuffer[index];
}

const Tile* WorldGrid::GetTile(TileIndex index) const
{
	if (index > mTileBuffer.Capacity) {
		return nullptr;
	}

	return &mTileBuffer[index];
}

TileIndex WorldGrid::GetTileIndexXY(const Vec2u& xy) const
{
	return std::min(xy.Y, mGridSize.Y - 1) * mGridSize.X + std::min(xy.X, mGridSize.X - 1);
}

TileIndex WorldGrid::GetTileIndex(const Vec3f& position) const
{
	const Vec3f adjusted = position + mPositionOffset;
	return std::min(static_cast<uint32>(std::floor(adjusted.Z / mTileSize.Y)), mGridSize.Y - 1) * mGridSize.X +
		   std::min(static_cast<uint32>(std::floor(adjusted.X / mTileSize.X)), mGridSize.X - 1);
}

void WorldGrid::RemoveObject(ObjectID id)
{
	mbNearbyObjectCacheValid = false;

	Object* object = gObjectManager->GetObject(id);
	TileIndex tile_index = object->mTileIndex;

	// Object was not assigned a tile, ignore
	if (tile_index == TileIndexNull) {
		return;
	}

	Tile& tile = mTileBuffer[tile_index];
	tile.Objects.FreeItem(tile.FindObject(id));
}

} // namespace fx
