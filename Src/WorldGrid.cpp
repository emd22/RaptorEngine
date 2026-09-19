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

void WorldGrid::GetObjectTileRect(const Object* object, TileIndex* out_start, Vec2u* out_span) const
{
	DebugAssert(out_start != nullptr);
	DebugAssert(out_span != nullptr);

	const Vec2u tile_xy_start = TileToTileXY(WorldToTile(object->mPosition + object->Bounds.Min));
	const Vec2u tile_xy_end = TileToTileXY(WorldToTile(object->mPosition + object->Bounds.Max));

	const uint32 width_in_tiles = std::clamp(tile_xy_end.X - tile_xy_start.X + 1, 1U, mGridSize.X);
	const uint32 height_in_tiles = std::clamp(tile_xy_end.Y - tile_xy_start.Y + 1, 1U, mGridSize.Y);

	*out_start = TileFromTileXY(tile_xy_start);
	*out_span = Vec2u(width_in_tiles, height_in_tiles);
}

void WorldGrid::InsertObjectIntoRect(ObjectID id, TileIndex start, Vec2u span)
{
	const Vec2u start_xy = TileToTileXY(start);

	for (uint32 y = 0; y < span.Y; y++) {
		for (uint32 x = 0; x < span.X; x++) {
			InsertInto(TileFromTileXY(start_xy + Vec2u(x, y)), id);
		}
	}
}

void WorldGrid::RemoveObjectFromRect(ObjectID id, TileIndex start, Vec2u span)
{
	const Vec2u start_xy = TileToTileXY(start);

	for (uint32 y = 0; y < span.Y; y++) {
		for (uint32 x = 0; x < span.X; x++) {
			Tile* tile = GetTile(TileFromTileXY(start_xy + Vec2u(x, y)));
			if (tile == nullptr) {
				continue;
			}

			const uint32 index = tile->FindObject(id);
			if (index == TileIndexNull) {
#ifdef FX_TILE_SYSTEM_LOG_ERRORS
				LogWarning(LC_CORE, "Could not find object ({}) in previous tile", id);
#endif
				continue;
			}

			tile->Objects.FreeItem(index);
		}
	}
}

void WorldGrid::AddObject(ObjectID id)
{
	if (id.IsInvalid()) {
		LogWarning("Cannot add invalid object ID to world grid");
		return;
	}

	Object* object = gObjectManager->GetObject(id);
	if (object == nullptr) {
		return;
	}

	// The object isn't cullable, insert into global tile
	if (!object->IsCullable()) {
		InsertInto(scGlobalTileIndex, object->ID);
		return;
	}

	TileIndex tile_start = TileIndexNull;
	Vec2u tile_span = Vec2u(1, 1);
	GetObjectTileRect(object, &tile_start, &tile_span);

	InsertObjectIntoRect(id, tile_start, tile_span);

	// InsertInto() overwrites mTileIndex for each tile it's called on, so record the full span here
	// after inserting into every tile so UpdateObject/RemoveObject can clear all of them later.
	object->mTileIndex = tile_start;
	object->mTileSpan = tile_span;
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

	Vec2u view_xy = TileToTileXY(ViewTileIndex);
	const Tile* view_tile = GetTile(TileFromTileXY(view_xy));

	const Tile* surrounding_tiles[] = {

		// Get the immediate surrounding tiles (up, left, down, right)
		GetTile(TileFromTileXY(view_xy + Vec2u(1, 0))),
		GetTile(TileFromTileXY(view_xy + Vec2u(-1, 0))),
		GetTile(TileFromTileXY(view_xy + Vec2u(0, -1))),
		GetTile(TileFromTileXY(view_xy + Vec2u(0, 1))),

		// Get the diagonal surrounding tiles
		GetTile(TileFromTileXY(view_xy + Vec2u(1, 1))),
		GetTile(TileFromTileXY(view_xy + Vec2u(1, -1))),
		GetTile(TileFromTileXY(view_xy + Vec2u(-1, -1))),
		GetTile(TileFromTileXY(view_xy + Vec2u(-1, 1))),
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


TileIndex WorldGrid::InsertInto(TileIndex tile_index, ObjectID id)
{
	mbNearbyObjectCacheValid = false;

	Object* object = gObjectManager->GetObject(id);
	if (object == nullptr) {
		return TileIndexNull;
	}

	// Insert into global tile (uncullable)
	if (!object->IsCullable()) {
		object->mTileIndex = scGlobalTileIndex;

		if (!GlobalTile.Objects.IsInited()) {
			GlobalTile.Objects.Init(scMaxGlobalObjects);
		}

		fx::ObjectID* out_id = GlobalTile.Objects.NewItem();
		if (out_id == nullptr) {
			LogWarning("Global tile is full, cannot insert object {}", tile_index, id);
			return TileIndexNull;
		}

		(*out_id) = id;

		return scGlobalTileIndex;
	}

	Tile& tile = mTileBuffer[tile_index];
	object->mTileIndex = tile_index;

	if (!tile.Objects.IsInited()) {
		tile.Objects.Init(scMaxObjectsPerTile);
	}

	fx::ObjectID* out_id = tile.Objects.NewItem();
	// Tile is full, die
	if (out_id == nullptr) {
		LogWarning("Tile {} is full, cannot insert object {}", tile_index, id);
		return TileIndexNull;
	}

	(*out_id) = id;

	return tile_index;
}

void WorldGrid::UpdateObject(Object* object, bool update_attached)
{
	if (object == nullptr) {
		return;
	}

	// Object has not been added to tile map, ignore.
	if (object->mTileIndex == TileIndexNull) {
#ifdef FX_TILE_SYSTEM_LOG_ERRORS
		LogError(LC_CORE, "Object ({}) has not been added to tile system!", id);
#endif
		return;
	}

	mbNearbyObjectCacheValid = false;

	if (!object->IsCullable()) {
		if (object->mTileIndex != scGlobalTileIndex) {
			RemoveObjectFromRect(object->ID, object->mTileIndex, object->mTileSpan);
			InsertInto(scGlobalTileIndex, object->ID);
			object->mTileSpan = Vec2u(1, 1);
		}
		return;
	}

	TileIndex new_tile_start = TileIndexNull;
	Vec2u new_tile_span = Vec2u(1, 1);
	GetObjectTileRect(object, &new_tile_start, &new_tile_span);

	// The object hasn't moved past any tile boundaries, leave it where it is
	if (new_tile_start == object->mTileIndex && new_tile_span == object->mTileSpan) {
		return;
	}

	// Clear every tile the object was previously spanning, then insert it into every tile it now spans.
	RemoveObjectFromRect(object->ID, object->mTileIndex, object->mTileSpan);
	InsertObjectIntoRect(object->ID, new_tile_start, new_tile_span);

	object->mTileIndex = new_tile_start;
	object->mTileSpan = new_tile_span;

	// If requested, update the objects attached to the current object.
	if (update_attached) {
		for (ObjectID attached_id : object->AttachedNodes) {
			Object* attached_object = gObjectManager->GetObject(attached_id);

			if (attached_object == nullptr || object->mTileIndex == attached_object->mTileIndex) {
				continue;
			}

			UpdateObject(attached_object, true);
		}
	}
}

Vec2u WorldGrid::TileToTileXY(TileIndex tile_index) const
{
	const uint32 tile_x = tile_index % mGridSize.X;
	const uint32 tile_y = (tile_index - tile_x) / mGridSize.X;
	return Vec2u(tile_x, tile_y);
}

Vec3f WorldGrid::GetTileWorldPosition(TileIndex tile_index) const
{
	return TileXYToWorldCenter(TileToTileXY(tile_index));
}

Vec3f WorldGrid::TileXYToWorldCenter(Vec2u tile_xy) const
{
	return Vec3f((static_cast<float32>(tile_xy.X) * mTileSize.X) - mPositionOffset.X + (mTileSize.X * 0.5f),
				 mPositionOffset.Y,
				 (static_cast<float32>(tile_xy.Y) * mTileSize.Y) - mPositionOffset.Z + (mTileSize.Y * 0.5f));
}


Tile* WorldGrid::GetTile(TileIndex index)
{
	if (index == scGlobalTileIndex) {
		return &GlobalTile;
	}

	if (index >= mTileBuffer.Capacity) {
		return nullptr;
	}

	return &mTileBuffer[index];
}

const Tile* WorldGrid::GetTile(TileIndex index) const
{
	if (index == scGlobalTileIndex) {
		return &GlobalTile;
	}

	if (index >= mTileBuffer.Capacity) {
		return nullptr;
	}

	return &mTileBuffer[index];
}

TileIndex WorldGrid::TileFromTileXY(const Vec2u& xy) const
{
	return std::min(xy.Y, mGridSize.Y - 1) * mGridSize.X + std::min(xy.X, mGridSize.X - 1);
}

TileIndex WorldGrid::WorldToTile(const Vec3f& position) const
{
	const Vec3f adjusted = position + mPositionOffset;
	const int32 tile_x = static_cast<int>(std::floor(adjusted.X / mTileSize.X));
	const int32 tile_z = static_cast<int>(std::floor(adjusted.Z / mTileSize.Y));

	const uint32 clamped_x = static_cast<uint32>(std::clamp(tile_x, 0, static_cast<int>(mGridSize.X) - 1));
	const uint32 clamped_z = static_cast<uint32>(std::clamp(tile_z, 0, static_cast<int>(mGridSize.Y) - 1));

	return clamped_z * mGridSize.X + clamped_x;
}

void WorldGrid::RemoveObject(ObjectID id)
{
	mbNearbyObjectCacheValid = false;

	Object* object = gObjectManager->GetObject(id);
	if (object == nullptr) {
		return;
	}

	// Object was not assigned a tile, ignore
	if (object->mTileIndex == TileIndexNull) {
		return;
	}

	if (object->mTileIndex == scGlobalTileIndex) {
		const uint32 index = GlobalTile.FindObject(id);
		if (index != TileIndexNull) {
			GlobalTile.Objects.FreeItem(index);
		}
		return;
	}

	RemoveObjectFromRect(id, object->mTileIndex, object->mTileSpan);
}

} // namespace fx
