#include "Bsp.hpp"

#include <algorithm>

namespace fx::mapc {

namespace {

constexpr double scBoundsMargin = 8.0;
constexpr int32 scSplitPenalty = 8;
constexpr int32 scAxialBonus = 4;

struct BuildTask
{
	int32 NodeIndex = 0;
	uint32 Depth = 0;
	Array<Polyhedron> Fragments;
	Array<Polyhedron> PortalFragments;
	Polyhedron Cell;
	int32 PortalBrush = -1;
};

bool IsAxial(const Plane& plane)
{
	return std::abs(plane.Normal.X) == 1.0 || std::abs(plane.Normal.Y) == 1.0 || std::abs(plane.Normal.Z) == 1.0;
}

bool ResolveEnclosed(BuildTask& task)
{
	for (const Polyhedron& fragment : task.Fragments) {
		if (!fragment.HasUnusedOriginalSide()) {
			return true;
		}
	}

	for (const Polyhedron& fragment : task.PortalFragments) {
		if (!fragment.HasUnusedOriginalSide()) {
			task.PortalBrush = fragment.Source;
			task.PortalFragments.Clear();
			break;
		}
	}

	return false;
}

void SplitFragments(const PlaneSet& planes, Array<Polyhedron>& fragments, int32 split_plane,
					Array<Polyhedron>& out_front, Array<Polyhedron>& out_back)
{
	Polyhedron front;
	Polyhedron back;

	for (Polyhedron& fragment : fragments) {
		const eSide side = SplitPolyhedron(planes, fragment, split_plane, front, back);

		if (side == eSide::Both) {
			out_front.Emplace(std::move(front));
			out_back.Emplace(std::move(back));
			continue;
		}

		fragment.MarkPlaneUsed(split_plane);

		if (side == eSide::Front) {
			out_front.Emplace(std::move(fragment));
		}
		else {
			out_back.Emplace(std::move(fragment));
		}
	}
}

} // namespace

int32 Bsp::ChooseSplitPlane(const Array<Polyhedron>& fragments, const Array<Polyhedron>& portal_fragments) const
{
	const PlaneSet& planes = *mpPlanes;

	const auto for_each_fragment = [&](const auto& func)
	{
		for (const Polyhedron& fragment : fragments) {
			func(fragment);
		}

		for (const Polyhedron& fragment : portal_fragments) {
			func(fragment);
		}
	};

	Array<int32> candidates;

	for_each_fragment(
		[&](const Polyhedron& fragment)
		{
			for (const PolyhedronSide& side : fragment.Sides) {
				if (side.bOriginal && !side.bUsed) {
					candidates.Emplace(side.PlaneIndex & ~1);
				}
			}
		});

	std::sort(candidates.begin(), candidates.end());

	uint32 unique_count = 0;

	for (uint32 i = 0; i < candidates.Size; i++) {
		if (i == 0 || candidates[i] != candidates[i - 1]) {
			candidates[unique_count++] = candidates[i];
		}
	}

	while (candidates.Size > unique_count) {
		candidates.RemoveLast();
	}

	int32 best_plane = PlaneSet::scNone;
	int64 best_score = 0;

	for (const int32 candidate : candidates) {
		const Plane& plane = planes[candidate];

		int64 front_count = 0;
		int64 back_count = 0;
		int64 split_count = 0;

		for_each_fragment(
			[&](const Polyhedron& fragment)
			{
				const Vec3d center = (fragment.Min + fragment.Max) * 0.5;
				const Vec3d half = (fragment.Max - fragment.Min) * 0.5;

				const double radius = std::abs(plane.Normal.X) * half.X + std::abs(plane.Normal.Y) * half.Y +
									  std::abs(plane.Normal.Z) * half.Z;
				const double distance = plane.DistanceTo(center);

				if (distance - radius >= -scOnEpsilon) {
					front_count++;
				}
				else if (distance + radius <= scOnEpsilon) {
					back_count++;
				}
				else {
					split_count++;
				}
			});

		int64 score = -(split_count * scSplitPenalty) - std::abs(front_count - back_count);

		if (IsAxial(plane)) {
			score += scAxialBonus;
		}

		if (best_plane == PlaneSet::scNone || score > best_score) {
			best_plane = candidate;
			best_score = score;
		}
	}

	return best_plane;
}

void Bsp::Build(PlaneSet& planes, Array<Polyhedron> brushes, Array<Polyhedron> portal_brushes, const Vec3d& min,
				const Vec3d& max)
{
	mpPlanes = &planes;

	Nodes.Clear();
	SolidLeafCount = 0;
	EmptyLeafCount = 0;
	PortalCount = 0;
	MaxDepth = 0;

	const Vec3d margin(scBoundsMargin, scBoundsMargin, scBoundsMargin);
	const Vec3d outer_min = min - margin;
	const Vec3d outer_max = max + margin;

	Array<int32> bounds_planes;

	for (int32 axis = 0; axis < 3; axis++) {
		Vec3d normal = Vec3d(0.0);
		normal.mData[axis] = 1.0;

		mBoundsPlanes[axis * 2] = planes.FindOrAdd(normal, outer_max.mData[axis]);
		mBoundsPlanes[axis * 2 + 1] = planes.FindOrAdd(normal * -1.0, -outer_min.mData[axis]);

		bounds_planes.Emplace(mBoundsPlanes[axis * 2]);
		bounds_planes.Emplace(mBoundsPlanes[axis * 2 + 1]);
	}

	BuildTask root;
	MakePolyhedron(planes, bounds_planes, (outer_max - outer_min).Length() * 4.0 + 1024.0, root.Cell);

	for (PolyhedronSide& side : root.Cell.Sides) {
		side.bOriginal = false;
		side.bUsed = true;
	}

	root.Fragments = std::move(brushes);
	root.PortalFragments = std::move(portal_brushes);

	Nodes.Emplace();

	Array<BuildTask> stack;
	stack.Emplace(std::move(root));

	while (stack.Size != 0) {
		BuildTask task = std::move(GetLast(stack));
		stack.RemoveLast();

		MaxDepth = std::max(MaxDepth, task.Depth);

		bool is_solid = ResolveEnclosed(task);
		bool is_leaf = is_solid || (task.Fragments.Size == 0 && task.PortalFragments.Size == 0);

		BuildTask front_task;
		BuildTask back_task;
		int32 split_plane = PlaneSet::scNone;

		while (!is_leaf) {
			split_plane = ChooseSplitPlane(task.Fragments, task.PortalFragments);

			if (split_plane == PlaneSet::scNone) {
				is_leaf = true;
				is_solid = task.Fragments.Size != 0;
				break;
			}

			const eSide cell_side = SplitPolyhedron(planes, task.Cell, split_plane, front_task.Cell, back_task.Cell);

			if (cell_side == eSide::Both) {
				break;
			}

			for (Polyhedron& fragment : task.Fragments) {
				fragment.MarkPlaneUsed(split_plane);
			}

			for (Polyhedron& fragment : task.PortalFragments) {
				fragment.MarkPlaneUsed(split_plane);
			}

			is_solid = ResolveEnclosed(task);
			is_leaf = is_solid || (task.Fragments.Size == 0 && task.PortalFragments.Size == 0);
		}

		if (is_leaf) {
			Node& node = Nodes[static_cast<size_t>(task.NodeIndex)];
			node.bSolid = is_solid;
			node.PortalBrush = is_solid ? -1 : task.PortalBrush;
			node.Cell = std::move(task.Cell);

			if (is_solid) {
				SolidLeafCount++;
			}
			else {
				EmptyLeafCount++;
			}

			continue;
		}

		SplitFragments(planes, task.Fragments, split_plane, front_task.Fragments, back_task.Fragments);
		SplitFragments(planes, task.PortalFragments, split_plane, front_task.PortalFragments,
					   back_task.PortalFragments);

		front_task.NodeIndex = static_cast<int32>(Nodes.Size);
		back_task.NodeIndex = front_task.NodeIndex + 1;
		front_task.Depth = task.Depth + 1;
		back_task.Depth = task.Depth + 1;
		front_task.PortalBrush = task.PortalBrush;
		back_task.PortalBrush = task.PortalBrush;

		Nodes.Emplace();
		Nodes.Emplace();

		Node& node = Nodes[static_cast<size_t>(task.NodeIndex)];
		node.PlaneIndex = split_plane;
		node.Front = front_task.NodeIndex;
		node.Back = back_task.NodeIndex;

		stack.Emplace(std::move(front_task));
		stack.Emplace(std::move(back_task));
	}
}

int32 Bsp::FindLeaf(const Vec3d& point) const
{
	int32 index = 0;

	while (!Nodes[static_cast<size_t>(index)].IsLeaf()) {
		const Node& node = Nodes[static_cast<size_t>(index)];
		index = ((*mpPlanes)[node.PlaneIndex].DistanceTo(point) >= 0.0) ? node.Front : node.Back;
	}

	return index;
}

void Bsp::FilterWinding(const Winding& winding, int32 plane_index, Array<LeafFragment>& out_fragments) const
{
	const PlaneSet& planes = *mpPlanes;
	const Vec3d normal = planes[plane_index].Normal;

	Array<LeafFragment> stack;
	stack.Emplace(LeafFragment { 0, CopyWinding(winding) });

	Winding front;
	Winding back;

	while (stack.Size != 0) {
		LeafFragment item = std::move(GetLast(stack));
		stack.RemoveLast();

		const Node& node = Nodes[static_cast<size_t>(item.Leaf)];

		if (node.IsLeaf()) {
			out_fragments.Emplace(std::move(item));
			continue;
		}

		if (node.PlaneIndex == plane_index) {
			stack.Emplace(LeafFragment { node.Front, std::move(item.Points) });
			continue;
		}

		if (node.PlaneIndex == PlaneSet::GetOpposite(plane_index)) {
			stack.Emplace(LeafFragment { node.Back, std::move(item.Points) });
			continue;
		}

		const Plane& plane = planes[node.PlaneIndex];
		const eSide side = SplitWinding(item.Points, plane, front, back);

		if (side == eSide::On) {
			const int32 child = (plane.Normal.Dot(normal) > 0.0) ? node.Front : node.Back;
			stack.Emplace(LeafFragment { child, std::move(item.Points) });
			continue;
		}

		if (side == eSide::Front || side == eSide::Both) {
			stack.Emplace(LeafFragment { node.Front, std::move(front) });
		}

		if (side == eSide::Back || side == eSide::Both) {
			stack.Emplace(LeafFragment { node.Back, std::move(back) });
		}
	}
}

void Bsp::BuildPortals()
{
	PortalCount = 0;

	Array<LeafFragment> fragments;

	for (size_t i = 0; i < Nodes.Size; i++) {
		if (!Nodes[i].IsLeaf() || Nodes[i].bSolid) {
			continue;
		}

		Array<Portal> portals;
		bool is_outside = false;

		for (const PolyhedronSide& side : Nodes[i].Cell.Sides) {
			bool is_bounds_plane = false;

			for (const int32 bounds_plane : mBoundsPlanes) {
				is_bounds_plane = is_bounds_plane || (side.PlaneIndex == bounds_plane);
			}

			if (is_bounds_plane) {
				is_outside = true;
				continue;
			}

			fragments.Clear();
			FilterWinding(side.Points, side.PlaneIndex, fragments);

			for (LeafFragment& fragment : fragments) {
				const Node& other = Nodes[static_cast<size_t>(fragment.Leaf)];

				if (other.bSolid || static_cast<size_t>(fragment.Leaf) == i || IsWindingTiny(fragment.Points)) {
					continue;
				}

				portals.Emplace(Portal { fragment.Leaf, side.PlaneIndex, std::move(fragment.Points) });
			}
		}

		PortalCount += static_cast<uint32>(portals.Size);

		Nodes[i].bOutside = is_outside;
		Nodes[i].Portals = std::move(portals);
	}
}

FloodResult Bsp::FloodFill(const Array<Vec3d>& entities)
{
	FloodResult result;

	struct Visit
	{
		int32 Previous = Node::scNone;
		Vec3d Through = Vec3d(0.0);
		bool bVisited = false;
	};

	Array<Visit> visits;
	Fill(visits, Nodes.Size, Visit {});

	Array<int32> queue;
	uint32 queue_head = 0;

	for (Node& node : Nodes) {
		node.bReached = false;
	}

	for (const Vec3d& entity : entities) {
		const int32 leaf = FindLeaf(entity);

		if (Nodes[static_cast<size_t>(leaf)].bSolid) {
			result.EntitiesInSolid++;
			continue;
		}

		result.EntitiesUsed++;

		Visit& visit = visits[static_cast<size_t>(leaf)];

		if (!visit.bVisited) {
			visit.bVisited = true;
			visit.Through = entity;
			queue.Emplace(leaf);
		}
	}

	int32 leak_leaf = Node::scNone;

	while (queue_head < queue.Size) {
		const int32 leaf = queue[queue_head++];

		Node& node = Nodes[static_cast<size_t>(leaf)];
		node.bReached = true;

		if (node.bOutside && leak_leaf == Node::scNone) {
			leak_leaf = leaf;
		}

		for (const Portal& portal : node.Portals) {
			Visit& visit = visits[static_cast<size_t>(portal.Leaf)];

			if (visit.bVisited) {
				continue;
			}

			visit.bVisited = true;
			visit.Previous = leaf;
			visit.Through = GetWindingCenter(portal.Points);
			queue.Emplace(portal.Leaf);
		}
	}

	result.bSealed = (leak_leaf == Node::scNone) && (result.EntitiesUsed > 0);

	if (leak_leaf != Node::scNone) {
		const Node& outside = Nodes[static_cast<size_t>(leak_leaf)];
		result.LeakPath.Emplace((outside.Cell.Min + outside.Cell.Max) * 0.5);

		for (int32 leaf = leak_leaf; leaf != Node::scNone; leaf = visits[static_cast<size_t>(leaf)].Previous) {
			result.LeakPath.Emplace(visits[static_cast<size_t>(leaf)].Through);
		}

		std::reverse(result.LeakPath.begin(), result.LeakPath.end());
	}

	return result;
}

} // namespace fx::mapc
