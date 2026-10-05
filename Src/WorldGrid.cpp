/*
 * File:        WorldGrid.cpp
 * Author:      emd22
 * Created:     06/07/2026
 * Description: Breaks scenes into 2D tiles to optimize object collect operations and visibility checks.
 */

#include "WorldGrid.hpp"

#include <Engine.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Light.hpp>
#include <Util/RustInterop.hpp>

namespace fx {

static_assert(WorldGrid::scGlobalTileIndex == RX_WORLD_GRID_GLOBAL_TILE);

namespace {

const RxLogSink scGridLog = { .user = nullptr, .log = RustInterop::Log };

void BoundsToFloats(const AABB& bounds, float32 out_min[3], float32 out_max[3])
{
	out_min[0] = bounds.Min.X;
	out_min[1] = bounds.Min.Y;
	out_min[2] = bounds.Min.Z;

	out_max[0] = bounds.Max.X;
	out_max[1] = bounds.Max.Y;
	out_max[2] = bounds.Max.Z;
}

} // namespace

WorldGrid::WorldGrid() { mpGrid = rx_world_grid_new(&scGridLog); }

WorldGrid::~WorldGrid()
{
	rx_world_grid_free(mpGrid);
	mpGrid = nullptr;
}

void WorldGrid::Create(const Vec2u grid_size) { rx_world_grid_create(mpGrid, grid_size.X, grid_size.Y); }

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

	float32 min[3];
	float32 max[3];
	BoundsToFloats(object->GetWorldOBB().GetWorldAABB(), min, max);

	rx_world_grid_add_object(mpGrid, object->ID.GetID(), min, max, object->IsCullable() ? 1 : 0);
}

void WorldGrid::UpdateObject(Object* object, bool update_attached)
{
	if (object == nullptr) {
		return;
	}

	float32 min[3];
	float32 max[3];
	BoundsToFloats(object->GetWorldOBB().GetWorldAABB(), min, max);

	const uint32 result = rx_world_grid_update_object(mpGrid, object->ID.GetID(), min, max, object->IsCullable() ? 1 : 0);

	// If requested, update the objects attached to the current object.
	if (result != RX_OBJECT_MOVED || !update_attached) {
		return;
	}

	const TileIndex object_tile = GetObjectTile(object->ID);

	for (ObjectID attached_id : object->AttachedNodes) {
		Object* attached_object = gObjectManager->GetObject(attached_id);

		if (attached_object == nullptr || object_tile == GetObjectTile(attached_object->ID)) {
			continue;
		}

		UpdateObject(attached_object, true);
	}
}

void WorldGrid::RemoveObject(ObjectID id)
{
	if (gObjectManager->GetObject(id) == nullptr) {
		return;
	}

	rx_world_grid_remove_object(mpGrid, id.GetID());
}

void WorldGrid::AddLight(LightBase* light)
{
	if (light == nullptr || light->ID.IsNull() || light->ID.IsInvalid()) {
		return;
	}

	float32 min[3];
	float32 max[3];
	BoundsToFloats(light->GetBounds(), min, max);

	rx_world_grid_add_light(mpGrid, light->ID.GetID(), min, max, light->IsCullable() ? 1 : 0);
}

void WorldGrid::UpdateLight(LightBase* light)
{
	if (light == nullptr || light->ID.IsNull() || light->ID.IsInvalid()) {
		return;
	}

	float32 min[3];
	float32 max[3];
	BoundsToFloats(light->GetBounds(), min, max);

	rx_world_grid_update_light(mpGrid, light->ID.GetID(), min, max, light->IsCullable() ? 1 : 0);
}

void WorldGrid::RemoveLight(LightBase* light)
{
	if (light == nullptr || light->ID.IsNull() || light->ID.IsInvalid()) {
		return;
	}

	rx_world_grid_remove_light(mpGrid, light->ID.GetID());
}

ObjectIDSpan WorldGrid::GetNearbyObjects()
{
	uint32 count = 0;
	const uint32* ids = rx_world_grid_nearby_objects(mpGrid, &count);

	return ObjectIDSpan { .pFirst = reinterpret_cast<const ObjectID*>(ids), .Size = count };
}

void WorldGrid::SetViewTileIndex(TileIndex view_tile_index) { rx_world_grid_set_view_tile(mpGrid, view_tile_index); }

TileIndex WorldGrid::GetObjectTile(ObjectID id) const { return rx_world_grid_object_tile(mpGrid, id.GetID()); }

TileIndex WorldGrid::GetLightTile(const LightBase* light) const
{
	return rx_world_grid_light_tile(mpGrid, light->ID.GetID());
}

TileIndex WorldGrid::WorldToTile(const Vec3f& position) const
{
	return rx_world_grid_world_to_tile(mpGrid, position.X, position.Y, position.Z);
}

TileIndex WorldGrid::TileFromTileXY(const Vec2u& xy) const { return rx_world_grid_tile_from_xy(mpGrid, xy.X, xy.Y); }

Vec2u WorldGrid::TileToTileXY(TileIndex tile_index) const
{
	uint32 xy[2];
	rx_world_grid_tile_to_xy(mpGrid, tile_index, xy);

	return Vec2u(xy[0], xy[1]);
}

Vec3f WorldGrid::GetTileWorldPosition(TileIndex tile_index) const
{
	return TileXYToWorldCenter(TileToTileXY(tile_index));
}

Vec3f WorldGrid::TileXYToWorldCenter(Vec2u tile_xy) const
{
	float32 center[3];
	rx_world_grid_tile_world_center(mpGrid, tile_xy.X, tile_xy.Y, center);

	return Vec3f(center);
}

TileView WorldGrid::GetTile(TileIndex index) const
{
	RxTileSlots slots {};

	if (!rx_world_grid_tile_slots(mpGrid, index, &slots)) {
		return TileView {};
	}

	return TileView {
		.pObjects = reinterpret_cast<ObjectID*>(const_cast<uint32*>(slots.objects)),
		.ObjectSlots = slots.object_slots,
		.pLights = reinterpret_cast<LightID*>(const_cast<uint32*>(slots.lights)),
		.LightSlots = slots.light_slots,
		.bValid = true,
	};
}

AABB WorldGrid::GetTileAABB(TileIndex ti) const
{
	RxTileBounds bounds;
	rx_world_grid_tile_bounds(mpGrid, ti, &bounds);

	return AABB(Vec3f(bounds.min), Vec3f(bounds.max));
}

Vec2u WorldGrid::GetGridSize() const
{
	RxGridInfo info;
	rx_world_grid_info(mpGrid, &info);

	return Vec2u(info.grid_size[0], info.grid_size[1]);
}

Vec2f WorldGrid::GetTileSize() const
{
	RxGridInfo info;
	rx_world_grid_info(mpGrid, &info);

	return Vec2f(info.tile_size[0], info.tile_size[1]);
}

Vec3f WorldGrid::GetPositionOffset() const
{
	RxGridInfo info;
	rx_world_grid_info(mpGrid, &info);

	return Vec3f(info.position_offset);
}

TileIndex WorldGrid::GetViewTileIndex() const
{
	RxGridInfo info;
	rx_world_grid_info(mpGrid, &info);

	return info.view_tile;
}

uint32 WorldGrid::GetNumTiles() const
{
	RxGridInfo info;
	rx_world_grid_info(mpGrid, &info);

	return info.tile_count;
}

} // namespace fx
