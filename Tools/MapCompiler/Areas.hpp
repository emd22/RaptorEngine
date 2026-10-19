/*
 * File:        Areas.hpp
 * Author:      emd22
 * Created:     19/10/2026
 * Description: Splits a sealed map into areas joined by the portal brushes between them
 */

#pragma once

#include "Bsp.hpp"
#include "MapSource.hpp"

namespace fx::mapc {

struct AreaPortal
{
	int32 Brush = -1;
	int32 BackArea = -1;
	int32 FrontArea = -1;

	Array<Winding> Polygons;
};

struct AreaStats
{
	uint32 AreaCount = 0;
	uint32 PortalCount = 0;
	uint32 IgnoredPortalCount = 0;
	uint32 NodeCount = 0;
};

class AreaBuilder
{
public:
	void Build(const PlaneSet& planes, Bsp& bsp, const MapSource& source, bool sealed);
	void Write(const PlaneSet& planes, const Bsp& bsp, const MapSource& source, CompiledMap& out_map);

public:
	AreaStats Stats;
	Array<AreaPortal> Portals;
	SizedArray<String> Warnings;

private:
	int32 WriteNode(const PlaneSet& planes, const Bsp& bsp, int32 node_index,
					Array<CompiledMapAreaNode>& out_nodes) const;
};

} // namespace fx::mapc
