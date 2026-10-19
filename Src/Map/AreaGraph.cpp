#include "AreaGraph.hpp"

#include <Core/Log.hpp>
#include <Map/CompiledMap.hpp>
#include <cmath>
#include <cstring>

namespace fx {

void AreaGraph::Clear()
{
	mNodes.Free();
	mAreas.Free();
	mPortals.Free();
	mRoot = CompiledMap::MakeLeafChild(scNoMapArea);
}

void AreaGraph::Load(const CompiledMap& map)
{
	Clear();

	if (!map.HasAreas()) {
		return;
	}

	if (map.Areas.Size > AreaSet::scMaxAreaIndex) {
		LogWarning(LC_ASSET, "The compiled map has {} areas, more than the {} that can be culled", map.Areas.Size,
				   AreaSet::scMaxAreaIndex);
		return;
	}

	mNodes.InitSize(map.AreaNodes.Size);

	for (size_t i = 0; i < map.AreaNodes.Size; i++) {
		const CompiledMapAreaNode& node = map.AreaNodes[i];

		for (uint32 component = 0; component < 4; component++) {
			mNodes[i].Plane[component] = node.Plane[component];
		}

		mNodes[i].Front = node.Front;
		mNodes[i].Back = node.Back;
	}

	mRoot = map.AreaRoot;

	SizedArray<uint32> portal_counts;
	portal_counts.InitSize(map.Areas.Size);

	for (uint32& count : portal_counts) {
		count = 0;
	}

	for (const CompiledMapAreaPortal& portal : map.AreaPortals) {
		portal_counts[portal.BackArea]++;
		portal_counts[portal.FrontArea]++;
	}

	mAreas.InitSize(map.Areas.Size);

	for (size_t i = 0; i < map.Areas.Size; i++) {
		mAreas[i].Bounds = map.Areas[i].Bounds;
		mAreas[i].Portals.InitCapacity(portal_counts[i]);
	}

	mPortals.InitSize(map.AreaPortals.Size);

	for (size_t i = 0; i < map.AreaPortals.Size; i++) {
		const CompiledMapAreaPortal& source = map.AreaPortals[i];
		MapPortal& portal = mPortals[i];

		portal.Name = source.Name;
		portal.BackArea = source.BackArea;
		portal.FrontArea = source.FrontArea;
		portal.Bounds = source.Bounds;

		const uint32 point_count = source.GetPointCount();

		portal.Points.InitSize(point_count);

		for (uint32 point = 0; point < point_count; point++) {
			portal.Points[point] = Vec3f(source.Points[point * 3], source.Points[point * 3 + 1],
										 source.Points[point * 3 + 2]);
		}

		portal.PolygonSizes.InitAsCopyOf(source.PolygonSizes);
		portal.PolygonPlanes.InitSize(portal.PolygonSizes.Size);

		uint32 first_point = 0;

		for (size_t polygon = 0; polygon < portal.PolygonSizes.Size; polygon++) {
			const uint32 size = portal.PolygonSizes[polygon];

			Vec3f normal = Vec3f::sZero;
			Vec3f center = Vec3f::sZero;

			for (uint32 point = 0; point < size; point++) {
				const Vec3f& a = portal.Points[first_point + point];
				const Vec3f& b = portal.Points[first_point + (point + 1) % size];

				normal.X += (a.Y - b.Y) * (a.Z + b.Z);
				normal.Y += (a.Z - b.Z) * (a.X + b.X);
				normal.Z += (a.X - b.X) * (a.Y + b.Y);

				center += a;
			}

			normal = normal.Normalize();
			center = center * (1.0f / static_cast<float32>(size));

			portal.PolygonPlanes[polygon].Normal = normal;
			portal.PolygonPlanes[polygon].Distance = normal.Dot(center);

			first_point += size;
		}

		mAreas[portal.BackArea].Portals.Insert(static_cast<uint32>(i));
		mAreas[portal.FrontArea].Portals.Insert(static_cast<uint32>(i));
	}

	LogInfo("Loaded {} areas and {} portals", mAreas.Size, mPortals.Size);
}

int32 AreaGraph::FindPortal(const char* name) const
{
	if (name == nullptr) {
		return -1;
	}

	for (size_t i = 0; i < mPortals.Size; i++) {
		if (std::strcmp(mPortals[i].Name.CStr(), name) == 0) {
			return static_cast<int32>(i);
		}
	}

	return -1;
}

int32 AreaGraph::FindArea(const Vec3f& point) const
{
	int32 child = mRoot;

	while (child >= 0) {
		const MapAreaNode& node = mNodes[static_cast<size_t>(child)];
		const float32 distance = node.Plane[0] * point.X + node.Plane[1] * point.Y + node.Plane[2] * point.Z -
								 node.Plane[3];

		if (distance >= 0.0f) {
			child = node.Front;
		}
		else {
			child = node.Back;
		}
	}

	return CompiledMap::GetLeafArea(child);
}

void AreaGraph::CollectAreas(int32 child, const Vec3f& center, const Vec3f& extent, AreaSet& out_set) const
{
	while (child >= 0 && !out_set.bEverywhere) {
		const MapAreaNode& node = mNodes[static_cast<size_t>(child)];

		const float32 distance = node.Plane[0] * center.X + node.Plane[1] * center.Y + node.Plane[2] * center.Z -
								 node.Plane[3];
		const float32 radius = std::abs(node.Plane[0]) * extent.X + std::abs(node.Plane[1]) * extent.Y +
							   std::abs(node.Plane[2]) * extent.Z;

		if (distance - radius >= 0.0f) {
			child = node.Front;
		}
		else if (distance + radius < 0.0f) {
			child = node.Back;
		}
		else {
			CollectAreas(node.Front, center, extent, out_set);
			child = node.Back;
		}
	}

	if (child < 0 && !out_set.bEverywhere) {
		const int32 area = CompiledMap::GetLeafArea(child);

		if (area != scNoMapArea) {
			out_set.Add(static_cast<uint32>(area));
		}
	}
}

AreaSet AreaGraph::FindAreas(const BBox& bounds) const
{
	if (IsEmpty()) {
		return AreaSet {};
	}

	const Vec3f center = (bounds.Min + bounds.Max) * 0.5f;
	const Vec3f extent = (bounds.Max - bounds.Min) * 0.5f;

	AreaSet set = AreaSet::MakeEmpty();
	CollectAreas(mRoot, center, extent, set);

	if (set.Count == 0) {
		return AreaSet {};
	}

	return set;
}

} // namespace fx
