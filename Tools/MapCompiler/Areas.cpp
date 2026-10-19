#include "Areas.hpp"

#include <Core/ArrayUtil.hpp>
#include <algorithm>
#include <cfloat>
#include <map>

namespace fx::mapc {

namespace {

constexpr double scHullEpsilon = 1e-9;
constexpr double scHullPointEpsilon = 1e-7;

struct AreaSets
{
	Array<int32> Parents;

	int32 Add()
	{
		const int32 area = static_cast<int32>(Parents.Size);
		Parents.Emplace(area);
		return area;
	}

	int32 Find(int32 area)
	{
		while (Parents[static_cast<size_t>(area)] != area) {
			Parents[static_cast<size_t>(area)] = Parents[static_cast<size_t>(Parents[static_cast<size_t>(area)])];
			area = Parents[static_cast<size_t>(area)];
		}

		return area;
	}

	void Join(int32 a, int32 b)
	{
		a = Find(a);
		b = Find(b);

		if (a != b) {
			Parents[static_cast<size_t>(std::max(a, b))] = std::min(a, b);
		}
	}
};

bool IsOpenLeaf(const Node& node) { return node.IsLeaf() && !node.bSolid && node.bReached; }

Vec3d GetCellPoint(const Polyhedron& cell)
{
	Vec3d sum = Vec3d(0.0);
	double count = 0.0;

	for (const PolyhedronSide& side : cell.Sides) {
		for (const Vec3d& point : side.Points) {
			sum = sum + point;
			count += 1.0;
		}
	}

	if (count == 0.0) {
		return (cell.Min + cell.Max) * 0.5;
	}

	return sum * (1.0 / count);
}

double Cross2(const Vec2d& origin, const Vec2d& a, const Vec2d& b)
{
	return (a.X - origin.X) * (b.Y - origin.Y) - (a.Y - origin.Y) * (b.X - origin.X);
}

Winding MergeConvex(const Array<Winding>& pieces, const Plane& plane)
{
	const Vec3d normal = plane.Normal;

	Vec3d axis = Vec3d(0.0);
	int32 smallest = 0;

	for (int32 i = 1; i < 3; i++) {
		if (std::abs(normal.mData[i]) < std::abs(normal.mData[smallest])) {
			smallest = i;
		}
	}

	axis.mData[smallest] = 1.0;

	const Vec3d u = axis.Cross(normal).Normalized();
	const Vec3d v = normal.Cross(u);

	Array<Vec2d> points;

	for (const Winding& piece : pieces) {
		for (const Vec3d& point : piece) {
			points.Emplace(Vec2d(u.Dot(point), v.Dot(point)));
		}
	}

	std::sort(points.begin(), points.end(),
			  [](const Vec2d& a, const Vec2d& b)
			  {
				  if (a.X != b.X) {
					  return a.X < b.X;
				  }

				  return a.Y < b.Y;
			  });

	Array<Vec2d> unique;

	for (const Vec2d& point : points) {
		if (unique.Size > 0 && std::abs(GetLast(unique).X - point.X) < scHullPointEpsilon &&
			std::abs(GetLast(unique).Y - point.Y) < scHullPointEpsilon) {
			continue;
		}

		unique.Emplace(point);
	}

	Winding hull;

	if (unique.Size < 3) {
		return hull;
	}

	Array<Vec2d> chain;

	for (int32 pass = 0; pass < 2; pass++) {
		const size_t start = chain.Size;

		for (size_t step = 0; step < unique.Size; step++) {
			size_t point_index = step;

			if (pass == 1) {
				point_index = unique.Size - 1 - step;
			}

			const Vec2d& point = unique[point_index];

			while (chain.Size >= start + 2 &&
				   Cross2(chain[chain.Size - 2], chain[chain.Size - 1], point) <= scHullEpsilon) {
				chain.RemoveLast();
			}

			chain.Emplace(point);
		}

		chain.RemoveLast();
	}

	if (chain.Size < 3) {
		return hull;
	}

	for (const Vec2d& point : chain) {
		hull.Emplace(normal * plane.Distance + u * point.X + v * point.Y);
	}

	return hull;
}

} // namespace

void AreaBuilder::Build(const PlaneSet& planes, Bsp& bsp, const MapSource& source, bool sealed)
{
	Stats = AreaStats {};
	Portals.Clear();
	Warnings.InitCapacity(source.PortalBrushCount + 1);

	Array<Node>& nodes = bsp.Nodes;

	for (Node& node : nodes) {
		node.Area = scNoMapArea;
	}

	if (!sealed) {
		if (source.PortalBrushCount > 0) {
			Warnings.Emplace(
				String::Fmt("{} portal brushes are ignored, areas need a sealed map", source.PortalBrushCount));
		}

		return;
	}

	AreaSets sets;
	Array<int32> queue;

	for (size_t i = 0; i < nodes.Size; i++) {
		if (!IsOpenLeaf(nodes[i]) || nodes[i].PortalBrush >= 0 || nodes[i].Area != scNoMapArea) {
			continue;
		}

		const int32 area = sets.Add();
		nodes[i].Area = area;

		queue.Clear();
		queue.Emplace(static_cast<int32>(i));

		uint32 queue_head = 0;

		while (queue_head < queue.Size) {
			const int32 leaf = queue[queue_head++];

			for (const Portal& portal : nodes[static_cast<size_t>(leaf)].Portals) {
				Node& other = nodes[static_cast<size_t>(portal.Leaf)];

				if (!IsOpenLeaf(other) || other.PortalBrush >= 0 || other.Area != scNoMapArea) {
					continue;
				}

				other.Area = area;
				queue.Emplace(portal.Leaf);
			}
		}
	}

	Array<Array<int32>> brush_leaves;

	for (size_t i = 0; i < source.Brushes.Size; i++) {
		brush_leaves.Emplace();
	}

	for (size_t i = 0; i < nodes.Size; i++) {
		if (IsOpenLeaf(nodes[i]) && nodes[i].PortalBrush >= 0) {
			brush_leaves[static_cast<size_t>(nodes[i].PortalBrush)].Emplace(static_cast<int32>(i));
		}
	}

	Array<int32> brush_portals;
	Fill(brush_portals, source.Brushes.Size, -1);

	Array<int32> neighbours;

	for (size_t brush_index = 0; brush_index < source.Brushes.Size; brush_index++) {
		const SourceBrush& brush = source.Brushes[brush_index];

		if (!brush.bPortal) {
			continue;
		}

		const Array<int32>& leaves = brush_leaves[brush_index];
		const Vec3d center = (brush.Shape.Min + brush.Shape.Max) * 0.5;

		if (leaves.Size == 0) {
			Warnings.Emplace(String::Fmt("portal brush '{}' at ({:.2f}, {:.2f}, {:.2f}) is buried in solid brushes or "
										 "outside the map, ignored",
										 brush.Name, center.X, center.Y, center.Z));
			Stats.IgnoredPortalCount++;
			continue;
		}

		neighbours.Clear();

		for (const int32 leaf : leaves) {
			for (const Portal& portal : nodes[static_cast<size_t>(leaf)].Portals) {
				const Node& other = nodes[static_cast<size_t>(portal.Leaf)];

				if (other.PortalBrush >= 0 || other.Area == scNoMapArea ||
					std::find(neighbours.begin(), neighbours.end(), other.Area) != neighbours.end()) {
					continue;
				}

				neighbours.Emplace(other.Area);
			}
		}

		std::sort(neighbours.begin(), neighbours.end());

		int32 area = scNoMapArea;

		if (neighbours.Size == 0) {
			area = sets.Add();
		}
		else {
			area = neighbours[0];
		}

		for (const int32 leaf : leaves) {
			nodes[static_cast<size_t>(leaf)].Area = area;
		}

		if (neighbours.Size == 2) {
			brush_portals[brush_index] = static_cast<int32>(Portals.Size);

			AreaPortal& portal = Portals.Emplace();
			portal.Brush = static_cast<int32>(brush_index);
			portal.BackArea = neighbours[0];
			portal.FrontArea = neighbours[1];
			continue;
		}

		Stats.IgnoredPortalCount++;

		if (neighbours.Size == 0) {
			Warnings.Emplace(String::Fmt("portal brush '{}' at ({:.2f}, {:.2f}, {:.2f}) only touches other portal "
										 "brushes, ignored",
										 brush.Name, center.X, center.Y, center.Z));
		}
		else if (neighbours.Size == 1) {
			Warnings.Emplace(String::Fmt("portal brush '{}' at ({:.2f}, {:.2f}, {:.2f}) does not separate two areas, "
										 "ignored. It does not fill the whole opening, or the space on both sides is "
										 "joined somewhere else",
										 brush.Name, center.X, center.Y, center.Z));
		}
		else {
			Warnings.Emplace(String::Fmt("portal brush '{}' at ({:.2f}, {:.2f}, {:.2f}) touches {} areas instead of "
										 "two, ignored",
										 brush.Name, center.X, center.Y, center.Z, neighbours.Size));
		}
	}

	const auto is_portal_crossing = [&](const Node& from, const Node& to)
	{
		if (from.PortalBrush < 0 || to.PortalBrush >= 0) {
			return false;
		}

		const int32 portal_index = brush_portals[static_cast<size_t>(from.PortalBrush)];

		return portal_index >= 0 && Portals[static_cast<size_t>(portal_index)].FrontArea == to.Area;
	};

	for (const Node& node : nodes) {
		if (!IsOpenLeaf(node) || node.Area == scNoMapArea) {
			continue;
		}

		for (const Portal& portal : node.Portals) {
			const Node& other = nodes[static_cast<size_t>(portal.Leaf)];

			if (!IsOpenLeaf(other) || other.Area == scNoMapArea || other.Area == node.Area ||
				is_portal_crossing(node, other) || is_portal_crossing(other, node)) {
				continue;
			}

			sets.Join(node.Area, other.Area);
		}
	}

	Array<int32> final_areas;
	Fill(final_areas, sets.Parents.Size, scNoMapArea);

	for (int32 area = 0; area < static_cast<int32>(sets.Parents.Size); area++) {
		const int32 root = sets.Find(area);

		if (root == area) {
			final_areas[static_cast<size_t>(area)] = static_cast<int32>(Stats.AreaCount++);
		}
		else {
			final_areas[static_cast<size_t>(area)] = final_areas[static_cast<size_t>(root)];
		}
	}

	for (Node& node : nodes) {
		if (node.Area != scNoMapArea) {
			node.Area = final_areas[static_cast<size_t>(node.Area)];
		}
	}

	Array<AreaPortal> kept_portals;

	for (AreaPortal& portal : Portals) {
		portal.BackArea = final_areas[static_cast<size_t>(portal.BackArea)];
		portal.FrontArea = final_areas[static_cast<size_t>(portal.FrontArea)];

		const SourceBrush& brush = source.Brushes[static_cast<size_t>(portal.Brush)];
		const Vec3d center = (brush.Shape.Min + brush.Shape.Max) * 0.5;

		if (portal.BackArea == portal.FrontArea) {
			Warnings.Emplace(String::Fmt("portal brush '{}' at ({:.2f}, {:.2f}, {:.2f}) does not separate two areas, "
										 "ignored. The space on both sides is joined somewhere else",
										 brush.Name, center.X, center.Y, center.Z));
			Stats.IgnoredPortalCount++;
			continue;
		}

		std::map<int32, Array<Winding>> pieces_on_plane;

		for (const int32 leaf : brush_leaves[static_cast<size_t>(portal.Brush)]) {
			for (const Portal& piece : nodes[static_cast<size_t>(leaf)].Portals) {
				const Node& other = nodes[static_cast<size_t>(piece.Leaf)];

				if (other.PortalBrush >= 0 || other.Area != portal.FrontArea) {
					continue;
				}

				int32 plane_index = piece.PlaneIndex;

				if (planes[plane_index].DistanceTo(GetCellPoint(other.Cell)) < 0.0) {
					plane_index = PlaneSet::GetOpposite(plane_index);
				}

				pieces_on_plane[plane_index].Emplace(CopyWinding(piece.Points));
			}
		}

		for (const auto& [plane_index, pieces] : pieces_on_plane) {
			Winding polygon = MergeConvex(pieces, planes[plane_index]);

			if (polygon.Size >= 3) {
				portal.Polygons.Emplace(std::move(polygon));
			}
		}

		if (portal.Polygons.Size == 0) {
			Warnings.Emplace(String::Fmt("portal brush '{}' at ({:.2f}, {:.2f}, {:.2f}) has no opening, ignored",
										 brush.Name, center.X, center.Y, center.Z));
			Stats.IgnoredPortalCount++;
			continue;
		}

		kept_portals.Emplace(std::move(portal));
	}

	Portals = std::move(kept_portals);
	Stats.PortalCount = static_cast<uint32>(Portals.Size);
}

int32 AreaBuilder::WriteNode(const PlaneSet& planes, const Bsp& bsp, int32 node_index,
							 Array<CompiledMapAreaNode>& out_nodes) const
{
	const Node& node = bsp.Nodes[static_cast<size_t>(node_index)];

	if (node.IsLeaf()) {
		return CompiledMap::MakeLeafChild(node.bSolid ? scNoMapArea : node.Area);
	}

	const int32 front = WriteNode(planes, bsp, node.Front, out_nodes);
	const int32 back = WriteNode(planes, bsp, node.Back, out_nodes);

	if (front == back && front < 0) {
		return front;
	}

	const Plane& plane = planes[node.PlaneIndex];

	CompiledMapAreaNode out_node;
	out_node.Plane[0] = static_cast<float32>(plane.Normal.X);
	out_node.Plane[1] = static_cast<float32>(plane.Normal.Y);
	out_node.Plane[2] = static_cast<float32>(plane.Normal.Z);
	out_node.Plane[3] = static_cast<float32>(plane.Distance);
	out_node.Front = front;
	out_node.Back = back;

	out_nodes.Emplace(out_node);

	return static_cast<int32>(out_nodes.Size - 1);
}

void AreaBuilder::Write(const PlaneSet& planes, const Bsp& bsp, const MapSource& source, CompiledMap& out_map)
{
	if (Stats.AreaCount == 0) {
		return;
	}

	Array<Vec3d> area_min;
	Array<Vec3d> area_max;
	Fill(area_min, Stats.AreaCount, Vec3d(DBL_MAX));
	Fill(area_max, Stats.AreaCount, Vec3d(-DBL_MAX));

	for (const Node& node : bsp.Nodes) {
		if (!node.IsLeaf() || node.bSolid || node.Area == scNoMapArea) {
			continue;
		}

		const size_t area = static_cast<size_t>(node.Area);

		area_min[area] = MinVec(area_min[area], node.Cell.Min);
		area_max[area] = MaxVec(area_max[area], node.Cell.Max);
	}

	out_map.Areas.InitSize(Stats.AreaCount);

	for (size_t i = 0; i < Stats.AreaCount; i++) {
		out_map.Areas[i].Bounds = BBox(area_min[i].ToVec3f(), area_max[i].ToVec3f());
	}

	out_map.AreaPortals.InitSize(Portals.Size);

	for (size_t i = 0; i < Portals.Size; i++) {
		const AreaPortal& portal = Portals[i];
		CompiledMapAreaPortal& out_portal = out_map.AreaPortals[i];

		out_portal.Name = source.Brushes[static_cast<size_t>(portal.Brush)].Name;
		out_portal.BackArea = static_cast<uint32>(portal.BackArea);
		out_portal.FrontArea = static_cast<uint32>(portal.FrontArea);

		size_t point_count = 0;

		for (const Winding& polygon : portal.Polygons) {
			point_count += polygon.Size;
		}

		out_portal.Points.InitSize(point_count * 3);
		out_portal.PolygonSizes.InitSize(portal.Polygons.Size);

		Vec3f bounds_min = portal.Polygons[0][0].ToVec3f();
		Vec3f bounds_max = bounds_min;

		size_t point_index = 0;

		for (size_t polygon = 0; polygon < portal.Polygons.Size; polygon++) {
			out_portal.PolygonSizes[polygon] = static_cast<uint32>(portal.Polygons[polygon].Size);

			for (const Vec3d& point : portal.Polygons[polygon]) {
				const Vec3f position = point.ToVec3f();

				out_portal.Points[point_index * 3] = position.X;
				out_portal.Points[point_index * 3 + 1] = position.Y;
				out_portal.Points[point_index * 3 + 2] = position.Z;

				bounds_min = Vec3f::Min(bounds_min, position);
				bounds_max = Vec3f::Max(bounds_max, position);

				point_index++;
			}
		}

		out_portal.Bounds = BBox(bounds_min, bounds_max);
	}

	Array<CompiledMapAreaNode> nodes;
	out_map.AreaRoot = WriteNode(planes, bsp, 0, nodes);
	out_map.AreaNodes = ArrayUtil::DynToSizedArray(nodes);

	Stats.NodeCount = static_cast<uint32>(nodes.Size);
}

} // namespace fx::mapc
