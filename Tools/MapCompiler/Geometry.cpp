#include "Geometry.hpp"

#include <algorithm>

namespace fx::mapc {

namespace {

constexpr double scNormalEpsilon = 1e-6;
constexpr double scDistanceEpsilon = 1e-4;
constexpr double scSnapEpsilon = 1e-7;
constexpr double scDistanceSnapGrid = 1024.0;
constexpr double scDistanceSnapEpsilon = 2e-6;
constexpr double scBucketsPerUnit = 64.0;
constexpr uint32 scBaseWindingCapacity = 8;

void SnapPlane(Vec3d& normal, double& distance)
{
	for (int32 axis = 0; axis < 3; axis++) {
		if (std::abs(normal.mData[axis]) < scSnapEpsilon) {
			normal.mData[axis] = 0.0;
		}
		else if (std::abs(std::abs(normal.mData[axis]) - 1.0) < scSnapEpsilon) {
			const double direction = (normal.mData[axis] < 0.0) ? -1.0 : 1.0;
			normal = Vec3d(0.0);
			normal.mData[axis] = direction;
			break;
		}
	}

	normal = normal * (1.0 / normal.Length());

	const double snapped = std::round(distance * scDistanceSnapGrid) / scDistanceSnapGrid;

	if (std::abs(distance - snapped) < scDistanceSnapEpsilon) {
		distance = snapped;
	}
}

bool IsSamePlane(const Plane& plane, const Vec3d& normal, double distance)
{
	return std::abs(plane.Distance - distance) < scDistanceEpsilon &&
		   std::abs(plane.Normal.X - normal.X) < scNormalEpsilon &&
		   std::abs(plane.Normal.Y - normal.Y) < scNormalEpsilon &&
		   std::abs(plane.Normal.Z - normal.Z) < scNormalEpsilon;
}

void SnapPointToAxialPlane(Vec3d& point, const Plane& plane)
{
	for (int32 axis = 0; axis < 3; axis++) {
		if (plane.Normal.mData[axis] == 1.0) {
			point.mData[axis] = plane.Distance;
		}
		else if (plane.Normal.mData[axis] == -1.0) {
			point.mData[axis] = -plane.Distance;
		}
	}
}

} // namespace

int32 PlaneSet::FindOrAdd(Vec3d normal, double distance)
{
	SnapPlane(normal, distance);

	double sign = 1.0;

	for (int32 axis = 0; axis < 3; axis++) {
		if (normal.mData[axis] != 0.0) {
			sign = (normal.mData[axis] < 0.0) ? -1.0 : 1.0;
			break;
		}
	}

	const Vec3d positive_normal = normal * sign;
	const double positive_distance = distance * sign;

	const int32 flipped = (sign < 0.0) ? 1 : 0;
	const int64 bucket = static_cast<int64>(std::floor(positive_distance * scBucketsPerUnit));

	for (int64 offset = -1; offset <= 1; offset++) {
		const auto range = mBuckets.equal_range(bucket + offset);

		for (auto it = range.first; it != range.second; ++it) {
			if (IsSamePlane(mPlanes[static_cast<size_t>(it->second)], positive_normal, positive_distance)) {
				return it->second + flipped;
			}
		}
	}

	const int32 index = static_cast<int32>(mPlanes.Size);

	mPlanes.Emplace(Plane { positive_normal, positive_distance });
	mPlanes.Emplace(Plane { positive_normal * -1.0, -positive_distance });
	mBuckets.emplace(bucket, index);

	return index + flipped;
}

Winding MakeBaseWinding(const Plane& plane, double extent)
{
	int32 major_axis = 0;

	for (int32 axis = 1; axis < 3; axis++) {
		if (std::abs(plane.Normal.mData[axis]) > std::abs(plane.Normal.mData[major_axis])) {
			major_axis = axis;
		}
	}

	Vec3d up = (major_axis == 1) ? Vec3d(1.0, 0.0, 0.0) : Vec3d(0.0, 1.0, 0.0);

	up = up - plane.Normal * up.Dot(plane.Normal);
	up = up * (extent / up.Length());

	const Vec3d right = up.Cross(plane.Normal);
	const Vec3d origin = plane.Normal * plane.Distance;

	Winding winding;
	winding.SetPageSize(scBaseWindingCapacity);

	winding.Emplace(origin - right + up);
	winding.Emplace(origin - right - up);
	winding.Emplace(origin + right - up);
	winding.Emplace(origin + right + up);

	return winding;
}

eSide SplitWinding(const Winding& winding, const Plane& plane, Winding& out_front, Winding& out_back, double epsilon)
{
	out_front.Clear();
	out_back.Clear();

	const size_t count = winding.Size;

	Array<double> distances;
	Array<int8> sides;

	Fill(distances, static_cast<uint32>(count), 0.0);
	Fill(sides, static_cast<uint32>(count), int8(0));

	size_t front_count = 0;
	size_t back_count = 0;

	for (size_t i = 0; i < count; i++) {
		distances[i] = plane.DistanceTo(winding[i]);

		if (distances[i] > epsilon) {
			sides[i] = 1;
			front_count++;
		}
		else if (distances[i] < -epsilon) {
			sides[i] = -1;
			back_count++;
		}
		else {
			sides[i] = 0;
		}
	}

	if (front_count == 0 && back_count == 0) {
		return eSide::On;
	}

	if (back_count == 0) {
		out_front = CopyWinding(winding);
		return eSide::Front;
	}

	if (front_count == 0) {
		out_back = CopyWinding(winding);
		return eSide::Back;
	}

	for (size_t i = 0; i < count; i++) {
		const size_t next = (i + 1) % count;
		const Vec3d& point = winding[i];

		if (sides[i] == 0) {
			out_front.Emplace(point);
			out_back.Emplace(point);
			continue;
		}

		if (sides[i] > 0) {
			out_front.Emplace(point);
		}
		else {
			out_back.Emplace(point);
		}

		if (sides[next] == 0 || sides[next] == sides[i]) {
			continue;
		}

		const Vec3d& next_point = winding[next];

		const bool forward = (sides[i] > 0);
		const Vec3d& from = forward ? point : next_point;
		const Vec3d& to = forward ? next_point : point;
		const double from_distance = forward ? distances[i] : distances[next];
		const double to_distance = forward ? distances[next] : distances[i];

		Vec3d middle = from + (to - from) * (from_distance / (from_distance - to_distance));
		SnapPointToAxialPlane(middle, plane);

		out_front.Emplace(middle);
		out_back.Emplace(middle);
	}

	RemoveDuplicatePoints(out_front);
	RemoveDuplicatePoints(out_back);

	const bool has_front = !IsWindingTiny(out_front);
	const bool has_back = !IsWindingTiny(out_back);

	if (has_front && has_back) {
		return eSide::Both;
	}

	if (has_front || (!has_back && front_count >= back_count)) {
		out_front = CopyWinding(winding);
		out_back.Clear();
		return eSide::Front;
	}

	out_back = CopyWinding(winding);
	out_front.Clear();
	return eSide::Back;
}

void RemoveDuplicatePoints(Winding& winding)
{
	Winding result;

	for (const Vec3d& point : winding) {
		if (result.Size != 0 && (point - GetLast(result)).Length() < scPointEpsilon) {
			continue;
		}

		result.Emplace(point);
	}

	while (result.Size > 1 && (result[0] - GetLast(result)).Length() < scPointEpsilon) {
		result.RemoveLast();
	}

	winding = std::move(result);
}

bool IsWindingTiny(const Winding& winding)
{
	if (winding.Size < 3) {
		return true;
	}

	uint32 long_edges = 0;

	for (size_t i = 0; i < winding.Size; i++) {
		if ((winding[(i + 1) % winding.Size] - winding[i]).Length() > scMinEdgeLength) {
			long_edges++;
		}
	}

	return long_edges < 3 || GetWindingArea(winding) < scMinArea;
}

static Vec3d GetAreaVector(const Winding& winding)
{
	Vec3d sum = Vec3d(0.0);

	for (size_t i = 2; i < winding.Size; i++) {
		sum = sum + (winding[i - 1] - winding[0]).Cross(winding[i] - winding[0]);
	}

	return sum * 0.5;
}

double GetWindingArea(const Winding& winding) { return GetAreaVector(winding).Length(); }

Vec3d GetWindingNormal(const Winding& winding)
{
	const Vec3d area = GetAreaVector(winding);
	const double length = area.Length();

	return (length > 0.0) ? area * (1.0 / length) : Vec3d(0.0);
}

Vec3d GetWindingCenter(const Winding& winding)
{
	Vec3d sum = Vec3d(0.0);

	for (const Vec3d& point : winding) {
		sum = sum + point;
	}

	return winding.Size == 0 ? sum : sum * (1.0 / static_cast<double>(winding.Size));
}

Winding ReverseWinding(const Winding& winding)
{
	Winding reversed;

	for (uint32 i = winding.Size; i > 0; i--) {
		reversed.Emplace(winding[i - 1]);
	}

	return reversed;
}

Winding CopyWinding(const Winding& winding) { return CopyArray(winding); }

PolyhedronSide PolyhedronSide::Clone() const
{
	return PolyhedronSide { PlaneIndex, CopyWinding(Points), bOriginal, bUsed };
}

Polyhedron Polyhedron::Clone() const
{
	Polyhedron copy;
	copy.Source = Source;
	copy.Min = Min;
	copy.Max = Max;

	for (const PolyhedronSide& side : Sides) {
		copy.Sides.Emplace(side.Clone());
	}

	return copy;
}

void Polyhedron::UpdateBounds()
{
	Min = Vec3d(1e30, 1e30, 1e30);
	Max = Vec3d(-1e30, -1e30, -1e30);

	for (const PolyhedronSide& side : Sides) {
		for (const Vec3d& point : side.Points) {
			Min = MinVec(Min, point);
			Max = MaxVec(Max, point);
		}
	}
}

void Polyhedron::MarkPlaneUsed(int32 plane_index)
{
	for (PolyhedronSide& side : Sides) {
		if (PlaneSet::IsSamePair(side.PlaneIndex, plane_index)) {
			side.bUsed = true;
		}
	}
}

bool Polyhedron::HasUnusedOriginalSide() const
{
	for (const PolyhedronSide& side : Sides) {
		if (side.bOriginal && !side.bUsed) {
			return true;
		}
	}

	return false;
}

bool MakePolyhedron(const PlaneSet& planes, const Array<int32>& plane_indices, double extent,
					Polyhedron& out_polyhedron)
{
	out_polyhedron.Sides.Clear();

	Winding front;
	Winding back;

	for (size_t i = 0; i < plane_indices.Size; i++) {
		const int32 plane_index = plane_indices[i];

		bool duplicate = false;

		for (size_t j = 0; j < i; j++) {
			duplicate = duplicate || (plane_indices[j] == plane_index);
		}

		if (duplicate) {
			continue;
		}

		Winding winding = MakeBaseWinding(planes[plane_index], extent);

		for (size_t j = 0; j < plane_indices.Size && winding.Size != 0; j++) {
			if (plane_indices[j] == plane_index) {
				continue;
			}

			if (plane_indices[j] == PlaneSet::GetOpposite(plane_index)) {
				winding.Clear();
				break;
			}

			const eSide side = SplitWinding(winding, planes[plane_indices[j]], front, back);

			if (side == eSide::Front || (side == eSide::On && j < i)) {
				winding.Clear();
			}
			else if (side == eSide::Both) {
				winding = std::move(back);
			}
		}

		if (IsWindingTiny(winding)) {
			continue;
		}

		out_polyhedron.Sides.Emplace(PolyhedronSide {
			.PlaneIndex = plane_index,
			.Points = std::move(winding),
			.bOriginal = true,
			.bUsed = false,
		});
	}

	out_polyhedron.UpdateBounds();

	return out_polyhedron.Sides.Size >= 4;
}

eSide SplitPolyhedron(const PlaneSet& planes, const Polyhedron& polyhedron, int32 plane_index, Polyhedron& out_front,
					  Polyhedron& out_back)
{
	const Plane& plane = planes[plane_index];

	double max_front = 0.0;
	double max_back = 0.0;

	for (const PolyhedronSide& side : polyhedron.Sides) {
		if (PlaneSet::IsSamePair(side.PlaneIndex, plane_index)) {
			continue;
		}

		for (const Vec3d& point : side.Points) {
			const double distance = plane.DistanceTo(point);

			max_front = std::fmax(max_front, distance);
			max_back = std::fmax(max_back, -distance);
		}
	}

	if (max_front <= scOnEpsilon) {
		return eSide::Back;
	}

	if (max_back <= scOnEpsilon) {
		return eSide::Front;
	}

	const eSide larger_side = (max_front >= max_back) ? eSide::Front : eSide::Back;

	Winding cap = MakeBaseWinding(plane, (polyhedron.Max - polyhedron.Min).Length() * 2.0 + std::abs(plane.Distance) +
											 polyhedron.Max.Length() + polyhedron.Min.Length() + 16.0);
	Winding front;
	Winding back;

	for (const PolyhedronSide& side : polyhedron.Sides) {
		if (PlaneSet::IsSamePair(side.PlaneIndex, plane_index)) {
			return larger_side;
		}

		const eSide result = SplitWinding(cap, planes[side.PlaneIndex], front, back);

		if (result == eSide::Front || result == eSide::On) {
			return larger_side;
		}

		if (result == eSide::Both) {
			cap = std::move(back);
		}
	}

	if (IsWindingTiny(cap)) {
		return larger_side;
	}

	out_front.Sides.Clear();
	out_back.Sides.Clear();
	out_front.Source = polyhedron.Source;
	out_back.Source = polyhedron.Source;

	for (const PolyhedronSide& side : polyhedron.Sides) {
		const eSide result = SplitWinding(side.Points, plane, front, back);

		if (result == eSide::Front || result == eSide::Both) {
			out_front.Sides.Emplace(PolyhedronSide { side.PlaneIndex, std::move(front), side.bOriginal, side.bUsed });
		}

		if (result == eSide::Back || result == eSide::Both) {
			out_back.Sides.Emplace(PolyhedronSide { side.PlaneIndex, std::move(back), side.bOriginal, side.bUsed });
		}
	}

	if (out_front.Sides.Size < 3 || out_back.Sides.Size < 3) {
		return larger_side;
	}

	out_front.Sides.Emplace(PolyhedronSide { PlaneSet::GetOpposite(plane_index), ReverseWinding(cap), false, true });
	out_back.Sides.Emplace(PolyhedronSide { plane_index, std::move(cap), false, true });

	out_front.UpdateBounds();
	out_back.UpdateBounds();

	return eSide::Both;
}

} // namespace fx::mapc
