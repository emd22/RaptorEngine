/*
 * File:        Geometry.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: Double precision planes, windings and convex polyhedra for the map compiler
 */

#pragma once

#include "Containers.hpp"

#include <Core/Types.hpp>
#include <Math/DVec3.hpp>
#include <cmath>
#include <unordered_map>

namespace fx::mapc {

inline DVec3 MinVec(const DVec3& a, const DVec3& b)
{
	return DVec3(std::fmin(a.X, b.X), std::fmin(a.Y, b.Y), std::fmin(a.Z, b.Z));
}

inline DVec3 MaxVec(const DVec3& a, const DVec3& b)
{
	return DVec3(std::fmax(a.X, b.X), std::fmax(a.Y, b.Y), std::fmax(a.Z, b.Z));
}

constexpr double scOnEpsilon = 1e-5;

struct Plane
{
	DVec3 Normal = DVec3(0.0);
	double Distance = 0.0;

	double DistanceTo(const DVec3& point) const { return Normal.Dot(point) - Distance; }
};

using Winding = Array<DVec3>;

enum class eSide
{
	Front,
	Back,
	Both,
	On,
};

class PlaneSet
{
public:
	static constexpr int32 scNone = -1;

public:
	int32 FindOrAdd(DVec3 normal, double distance);

	const Plane& operator[](int32 index) const { return mPlanes[static_cast<size_t>(index)]; }
	int32 GetCount() const { return static_cast<int32>(mPlanes.Size); }

	static constexpr int32 GetOpposite(int32 index) { return index ^ 1; }
	static constexpr bool IsSamePair(int32 a, int32 b) { return (a >> 1) == (b >> 1); }

private:
	Array<Plane> mPlanes;
	std::unordered_multimap<int64, int32> mBuckets;
};

Winding MakeBaseWinding(const Plane& plane, double extent);

eSide SplitWinding(const Winding& winding, const Plane& plane, Winding& out_front, Winding& out_back,
				   double epsilon = scOnEpsilon);

void RemoveDuplicatePoints(Winding& winding);
bool IsWindingTiny(const Winding& winding);
double GetWindingArea(const Winding& winding);
DVec3 GetWindingCenter(const Winding& winding);
DVec3 GetWindingNormal(const Winding& winding);
Winding ReverseWinding(const Winding& winding);

Winding CopyWinding(const Winding& winding);

struct PolyhedronSide
{
	int32 PlaneIndex = PlaneSet::scNone;
	Winding Points;

	bool bOriginal = false;
	bool bUsed = false;

	PolyhedronSide Clone() const;
};

struct Polyhedron
{
	Array<PolyhedronSide> Sides;
	int32 Source = -1;

	DVec3 Min = DVec3(0.0);
	DVec3 Max = DVec3(0.0);

	Polyhedron Clone() const;

	void UpdateBounds();
	void MarkPlaneUsed(int32 plane_index);
	bool HasUnusedOriginalSide() const;
};

bool MakePolyhedron(const PlaneSet& planes, const Array<int32>& plane_indices, double extent,
					Polyhedron& out_polyhedron);

eSide SplitPolyhedron(const PlaneSet& planes, const Polyhedron& polyhedron, int32 plane_index, Polyhedron& out_front,
					  Polyhedron& out_back);

} // namespace fx::mapc
