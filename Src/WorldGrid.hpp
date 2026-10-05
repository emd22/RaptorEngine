#pragma once

#include <raptor_ffi.h>

#include <Core/Types.hpp>
#include <Math/BoundingBox.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>
#include <Renderer/LightID.hpp>

namespace fx {


using TileIndex = uint32;

static constexpr TileIndex TileIndexNull = UINT32_MAX;

static_assert(TileIndexNull == RX_WORLD_GRID_NULL_TILE);
static_assert(sizeof(ObjectID) == sizeof(uint32));
static_assert(sizeof(LightID) == sizeof(uint32));


/**
 * @brief What is in a tile. The slots hold a null ID where there is nothing, and stay where they are for as long as
 * the tile exists, so a pointer to one can be kept until the grid changes the slot.
 */
struct TileView
{
public:
	bool IsValid() const { return bValid; }

	template <typename TFunc>
	void ForEachObject(TFunc&& func) const
	{
		for (uint32 i = 0; i < ObjectSlots; i++) {
			if (!pObjects[i].IsNull()) {
				func(&pObjects[i]);
			}
		}
	}

	template <typename TFunc>
	void ForEachLight(TFunc&& func) const
	{
		for (uint32 i = 0; i < LightSlots; i++) {
			if (!pLights[i].IsNull()) {
				func(pLights[i]);
			}
		}
	}

public:
	ObjectID* pObjects = nullptr;
	uint32 ObjectSlots = 0;

	LightID* pLights = nullptr;
	uint32 LightSlots = 0;

	bool bValid = false;
};


struct ObjectIDSpan
{
	const ObjectID* begin() const { return pFirst; }
	const ObjectID* end() const { return pFirst + Size; }

	const ObjectID* pFirst = nullptr;
	uint32 Size = 0;
};


class Object;
class LightBase;

class WorldGrid
{
public:
	static constexpr TileIndex scGlobalTileIndex = RX_WORLD_GRID_GLOBAL_TILE;

public:
	WorldGrid();
	~WorldGrid();

	WorldGrid(const WorldGrid&) = delete;
	WorldGrid& operator=(const WorldGrid&) = delete;

	void Create(const Vec2u grid_size);

	/**
	 * @brief Inserts an object into the tiles its bounds reach, or the global tile if it cannot be culled.
	 */
	void AddObject(ObjectID id);

	TileIndex WorldToTile(const Vec3f& position) const;
	TileIndex TileFromTileXY(const Vec2u& xy) const;

	/**
	 * @brief The objects in the tile of the view and the tiles around it. Valid until the grid next changes.
	 */
	ObjectIDSpan GetNearbyObjects();

	/**
	 * @brief Updates an object to new tiles if the object has moved into another tile boundary.
	 */
	void UpdateObject(Object* object, bool update_attached = true);

	/**
	 * @brief Remove an object from its assigned tiles.
	 */
	void RemoveObject(ObjectID id);

	/**
	 * @brief Inserts a light into every tile its radius reaches. Directional lights, and lights covering more than
	 * a set number of tiles, go into the global tile.
	 */
	void AddLight(LightBase* light);

	/**
	 * @brief Moves a light to the tiles it now reaches, if they changed.
	 */
	void UpdateLight(LightBase* light);

	void RemoveLight(LightBase* light);

	Vec2u TileToTileXY(TileIndex tile_index) const;
	Vec3f GetTileWorldPosition(TileIndex tile_index) const;

	Vec3f TileXYToWorldCenter(Vec2u tile_xy) const;

	TileView GetTile(TileIndex index) const;

	void SetViewTileIndex(TileIndex view_tile_index);

	/// The first tile an object or light is in, scGlobalTileIndex for the global tile, or TileIndexNull if it is not in
	/// the grid.
	TileIndex GetObjectTile(ObjectID id) const;
	TileIndex GetLightTile(const LightBase* light) const;

	AABB GetTileAABB(TileIndex ti) const;

	Vec2u GetGridSize() const;
	Vec2f GetTileSize() const;
	Vec3f GetPositionOffset() const;
	TileIndex GetViewTileIndex() const;

	uint32 GetNumTiles() const;

private:
	RxWorldGrid* mpGrid = nullptr;
};

} // namespace fx
