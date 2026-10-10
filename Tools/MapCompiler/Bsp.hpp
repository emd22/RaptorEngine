/*
 * File:        Bsp.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: Solid/empty BSP tree built from brushes, with portals between its empty leaves
 */

#pragma once

#include "Geometry.hpp"

namespace fx::mapc {

struct Portal
{
	int32 Leaf = -1;
	Winding Points;
};

struct Node
{
	static constexpr int32 scNone = -1;

	int32 PlaneIndex = PlaneSet::scNone;
	int32 Front = scNone;
	int32 Back = scNone;

	bool bSolid = false;
	bool bOutside = false;
	bool bReached = false;

	Polyhedron Cell;
	Array<Portal> Portals;

	bool IsLeaf() const { return PlaneIndex == PlaneSet::scNone; }
};

struct LeafFragment
{
	int32 Leaf = Node::scNone;
	Winding Points;
};

struct FloodResult
{
	bool bSealed = false;
	uint32 EntitiesInSolid = 0;
	uint32 EntitiesUsed = 0;
	Array<Vec3d> LeakPath;
};

class Bsp
{
public:
	/**
	 * @param brushes The solids to build the tree from. They are taken apart while it is built.
	 */
	void Build(PlaneSet& planes, Array<Polyhedron> brushes, const Vec3d& min, const Vec3d& max);

	int32 FindLeaf(const Vec3d& point) const;

	void FilterWinding(const Winding& winding, int32 plane_index, Array<LeafFragment>& out_fragments) const;

	void BuildPortals();
	FloodResult FloodFill(const Array<Vec3d>& entities);

public:
	Array<Node> Nodes;

	uint32 SolidLeafCount = 0;
	uint32 EmptyLeafCount = 0;
	uint32 PortalCount = 0;
	uint32 MaxDepth = 0;

private:
	int32 ChooseSplitPlane(const Array<Polyhedron>& fragments) const;

private:
	const PlaneSet* mpPlanes = nullptr;
	int32 mBoundsPlanes[6] = {};
};

} // namespace fx::mapc
