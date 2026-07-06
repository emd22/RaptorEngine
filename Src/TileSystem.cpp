/*
 * File:        TileSystem.cpp
 * Author:      emd22
 * Created:     06/07/2026
 * Description: Breaks scenes into 2D tiles to optimize object collect operations and visibility checks.
 */

#include "TileSystem.hpp"

#include <Engine.hpp>
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

void TileSystem::Create(const Vec2u grid_size)
{
	mGridSize = grid_size;

	uint32 num_tiles = grid_size.X * grid_size.Y;
	mTileBuffer.InitSize(num_tiles);
}

Tile* TileSystem::GetObjectTile(const Object* object, uint32* out_tile_index)
{
	// TODO: Check based on AABB or radius.

	const float32 x = object->mPosition.GetX();
	const float32 z = object->mPosition.GetZ();

	if (x >= mGridSize.X || z >= mGridSize.Y) {
#ifdef FX_TILE_SYSTEM_LOG_ERRORS
		LogError(LC_CORE, "Object out of tile grid range");
#endif
		return nullptr;
	}

	const uint32 tile_index = z * mGridSize.Y + x;

	if (out_tile_index != nullptr) {
		(*out_tile_index) = tile_index;
	}

	return &mTileBuffer[tile_index];
}


uint32 TileSystem::Insert(ObjectID id)
{
	Object* object = gObjectManager->GetObject(id);

	uint32 tile_index = 0;

	Tile* tile = GetObjectTile(object, &tile_index);
	object->mTileIndex = tile_index;

	if (!tile->Objects.IsInited()) {
		tile->Objects.Init(scMaxObjectsPerTile);
	}

	fx::ObjectID* out_id = tile->Objects.NewItem();
	(*out_id) = id;

	return tile_index;
}

uint32 TileSystem::InsertInto(uint32 tile_index, ObjectID id)
{
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


void TileSystem::Update(ObjectID id, bool update_attached)
{
	Object* object = gObjectManager->GetObject(id);

	uint32 new_tile_index = 0;
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
	const uint32 old_index = old_tile.FindObject(id);

	if (old_index == UINT32_MAX) {
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

			Update(attached_id, true);
		}
	}
}

} // namespace fx
