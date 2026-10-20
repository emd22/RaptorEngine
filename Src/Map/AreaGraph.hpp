#pragma once

#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Map/AreaSet.hpp>
#include <Math/BBox.hpp>
#include <Math/Vec3.hpp>

namespace fx {

class CompiledMap;

struct MapPortalPlane
{
	Vec3f Normal = Vec3f::sZero;
	float32 Distance = 0.0f;
};

struct MapPortal
{
	String Name;

	uint32 BackArea = 0;
	uint32 FrontArea = 0;

	BBox Bounds;

	SizedArray<Vec3f> Points;
	SizedArray<uint32> PolygonSizes;
	SizedArray<MapPortalPlane> PolygonPlanes;

	bool bOpen = true;

	uint32 GetOtherArea(uint32 area) const
	{
		if (area == BackArea) {
			return FrontArea;
		}

		return BackArea;
	}
};

struct MapAreaNode
{
	float32 Plane[4] = {};
	int32 Front = 0;
	int32 Back = 0;
};

struct MapArea
{
	BBox Bounds;
	SizedArray<uint32> Portals;
};

class AreaGraph
{
public:
	void Load(const CompiledMap& map);
	void Clear();

	bool IsEmpty() const { return mAreas.Size == 0; }

	uint32 GetAreaCount() const { return static_cast<uint32>(mAreas.Size); }
	const MapArea& GetArea(uint32 index) const { return mAreas[index]; }

	uint32 GetPortalCount() const { return static_cast<uint32>(mPortals.Size); }
	const MapPortal& GetPortal(uint32 index) const { return mPortals[index]; }

	int32 FindPortal(const char* name) const;
	void SetPortalOpen(uint32 index, bool open) { mPortals[index].bOpen = open; }

	int32 FindArea(const Vec3f& point) const;
	AreaSet FindAreas(const BBox& bounds) const;
	AreaSet FindLightAreas(const Vec3f& origin, const BBox& bounds) const;

private:
	void CollectAreas(int32 child, const Vec3f& center, const Vec3f& extent, AreaSet& out_set) const;
	void AddLightSeedArea(const Vec3f& point, AreaSet& out_set) const;

private:
	SizedArray<MapAreaNode> mNodes;
	int32 mRoot = -1;

	SizedArray<MapArea> mAreas;
	SizedArray<MapPortal> mPortals;
};

} // namespace fx
