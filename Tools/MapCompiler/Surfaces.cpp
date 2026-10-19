#include "Surfaces.hpp"

#include <Core/ArrayUtil.hpp>
#include <Math/Vec2.hpp>
#include <algorithm>
#include <map>
#include <tuple>
#include <unordered_map>

namespace fx::mapc {

namespace {

constexpr double scFullFaceTolerance = 1e-9;
constexpr double scGradientEpsilon = 1e-6;
constexpr double scOffsetEpsilon = 1e-5;
constexpr double scCollinearEpsilon = 2e-6;
constexpr double scWeldEpsilon = 2e-5;
constexpr double scWeldCellSize = 1e-3;
constexpr double scOnEdgeEpsilon = 1e-5;
constexpr double scEdgeGridCellSize = 2.0;
constexpr int64 scMaxEdgeGridCells = 4096;
constexpr double scMinTangentDeterminant = 1e-20;
constexpr double scAreaTolerance = 1e-6;
constexpr double scAreaProbeDistance = 1e-3;

struct FacePiece
{
	Winding Points;
	int32 Area = scNoMapArea;
};

struct Face
{
	int32 PlaneIndex = PlaneSet::scNone;
	int32 MaterialIndex = 0;
	int32 MapIndex = 0;

	Winding Original;
	Vec3d Min = Vec3d(0.0);
	Vec3d Max = Vec3d(0.0);

	Array<FacePiece> Visible;
};

struct CellKey
{
	int64 X = 0;
	int64 Y = 0;
	int64 Z = 0;

	bool operator==(const CellKey& other) const { return X == other.X && Y == other.Y && Z == other.Z; }
};

struct CellKeyHash
{
	size_t operator()(const CellKey& key) const
	{
		uint64 hash = static_cast<uint64>(key.X) * 0x9E3779B97F4A7C15ull;
		hash ^= static_cast<uint64>(key.Y) * 0xC2B2AE3D27D4EB4Full + (hash << 6) + (hash >> 2);
		hash ^= static_cast<uint64>(key.Z) * 0x165667B19E3779F9ull + (hash << 6) + (hash >> 2);
		return static_cast<size_t>(hash);
	}
};

CellKey GetCell(const Vec3d& point, double cell_size)
{
	return CellKey { static_cast<int64>(std::floor(point.X / cell_size)),
					 static_cast<int64>(std::floor(point.Y / cell_size)),
					 static_cast<int64>(std::floor(point.Z / cell_size)) };
}

class VertexWelder
{
public:
	int32 Add(const Vec3d& point)
	{
		const CellKey center = GetCell(point, scWeldCellSize);

		for (int64 x = -1; x <= 1; x++) {
			for (int64 y = -1; y <= 1; y++) {
				for (int64 z = -1; z <= 1; z++) {
					const auto found = mCells.find(CellKey { center.X + x, center.Y + y, center.Z + z });

					if (found == mCells.end()) {
						continue;
					}

					for (const int32 index : found->second) {
						if ((Points[static_cast<size_t>(index)] - point).Length() < scWeldEpsilon) {
							return index;
						}
					}
				}
			}
		}

		const int32 index = static_cast<int32>(Points.Size);

		Points.Emplace(point);
		mCells[center].Emplace(index);

		return index;
	}

public:
	Array<Vec3d> Points;

private:
	std::unordered_map<CellKey, Array<int32>, CellKeyHash> mCells;
};

bool SubtractConvex(const Winding& piece, const Winding& cutter, const Vec3d& normal, Array<Winding>& out_pieces)
{
	Array<Winding> outside;

	Winding rest = CopyWinding(piece);
	Winding front;
	Winding back;

	for (size_t i = 0; i < cutter.Size && rest.Size != 0; i++) {
		const Vec3d& from = cutter[i];
		const Vec3d& to = cutter[(i + 1) % cutter.Size];

		const Vec3d edge_normal = (to - from).Cross(normal);
		const double length = edge_normal.Length();

		if (length <= 0.0) {
			continue;
		}

		Plane edge_plane;
		edge_plane.Normal = edge_normal * (1.0 / length);
		edge_plane.Distance = edge_plane.Normal.Dot(from);

		const eSide side = SplitWinding(rest, edge_plane, front, back);

		if (side == eSide::Front) {
			return false;
		}

		if (side == eSide::Both) {
			outside.Emplace(std::move(front));
			rest = std::move(back);
		}
	}

	if (IsWindingTiny(rest)) {
		return false;
	}

	for (Winding& winding : outside) {
		out_pieces.Emplace(std::move(winding));
	}

	return true;
}

struct TangentFrame
{
	Vec3d Tangent = Vec3d(0.0);
	double Handedness = 1.0;
};

TangentFrame GetTangentFrame(const Vec3d& normal, const TextureMap& map)
{
	const Vec3d u_gradient = map.GradientU - normal * normal.Dot(map.GradientU);
	const Vec3d v_gradient = map.GradientV - normal * normal.Dot(map.GradientV);

	const double uu = u_gradient.Dot(u_gradient);
	const double uv = u_gradient.Dot(v_gradient);
	const double vv = v_gradient.Dot(v_gradient);
	const double determinant = uu * vv - uv * uv;

	TangentFrame frame;

	if (std::abs(determinant) < scMinTangentDeterminant) {
		const Vec3d fallback = (std::abs(normal.Y) < 0.9) ? Vec3d(0.0, 1.0, 0.0) : Vec3d(1.0, 0.0, 0.0);
		const Vec3d tangent = fallback.Cross(normal);
		frame.Tangent = tangent * (1.0 / tangent.Length());
		return frame;
	}

	Vec3d tangent = (u_gradient * vv - v_gradient * uv) * (1.0 / determinant);
	tangent = tangent * (1.0 / tangent.Length());

	const Vec3d v_direction = (v_gradient * uu - u_gradient * uv) * (1.0 / determinant);

	frame.Tangent = tangent;
	frame.Handedness = (normal.Cross(tangent).Dot(v_direction) > 0.0) ? -1.0 : 1.0;

	return frame;
}

bool IsOnSegment(const Vec3d& point, const Vec3d& from, const Vec3d& to, double& out_fraction)
{
	const Vec3d edge = to - from;
	const double length_squared = edge.Dot(edge);

	if (length_squared <= 0.0) {
		return false;
	}

	out_fraction = (point - from).Dot(edge) / length_squared;

	if (out_fraction <= 0.0 || out_fraction >= 1.0) {
		return false;
	}

	const Vec3d closest = from + edge * out_fraction;

	return (point - closest).Length() < scOnEdgeEpsilon && (point - from).Length() > scWeldEpsilon &&
		   (point - to).Length() > scWeldEpsilon;
}

using Loop = Array<int32>;
using EdgeKey = std::pair<int32, int32>;

Array<Loop> CopyLoops(const Array<Loop>& loops)
{
	Array<Loop> copy;

	for (const Loop& loop : loops) {
		copy.Emplace(CopyArray(loop));
	}

	return copy;
}

void RemoveValue(Array<int32>& array, int32 value)
{
	uint32 kept = 0;

	for (uint32 i = 0; i < array.Size; i++) {
		if (array[i] != value) {
			array[kept++] = array[i];
		}
	}

	while (array.Size > kept) {
		array.RemoveLast();
	}
}

struct PlaneBasis
{
	Vec3d U = Vec3d(0.0);
	Vec3d V = Vec3d(0.0);

	explicit PlaneBasis(const Vec3d& normal)
	{
		int32 minor_axis = 0;

		for (int32 axis = 1; axis < 3; axis++) {
			if (std::abs(normal.mData[axis]) < std::abs(normal.mData[minor_axis])) {
				minor_axis = axis;
			}
		}

		Vec3d reference = Vec3d(0.0);
		reference.mData[minor_axis] = 1.0;

		U = reference.Cross(normal);
		U = U * (1.0 / U.Length());
		V = normal.Cross(U);
	}

	Vec2d Project(const Vec3d& point) const { return Vec2d(point.Dot(U), point.Dot(V)); }
};

class GroupPoints
{
public:
	GroupPoints(const Array<Vec3d>& points, const Vec3d& normal) : mPoints(points), mBasis(normal) {}

	const Vec2d& operator[](int32 index)
	{
		auto found = mProjected.find(index);

		if (found == mProjected.end()) {
			found = mProjected.emplace(index, mBasis.Project(mPoints[static_cast<size_t>(index)])).first;
		}

		return found->second;
	}

private:
	const Array<Vec3d>& mPoints;
	PlaneBasis mBasis;
	std::unordered_map<int32, Vec2d> mProjected;
};

double Cross2(const Vec2d& origin, const Vec2d& a, const Vec2d& b)
{
	return (a.X - origin.X) * (b.Y - origin.Y) - (a.Y - origin.Y) * (b.X - origin.X);
}

double Distance2(const Vec2d& a, const Vec2d& b) { return std::hypot(b.X - a.X, b.Y - a.Y); }

double GetSideDistance(const Vec2d& from, const Vec2d& to, const Vec2d& point)
{
	const double length = Distance2(from, to);

	return (length > 0.0) ? Cross2(from, to, point) / length : 0.0;
}

double GetLoopArea(const Loop& loop, GroupPoints& points)
{
	double area = 0.0;

	for (size_t i = 0; i < loop.Size; i++) {
		const Vec2d& a = points[loop[i]];
		const Vec2d& b = points[loop[(i + 1) % loop.Size]];

		area += a.X * b.Y - a.Y * b.X;
	}

	return area * 0.5;
}

bool BuildLoops(const Array<EdgeKey>& edges, GroupPoints& points, Array<Loop>& out_loops)
{
	constexpr double scFullTurn = 6.283185307179586;
	constexpr double scNoTurn = 1e-9;

	out_loops.Clear();

	std::unordered_map<int32, Array<size_t>> outgoing;

	for (size_t i = 0; i < edges.Size; i++) {
		outgoing[edges[i].first].Emplace(i);
	}

	Array<bool> used;
	Fill(used, edges.Size, false);

	for (size_t first = 0; first < edges.Size; first++) {
		if (used[first]) {
			continue;
		}

		Loop loop;
		size_t current = first;

		for (size_t step = 0; step <= edges.Size; step++) {
			used[current] = true;
			loop.Emplace(edges[current].first);

			const int32 from = edges[current].first;
			const int32 at = edges[current].second;

			const Vec2d& at_point = points[at];
			const Vec2d& from_point = points[from];

			const double back_x = from_point.X - at_point.X;
			const double back_y = from_point.Y - at_point.Y;

			size_t best = edges.Size;
			double best_turn = 0.0;

			for (const size_t candidate : outgoing[at]) {
				if (used[candidate] && candidate != first) {
					continue;
				}

				const Vec2d& to_point = points[edges[candidate].second];

				const double out_x = to_point.X - at_point.X;
				const double out_y = to_point.Y - at_point.Y;

				double turn = std::atan2(out_x * back_y - out_y * back_x, out_x * back_x + out_y * back_y);

				if (turn <= scNoTurn) {
					turn += scFullTurn;
				}

				if (best == edges.Size || turn < best_turn) {
					best = candidate;
					best_turn = turn;
				}
			}

			if (best == edges.Size) {
				return false;
			}

			current = best;

			if (current == first) {
				break;
			}
		}

		if (current != first || loop.Size < 3) {
			return false;
		}

		out_loops.Emplace(std::move(loop));
	}

	return true;
}

bool FindBridge(const Loop& hole, const Array<Loop>& loops, size_t hole_index,
				const std::map<int32, Array<int32>>& inner_edges, Array<int32>& out_path)
{
	std::unordered_map<int32, int32> previous;
	Array<int32> queue;

	std::unordered_map<int32, bool> is_target;

	for (size_t i = 0; i < loops.Size; i++) {
		if (i == hole_index) {
			continue;
		}

		for (const int32 vertex : loops[i]) {
			is_target[vertex] = true;
		}
	}

	for (const int32 vertex : hole) {
		if (previous.emplace(vertex, -1).second) {
			queue.Emplace(vertex);
		}
	}

	for (size_t head = 0; head < queue.Size; head++) {
		const int32 vertex = queue[head];

		if (previous[vertex] != -1 && is_target.contains(vertex)) {
			out_path.Clear();

			for (int32 at = vertex; at != -1; at = previous[at]) {
				out_path.Emplace(at);
			}

			return true;
		}

		const auto found = inner_edges.find(vertex);

		if (found == inner_edges.end()) {
			continue;
		}

		for (const int32 next : found->second) {
			if (previous.emplace(next, vertex).second) {
				queue.Emplace(next);
			}
		}
	}

	return false;
}

void RemoveCollinearPoints(Loop& loop, GroupPoints& points)
{
	bool removed = true;

	while (removed && loop.Size > 3) {
		removed = false;

		for (size_t i = 0; i < loop.Size && loop.Size > 3; i++) {
			const size_t count = loop.Size;

			const Vec2d& previous = points[loop[(i + count - 1) % count]];
			const Vec2d& current = points[loop[i]];
			const Vec2d& next = points[loop[(i + 1) % count]];

			const double along = (current.X - previous.X) * (next.X - current.X) +
								 (current.Y - previous.Y) * (next.Y - current.Y);

			if (along > 0.0 && std::abs(GetSideDistance(previous, next, current)) < scCollinearEpsilon) {
				RemoveAt(loop, static_cast<uint32>(i));
				removed = true;
				i--;
			}
		}
	}
}

bool MergeRegion(const Array<Loop>& polygons, GroupPoints& points, Array<Loop>& out_loops)
{
	std::map<EdgeKey, int32> edge_counts;
	std::map<int32, Array<int32>> inner_edges;

	for (const Loop& polygon : polygons) {
		for (size_t i = 0; i < polygon.Size; i++) {
			const int32 from = polygon[i];
			const int32 to = polygon[(i + 1) % polygon.Size];

			const auto opposite = edge_counts.find({ to, from });

			if (opposite != edge_counts.end() && opposite->second > 0) {
				opposite->second--;
				inner_edges[from].Emplace(to);
				inner_edges[to].Emplace(from);
			}
			else {
				edge_counts[{ from, to }]++;
			}
		}
	}

	Array<EdgeKey> edges;

	for (const auto& [edge, count] : edge_counts) {
		for (int32 i = 0; i < count; i++) {
			edges.Emplace(edge);
		}
	}

	Array<int32> path;

	for (size_t attempt = 0; attempt <= polygons.Size; attempt++) {
		if (!BuildLoops(edges, points, out_loops)) {
			return false;
		}

		size_t hole_index = out_loops.Size;

		for (size_t i = 0; i < out_loops.Size; i++) {
			if (GetLoopArea(out_loops[i], points) < 0.0) {
				hole_index = i;
				break;
			}
		}

		if (hole_index == out_loops.Size) {
			for (Loop& loop : out_loops) {
				RemoveCollinearPoints(loop, points);
			}

			return true;
		}

		if (!FindBridge(out_loops[hole_index], out_loops, hole_index, inner_edges, path)) {
			return false;
		}

		for (size_t i = 0; i + 1 < path.Size; i++) {
			const int32 from = path[i];
			const int32 to = path[i + 1];

			edges.Emplace(EdgeKey { from, to });
			edges.Emplace(EdgeKey { to, from });

			RemoveValue(inner_edges[from], to);
			RemoveValue(inner_edges[to], from);
		}
	}

	return false;
}

bool TriangulateLoop(const Loop& loop, GroupPoints& points, Array<int32>& out_indices)
{
	constexpr double scInsideTolerance = 1e-7;

	Loop remaining = CopyArray(loop);

	while (remaining.Size > 3) {
		const size_t count = remaining.Size;

		size_t best = count;
		double best_length = 0.0;

		for (size_t i = 0; i < count; i++) {
			const int32 previous_index = remaining[(i + count - 1) % count];
			const int32 current_index = remaining[i];
			const int32 next_index = remaining[(i + 1) % count];

			const Vec2d& previous = points[previous_index];
			const Vec2d& current = points[current_index];
			const Vec2d& next = points[next_index];

			if (previous_index == next_index || GetSideDistance(previous, next, current) > -scCollinearEpsilon) {
				continue;
			}

			bool blocked = false;

			for (size_t j = 0; j < count && !blocked; j++) {
				const int32 other_index = remaining[j];

				if (other_index == previous_index || other_index == current_index || other_index == next_index) {
					continue;
				}

				const Vec2d& other = points[other_index];

				blocked = GetSideDistance(previous, current, other) > -scInsideTolerance &&
						  GetSideDistance(current, next, other) > -scInsideTolerance &&
						  GetSideDistance(next, previous, other) > -scInsideTolerance;
			}

			const double length = Distance2(previous, next);

			if (!blocked && (best == count || length < best_length)) {
				best = i;
				best_length = length;
			}
		}

		if (best == count) {
			return false;
		}

		out_indices.Emplace(remaining[(best + count - 1) % count]);
		out_indices.Emplace(remaining[best]);
		out_indices.Emplace(remaining[(best + 1) % count]);

		RemoveAt(remaining, static_cast<uint32>(best));
	}

	if (remaining.Size == 3 && Cross2(points[remaining[0]], points[remaining[1]], points[remaining[2]]) > 0.0) {
		for (const int32 index : remaining) {
			out_indices.Emplace(index);
		}
	}

	return true;
}

double GetTriangleArea(const Array<int32>& indices, GroupPoints& points)
{
	double area = 0.0;

	for (size_t i = 0; i + 2 < indices.Size; i += 3) {
		area += Cross2(points[indices[i]], points[indices[i + 1]], points[indices[i + 2]]) * 0.5;
	}

	return area;
}

class TJunctionFixer
{
public:
	TJunctionFixer(const Array<Vec3d>& points, const Array<int32>& candidates)
		: mPoints(points), mCandidates(candidates)
	{
		for (const int32 index : candidates) {
			mGrid[GetCell(points[static_cast<size_t>(index)], scEdgeGridCellSize)].Emplace(index);
		}
	}

	uint32 Fix(Loop& loop)
	{
		uint32 fixed_count = 0;

		mFixedLoop.Clear();

		for (size_t i = 0; i < loop.Size; i++) {
			const int32 from_index = loop[i];

			FindInserts(from_index, loop[(i + 1) % loop.Size]);

			mFixedLoop.Emplace(from_index);

			for (const auto& [fraction, index] : mInserts) {
				if (GetLast(mFixedLoop) != index) {
					mFixedLoop.Emplace(index);
					fixed_count++;
				}
			}
		}

		loop = CopyArray(mFixedLoop);

		return fixed_count;
	}

	const Array<std::pair<double, int32>>& FindInserts(int32 from_index, int32 to_index)
	{
		const Vec3d padding(scOnEdgeEpsilon, scOnEdgeEpsilon, scOnEdgeEpsilon);

		const Vec3d& from = mPoints[static_cast<size_t>(from_index)];
		const Vec3d& to = mPoints[static_cast<size_t>(to_index)];

		mInserts.Clear();

		const auto test_point = [&](int32 index)
		{
			double fraction = 0.0;

			if (index != from_index && index != to_index &&
				IsOnSegment(mPoints[static_cast<size_t>(index)], from, to, fraction)) {
				mInserts.Emplace(fraction, index);
			}
		};

		const CellKey min_cell = GetCell(MinVec(from, to) - padding, scEdgeGridCellSize);
		const CellKey max_cell = GetCell(MaxVec(from, to) + padding, scEdgeGridCellSize);

		const int64 cell_count = (max_cell.X - min_cell.X + 1) * (max_cell.Y - min_cell.Y + 1) *
								 (max_cell.Z - min_cell.Z + 1);

		if (cell_count > scMaxEdgeGridCells) {
			for (const int32 index : mCandidates) {
				test_point(index);
			}
		}
		else {
			for (int64 x = min_cell.X; x <= max_cell.X; x++) {
				for (int64 y = min_cell.Y; y <= max_cell.Y; y++) {
					for (int64 z = min_cell.Z; z <= max_cell.Z; z++) {
						const auto found = mGrid.find(CellKey { x, y, z });

						if (found == mGrid.end()) {
							continue;
						}

						for (const int32 index : found->second) {
							test_point(index);
						}
					}
				}
			}
		}

		std::sort(mInserts.begin(), mInserts.end());

		return mInserts;
	}

private:
	const Array<Vec3d>& mPoints;
	const Array<int32>& mCandidates;
	std::unordered_map<CellKey, Array<int32>, CellKeyHash> mGrid;

	Array<std::pair<double, int32>> mInserts;
	Loop mFixedLoop;
};

PatchSurfaceVertex BlendPatchVertices(const PatchSurfaceVertex& a, const PatchSurfaceVertex& b, double weight)
{
	PatchSurfaceVertex vertex;
	vertex.Position = a.Position + (b.Position - a.Position) * weight;
	vertex.Texcoord = a.Texcoord + (b.Texcoord - a.Texcoord) * weight;
	vertex.Chart = a.Chart + (b.Chart - a.Chart) * weight;
	vertex.Handedness = a.Handedness;

	const Vec3d normal = a.Normal + (b.Normal - a.Normal) * weight;
	const double normal_length = normal.Length();

	vertex.Normal = a.Normal;

	if (normal_length > 0.0) {
		vertex.Normal = normal * (1.0 / normal_length);
	}

	const Vec3d tangent = a.Tangent + (b.Tangent - a.Tangent) * weight;
	const Vec3d projected = tangent - vertex.Normal * vertex.Normal.Dot(tangent);
	const double tangent_length = projected.Length();

	vertex.Tangent = a.Tangent;

	if (tangent_length > 0.0) {
		vertex.Tangent = projected * (1.0 / tangent_length);
	}

	return vertex;
}

uint32 StitchPatch(PatchSurface& surface, const Array<int32>& weld, TJunctionFixer& fixer, const Array<Vec3d>& points)
{
	uint32 inserted_count = 0;

	Array<int32> indices;
	Array<int32> polygon;

	for (size_t triangle = 0; triangle + 2 < surface.Indices.Size; triangle += 3) {
		const int32 corners[3] = { surface.Indices[triangle], surface.Indices[triangle + 1],
								   surface.Indices[triangle + 2] };

		polygon.Clear();

		uint32 split_edges = 0;
		uint32 split_edge = 0;

		for (uint32 edge = 0; edge < 3; edge++) {
			const int32 from = corners[edge];
			const int32 to = corners[(edge + 1) % 3];

			polygon.Emplace(from);

			const int32 weld_from = weld[static_cast<size_t>(from)];
			const int32 weld_to = weld[static_cast<size_t>(to)];

			if (weld_from < 0 || weld_to < 0) {
				continue;
			}

			const Array<std::pair<double, int32>>& inserts = fixer.FindInserts(weld_from, weld_to);

			if (inserts.Size == 0) {
				continue;
			}

			split_edges++;
			split_edge = edge;

			for (const auto& [fraction, point_index] : inserts) {
				PatchSurfaceVertex vertex = BlendPatchVertices(surface.Vertices[static_cast<size_t>(from)],
															   surface.Vertices[static_cast<size_t>(to)], fraction);
				vertex.Position = points[static_cast<size_t>(point_index)];

				polygon.Emplace(static_cast<int32>(surface.Vertices.Size));
				surface.Vertices.Emplace(vertex);

				inserted_count++;
			}
		}

		if (split_edges == 0) {
			indices.Emplace(corners[0]);
			indices.Emplace(corners[1]);
			indices.Emplace(corners[2]);
			continue;
		}

		if (split_edges == 1) {
			const int32 apex = corners[(split_edge + 2) % 3];
			size_t start = 0;

			while (polygon[start] != apex) {
				start++;
			}

			for (size_t i = 1; i + 1 < polygon.Size; i++) {
				indices.Emplace(apex);
				indices.Emplace(polygon[(start + i) % polygon.Size]);
				indices.Emplace(polygon[(start + i + 1) % polygon.Size]);
			}

			continue;
		}

		PatchSurfaceVertex center = BlendPatchVertices(surface.Vertices[static_cast<size_t>(corners[0])],
													   surface.Vertices[static_cast<size_t>(corners[1])], 0.5);
		center = BlendPatchVertices(center, surface.Vertices[static_cast<size_t>(corners[2])], 1.0 / 3.0);

		const int32 center_index = static_cast<int32>(surface.Vertices.Size);
		surface.Vertices.Emplace(center);

		for (size_t i = 0; i < polygon.Size; i++) {
			indices.Emplace(center_index);
			indices.Emplace(polygon[i]);
			indices.Emplace(polygon[(i + 1) % polygon.Size]);
		}
	}

	surface.Indices = std::move(indices);

	return inserted_count;
}

struct SurfaceGroup
{
	int32 PlaneIndex = PlaneSet::scNone;
	int32 MaterialIndex = 0;
	int32 MapIndex = 0;
	int32 Area = scNoMapArea;

	Array<Loop> Polygons;
	Array<Loop> Loops;

	Vec3d Center = Vec3d(0.0);
};

int32 FindPointArea(const Bsp& bsp, const Vec3d& point, const Vec3d& normal)
{
	for (const double offset : { scAreaProbeDistance, -scAreaProbeDistance, 0.0 }) {
		const Node& leaf = bsp.Nodes[static_cast<size_t>(bsp.FindLeaf(point + normal * offset))];

		if (leaf.Area != scNoMapArea) {
			return leaf.Area;
		}
	}

	return scNoMapArea;
}

} // namespace

int32 SurfaceBuilder::FindOrAddMap(const TextureMap& map)
{
	const auto is_near = [](const Vec3d& a, const Vec3d& b)
	{
		return std::abs(a.X - b.X) < scGradientEpsilon && std::abs(a.Y - b.Y) < scGradientEpsilon &&
			   std::abs(a.Z - b.Z) < scGradientEpsilon;
	};

	for (size_t i = 0; i < mMaps.Size; i++) {
		const TextureMap& other = mMaps[i];

		if (is_near(map.GradientU, other.GradientU) && is_near(map.GradientV, other.GradientV) &&
			std::abs(map.OffsetU - other.OffsetU) < scOffsetEpsilon &&
			std::abs(map.OffsetV - other.OffsetV) < scOffsetEpsilon) {
			return static_cast<int32>(i);
		}
	}

	mMaps.Emplace(map);

	return static_cast<int32>(mMaps.Size - 1);
}

void SurfaceBuilder::CullFaces(const PlaneSet& planes, const Bsp& bsp, const MapSource& source, bool sealed,
							   const SurfaceOptions& options)
{
	Array<Face> faces;
	Array<LeafFragment> fragments;

	for (const SourceBrush& brush : source.Brushes) {
		if (!brush.bVisible) {
			continue;
		}

		for (const PolyhedronSide& side : brush.Shape.Sides) {
			int32 material_index = SourceBrush::scDroppedFace;
			const TextureMap* map = brush.FindMap(side.PlaneIndex, material_index);

			if (map == nullptr) {
				continue;
			}

			Face face;
			face.PlaneIndex = side.PlaneIndex;
			face.MaterialIndex = material_index;
			face.MapIndex = FindOrAddMap(*map);
			face.Original = CopyWinding(side.Points);
			face.Min = Vec3d(1e30, 1e30, 1e30);
			face.Max = Vec3d(-1e30, -1e30, -1e30);

			for (const Vec3d& point : side.Points) {
				face.Min = MinVec(face.Min, point);
				face.Max = MaxVec(face.Max, point);
			}

			const double original_area = GetWindingArea(side.Points);

			Stats.NumSrcFaces++;
			Stats.NumSrcTriangles += static_cast<uint32>(side.Points.Size - 2);
			Stats.SourceArea += original_area;

			if (!options.bCullHidden) {
				const int32 area = FindPointArea(bsp, GetWindingCenter(side.Points), planes[side.PlaneIndex].Normal);

				face.Visible.Emplace(FacePiece { CopyWinding(side.Points), area });
				faces.Emplace(std::move(face));
				continue;
			}

			fragments.Clear();
			bsp.FilterWinding(side.Points, side.PlaneIndex, fragments);

			double visible_area = 0.0;
			bool is_one_area = true;

			for (LeafFragment& fragment : fragments) {
				const Node& leaf = bsp.Nodes[static_cast<size_t>(fragment.Leaf)];

				if (leaf.bSolid || (sealed && !leaf.bReached) || IsWindingTiny(fragment.Points)) {
					continue;
				}

				if (face.Visible.Size > 0 && face.Visible[0].Area != leaf.Area) {
					is_one_area = false;
				}

				visible_area += GetWindingArea(fragment.Points);
				face.Visible.Emplace(FacePiece { std::move(fragment.Points), leaf.Area });
			}

			if (face.Visible.Size == 0) {
				Stats.FacesRemoved++;
				continue;
			}

			if (is_one_area && visible_area >= original_area * (1.0 - scFullFaceTolerance)) {
				const int32 area = face.Visible[0].Area;

				face.Visible.Clear();
				face.Visible.Emplace(FacePiece { CopyWinding(side.Points), area });
			}
			else {
				Stats.FacesClipped++;
			}

			faces.Emplace(std::move(face));
		}
	}

	std::unordered_map<int32, Array<size_t>> faces_on_plane;

	for (size_t i = 0; i < faces.Size; i++) {
		Face& face = faces[i];
		Array<size_t>& earlier_faces = faces_on_plane[face.PlaneIndex];

		const Vec3d normal = planes[face.PlaneIndex].Normal;

		for (const size_t earlier_index : earlier_faces) {
			if (!options.bCullHidden) {
				break;
			}

			const Face& earlier = faces[earlier_index];

			if (earlier.MaterialIndex == SourceBrush::scDroppedFace &&
				face.MaterialIndex != SourceBrush::scDroppedFace) {
				continue;
			}

			bool overlaps = true;

			for (int32 axis = 0; axis < 3; axis++) {
				overlaps = overlaps && face.Min.mData[axis] <= earlier.Max.mData[axis] + scOnEpsilon &&
						   face.Max.mData[axis] >= earlier.Min.mData[axis] - scOnEpsilon;
			}

			if (!overlaps) {
				continue;
			}

			Array<FacePiece> remaining;
			Array<Winding> cut_pieces;

			for (FacePiece& piece : face.Visible) {
				cut_pieces.Clear();

				if (SubtractConvex(piece.Points, earlier.Original, normal, cut_pieces)) {
					Stats.OverlapsRemoved++;

					for (Winding& cut_piece : cut_pieces) {
						remaining.Emplace(FacePiece { std::move(cut_piece), piece.Area });
					}
				}
				else {
					remaining.Emplace(std::move(piece));
				}
			}

			face.Visible = std::move(remaining);
		}

		earlier_faces.Emplace(i);
	}

	for (Face& face : faces) {
		for (FacePiece& piece : face.Visible) {
			if (IsWindingTiny(piece.Points)) {
				continue;
			}

			mPolygons.Emplace(SurfacePolygon { face.PlaneIndex, face.MaterialIndex, face.MapIndex, piece.Area,
											   std::move(piece.Points) });
		}
	}
}

void SurfaceBuilder::Triangulate(const PlaneSet& planes, const Bsp& bsp, const MapSource& source,
								 const SurfaceOptions& options, PatchBuilder* patches, CompiledMap& out_map)
{
	VertexWelder welder;

	std::map<std::tuple<int32, int32, int32, int32>, SurfaceGroup> groups;

	for (const SurfacePolygon& polygon : mPolygons) {
		Loop loop;

		for (const Vec3d& point : polygon.Points) {
			const int32 index = welder.Add(point);

			if (loop.Size == 0 || (GetLast(loop) != index && loop[0] != index)) {
				loop.Emplace(index);
			}
		}

		if (loop.Size < 3) {
			continue;
		}

		SurfaceGroup& group = groups[{ polygon.PlaneIndex, polygon.MaterialIndex, polygon.MapIndex, polygon.Area }];
		group.PlaneIndex = polygon.PlaneIndex;
		group.MaterialIndex = polygon.MaterialIndex;
		group.MapIndex = polygon.MapIndex;
		group.Area = polygon.Area;
		group.Polygons.Emplace(std::move(loop));
	}

	Array<Array<int32>> patch_welds;

	if (patches != nullptr) {
		Array<std::pair<int32, int32>> boundary;

		for (PatchSurface& surface : patches->Surfaces) {
			Array<int32>& weld = patch_welds.Emplace();
			Fill(weld, static_cast<uint32>(surface.Vertices.Size), -1);

			PatchBuilder::FindBoundaryEdges(surface, boundary);

			for (const auto& [from, to] : boundary) {
				for (const int32 vertex : { from, to }) {
					if (weld[static_cast<size_t>(vertex)] < 0) {
						weld[static_cast<size_t>(vertex)] = welder.Add(
							surface.Vertices[static_cast<size_t>(vertex)].Position);
					}
				}
			}
		}
	}

	const Array<Vec3d>& points = welder.Points;

	for (uint32 surface_index = 0; surface_index < patch_welds.Size; surface_index++) {
		PatchSurface& surface = patches->Surfaces[surface_index];

		for (uint32 vertex = 0; vertex < patch_welds[surface_index].Size; vertex++) {
			const int32 index = patch_welds[surface_index][vertex];

			if (index >= 0) {
				surface.Vertices[vertex].Position = points[static_cast<size_t>(index)];
			}
		}
	}

	Array<int32> all_points;

	for (uint32 i = 0; i < points.Size; i++) {
		all_points.Emplace(static_cast<int32>(i));
	}

	if (options.bMerge) {
		TJunctionFixer splitter(points, all_points);

		for (auto& [key, group] : groups) {
			for (Loop& polygon : group.Polygons) {
				splitter.Fix(polygon);
			}

			GroupPoints group_points(points, planes[group.PlaneIndex].Normal);

			if (!MergeRegion(group.Polygons, group_points, group.Loops)) {
				group.Loops = CopyLoops(group.Polygons);
				Stats.MergeFallbacks++;
			}
		}
	}
	else {
		for (auto& [key, group] : groups) {
			group.Loops = CopyLoops(group.Polygons);
		}
	}

	if (options.bFixTJunctions) {
		Array<bool> is_corner;
		Fill(is_corner, points.Size, false);

		for (const auto& [key, group] : groups) {
			for (const Loop& loop : group.Loops) {
				for (const int32 index : loop) {
					is_corner[static_cast<size_t>(index)] = true;
				}
			}
		}

		for (const Array<int32>& weld : patch_welds) {
			for (const int32 index : weld) {
				if (index >= 0) {
					is_corner[static_cast<size_t>(index)] = true;
				}
			}
		}

		Array<int32> corners;

		for (size_t i = 0; i < points.Size; i++) {
			if (is_corner[i]) {
				corners.Emplace(static_cast<int32>(i));
			}
		}

		TJunctionFixer fixer(points, corners);

		for (auto& [key, group] : groups) {
			for (Loop& loop : group.Loops) {
				Stats.TJunctionsFixed += fixer.Fix(loop);
			}
		}

		for (uint32 surface_index = 0; surface_index < patch_welds.Size; surface_index++) {
			patches->Stats.TJunctionsFixed += StitchPatch(patches->Surfaces[surface_index], patch_welds[surface_index],
														  fixer, points);
		}
	}

	struct Batch
	{
		uint32 MaterialIndex = 0;
		int32 Area = scNoMapArea;

		Array<CompiledMapVertex> Vertices;
		Array<LightmapVertex> LightmapVertices;
		Array<uint32> Indices;

		std::map<std::tuple<int32, int32, int32, int32>, uint32> VertexLookup;
		std::map<std::pair<uint32, int32>, uint32> PatchVertexLookup;
	};

	std::map<std::tuple<int32, int32, int64, int64, int64>, Batch> batches;

	struct ShadowBatch
	{
		Array<Vec3d> Points;
		Array<uint32> Indices;

		std::map<int32, uint32> PointLookup;
		std::map<std::pair<uint32, int32>, uint32> PatchPointLookup;
	};

	std::map<std::tuple<int64, int64, int64>, ShadowBatch> shadow_batches;

	Array<int32> loop_indices;
	Array<int32> polygon_indices;

	for (auto& [key, group] : groups) {
		const Vec3d normal = planes[group.PlaneIndex].Normal;
		const TextureMap& map = mMaps[static_cast<size_t>(group.MapIndex)];
		const TangentFrame frame = GetTangentFrame(normal, map);

		GroupPoints group_points(points, normal);

		double expected_area = 0.0;

		for (const Loop& polygon : group.Polygons) {
			expected_area += GetLoopArea(polygon, group_points);
		}

		bool is_valid = true;
		Array<Array<int32>> triangle_lists;

		for (const Loop& loop : group.Loops) {
			loop_indices.Clear();
			is_valid = is_valid && TriangulateLoop(loop, group_points, loop_indices);
			triangle_lists.Emplace(CopyArray(loop_indices));
		}

		double area = 0.0;

		for (const Array<int32>& list : triangle_lists) {
			area += GetTriangleArea(list, group_points);
		}

		if (!is_valid || std::abs(area - expected_area) > scAreaTolerance * (1.0 + expected_area)) {
			triangle_lists.Clear();
			Stats.MergeFallbacks++;

			TJunctionFixer splitter(points, all_points);

			for (const Loop& source_polygon : group.Polygons) {
				Loop polygon = CopyArray(source_polygon);

				if (options.bFixTJunctions) {
					splitter.Fix(polygon);
				}

				polygon_indices.Clear();
				TriangulateLoop(polygon, group_points, polygon_indices);
				triangle_lists.Emplace(CopyArray(polygon_indices));
			}
		}

		Stats.NumPolygons += static_cast<uint32>(triangle_lists.Size);

		for (const Array<int32>& triangle_indices : triangle_lists) {
			if (triangle_indices.Size == 0) {
				continue;
			}

			Vec3d center = Vec3d(0.0);

			for (const int32 index : triangle_indices) {
				center = center + points[static_cast<size_t>(index)];
			}

			center = center * (1.0 / static_cast<double>(triangle_indices.Size));

			CellKey cell;

			if (options.ChunkSize > 0.0) {
				cell = GetCell(center, options.ChunkSize);
			}

			const bool is_caulk = group.MaterialIndex == SourceBrush::scDroppedFace;

			if (is_caulk || source.OpaqueMaterials[static_cast<size_t>(group.MaterialIndex)] != 0) {
				ShadowBatch& shadow = shadow_batches[{ cell.X, cell.Y, cell.Z }];

				for (size_t t = 0; t + 2 < triangle_indices.Size; t += 3) {
					const int32 corners[3] = { triangle_indices[t], triangle_indices[t + 2], triangle_indices[t + 1] };

					for (const int32 corner : corners) {
						auto found = shadow.PointLookup.find(corner);

						if (found == shadow.PointLookup.end()) {
							found = shadow.PointLookup.emplace(corner, static_cast<uint32>(shadow.Points.Size)).first;
							shadow.Points.Emplace(points[static_cast<size_t>(corner)]);
						}

						shadow.Indices.Emplace(found->second);
					}
				}
			}

			if (is_caulk) {
				Stats.NumCaulkTriangles += static_cast<uint32>(triangle_indices.Size / 3);
				continue;
			}

			Batch& batch = batches[{ group.MaterialIndex, group.Area, cell.X, cell.Y, cell.Z }];
			batch.MaterialIndex = static_cast<uint32>(group.MaterialIndex);
			batch.Area = group.Area;

			int32 chart_index = -1;

			if (options.bMakeLightmapCharts) {
				const PlaneBasis basis(normal);

				chart_index = static_cast<int32>(Charts.Size);

				LightmapChart& chart = Charts.Emplace();
				chart.PlaneIndex = group.PlaneIndex;
				chart.U = basis.U;
				chart.V = basis.V;

				for (const int32 index : triangle_indices) {
					chart.TrianglePoints.Emplace(group_points[index]);
				}
			}

			for (size_t t = 0; t + 2 < triangle_indices.Size; t += 3) {
				const int32 corners[3] = { triangle_indices[t], triangle_indices[t + 2], triangle_indices[t + 1] };

				for (const int32 corner : corners) {
					const Vec3d& point = points[static_cast<size_t>(corner)];
					const auto vertex_key = std::make_tuple(corner, group.PlaneIndex, group.MapIndex, chart_index);

					auto found = batch.VertexLookup.find(vertex_key);

					if (found == batch.VertexLookup.end()) {
						CompiledMapVertex vertex {};

						vertex.Position = point.ToVec3f();
						vertex.Normal = normal.ToVec3f();
						vertex.Tangent = Vec4f(frame.Tangent.ToVec3f(), frame.Handedness);

						vertex.Texcoord = Vec2f(static_cast<float32>(map.GradientU.Dot(point) + map.OffsetU),
												static_cast<float32>(map.GradientV.Dot(point) + map.OffsetV));

						found = batch.VertexLookup.emplace(vertex_key, batch.Vertices.Size).first;
						batch.Vertices.Emplace(vertex);
						batch.LightmapVertices.Emplace(LightmapVertex { chart_index, group_points[corner] });
					}

					batch.Indices.Emplace(found->second);
					TrianglePoints.Emplace(point);
				}

				const Vec3d& a = points[static_cast<size_t>(corners[0])];

				Stats.Area += (points[static_cast<size_t>(corners[1])] - a)
								  .Cross(points[static_cast<size_t>(corners[2])] - a)
								  .Length() *
							  0.5;
			}
		}
	}

	uint32 patch_triangle_count = 0;
	uint32 patch_vertex_count = 0;

	if (patches != nullptr) {
		for (uint32 surface_index = 0; surface_index < patches->Surfaces.Size; surface_index++) {
			const PatchSurface& surface = patches->Surfaces[surface_index];

			int32 chart_index = -1;

			if (options.bMakeLightmapCharts && surface.Indices.Size >= 3) {
				chart_index = static_cast<int32>(Charts.Size);

				LightmapChart& chart = Charts.Emplace();

				for (const int32 index : surface.Indices) {
					const PatchSurfaceVertex& vertex = surface.Vertices[static_cast<size_t>(index)];

					chart.TrianglePoints.Emplace(vertex.Chart);
					chart.WorldPoints.Emplace(vertex.Position);
					chart.Normals.Emplace(vertex.Normal);
				}
			}

			for (size_t triangle = 0; triangle + 2 < surface.Indices.Size; triangle += 3) {
				Vec3d center = Vec3d(0.0);

				for (uint32 corner = 0; corner < 3; corner++) {
					center = center +
							 surface.Vertices[static_cast<size_t>(surface.Indices[triangle + corner])].Position;
				}

				CellKey cell;

				if (options.ChunkSize > 0.0) {
					cell = GetCell(center * (1.0 / 3.0), options.ChunkSize);
				}

				if (surface.bCastsShadow) {
					ShadowBatch& shadow = shadow_batches[{ cell.X, cell.Y, cell.Z }];

					for (uint32 corner = 0; corner < 3; corner++) {
						const int32 vertex_index = surface.Indices[triangle + corner];
						auto found = shadow.PatchPointLookup.find({ surface_index, vertex_index });

						if (found == shadow.PatchPointLookup.end()) {
							found = shadow.PatchPointLookup
										.emplace(std::make_pair(surface_index, vertex_index),
												 static_cast<uint32>(shadow.Points.Size))
										.first;
							shadow.Points.Emplace(surface.Vertices[static_cast<size_t>(vertex_index)].Position);
						}

						shadow.Indices.Emplace(found->second);
					}
				}

				const Vec3d& a = surface.Vertices[static_cast<size_t>(surface.Indices[triangle])].Position;
				const Vec3d& b = surface.Vertices[static_cast<size_t>(surface.Indices[triangle + 1])].Position;
				const Vec3d& c = surface.Vertices[static_cast<size_t>(surface.Indices[triangle + 2])].Position;

				const Vec3d triangle_normal = (b - a).Cross(c - a);
				const double normal_length = triangle_normal.Length();

				Vec3d unit_normal = Vec3d(0.0);

				if (normal_length > 0.0) {
					unit_normal = triangle_normal * (1.0 / normal_length);
				}

				const int32 area = FindPointArea(bsp, center * (1.0 / 3.0), unit_normal);

				Batch& batch = batches[{ surface.MaterialIndex, area, cell.X, cell.Y, cell.Z }];
				batch.MaterialIndex = static_cast<uint32>(surface.MaterialIndex);
				batch.Area = area;

				for (uint32 corner = 0; corner < 3; corner++) {
					const int32 vertex_index = surface.Indices[triangle + corner];
					const PatchSurfaceVertex& source_vertex = surface.Vertices[static_cast<size_t>(vertex_index)];

					auto found = batch.PatchVertexLookup.find({ surface_index, vertex_index });

					if (found == batch.PatchVertexLookup.end()) {
						CompiledMapVertex vertex {};
						vertex.Position = source_vertex.Position.ToVec3f();
						vertex.Normal = source_vertex.Normal.ToVec3f();
						vertex.Tangent = Vec4f(source_vertex.Tangent.ToVec3f(),
											   static_cast<float32>(source_vertex.Handedness));
						vertex.Texcoord = Vec2f(static_cast<float32>(source_vertex.Texcoord.X),
												static_cast<float32>(source_vertex.Texcoord.Y));

						found = batch.PatchVertexLookup
									.emplace(std::make_pair(surface_index, vertex_index), batch.Vertices.Size)
									.first;
						batch.Vertices.Emplace(vertex);
						batch.LightmapVertices.Emplace(LightmapVertex { chart_index, source_vertex.Chart });

						patch_vertex_count++;
					}

					batch.Indices.Emplace(found->second);
					TrianglePoints.Emplace(source_vertex.Position);
				}

				patch_triangle_count++;
			}
		}

		patches->Stats.Triangles = patch_triangle_count;
	}

	out_map.Surfaces.InitCapacity(batches.size());

	for (auto& [key, batch] : batches) {
		CompiledMapSurface surface;
		surface.MaterialIndex = batch.MaterialIndex;
		surface.Area = batch.Area;

		// surface.Vertices = CopyToSizedArray(batch.Vertices);
		// surface.Indices = CopyToSizedArray(batch.Indices);

		surface.Vertices = ArrayUtil::DynToSizedArray(batch.Vertices);
		surface.Indices = ArrayUtil::DynToSizedArray(batch.Indices);

		// for (int32 axis = 0; axis < 3; axis++) {
		// 	surface.BoundsMin[axis] = surface.Vertices[0].Position[axis];
		// 	surface.BoundsMax[axis] = surface.Vertices[0].Position[axis];
		// }

		surface.Bounds = BBox(surface.Vertices[0].Position, surface.Vertices[0].Position);

		for (const CompiledMapVertex& vertex : surface.Vertices) {
			surface.Bounds = BBox(Vec3f::Min(surface.Bounds.Min, vertex.Position),
								  Vec3f::Max(surface.Bounds.Max, vertex.Position));

			// for (int32 axis = 0; axis < 3; axis++) {
			// 	surface.BoundsMin[axis] = std::min(surface.BoundsMin[axis], vertex.Position[axis]);
			// 	surface.BoundsMax[axis] = std::max(surface.BoundsMax[axis], vertex.Position[axis]);
			// }
		}

		Stats.NumTriangles += static_cast<uint32>(surface.Indices.Size / 3);
		Stats.NumVertices += static_cast<uint32>(surface.Vertices.Size);

		out_map.Surfaces.Emplace(std::move(surface));
		LightmapVertices.Emplace(std::move(batch.LightmapVertices));
	}

	Stats.NumTriangles -= patch_triangle_count;
	Stats.NumVertices -= patch_vertex_count;

	out_map.ShadowSurfaces.InitCapacity(shadow_batches.size());

	for (auto& [key, shadow] : shadow_batches) {
		if (shadow.Indices.Size == 0) {
			continue;
		}

		CompiledMapShadowSurface surface;
		surface.Points.InitSize(shadow.Points.Size * 3);
		surface.Indices = ArrayUtil::DynToSizedArray(shadow.Indices);

		const Vec3f first = shadow.Points[0].ToVec3f();
		surface.Bounds = BBox(first, first);

		for (size_t i = 0; i < shadow.Points.Size; i++) {
			const Vec3f point = shadow.Points[i].ToVec3f();

			surface.Points[i * 3] = point.X;
			surface.Points[i * 3 + 1] = point.Y;
			surface.Points[i * 3 + 2] = point.Z;

			surface.Bounds = BBox(Vec3f::Min(surface.Bounds.Min, point), Vec3f::Max(surface.Bounds.Max, point));
		}

		Stats.NumShadowTriangles += static_cast<uint32>(surface.Indices.Size / 3);

		out_map.ShadowSurfaces.Emplace(std::move(surface));
	}

	out_map.Materials.InitAsCopyOf(source.Materials);
}

void SurfaceBuilder::Build(const PlaneSet& planes, const Bsp& bsp, const MapSource& source, bool sealed,
						   const SurfaceOptions& options, PatchBuilder* patches, CompiledMap& out_map)
{
	Stats = SurfaceStats {};
	TrianglePoints.Clear();
	Charts.Clear();
	LightmapVertices.Clear();
	mPolygons.Clear();
	mMaps.Clear();

	CullFaces(planes, bsp, source, sealed, options);

	Stats.NumPolygonsBeforeMerge = static_cast<uint32>(mPolygons.Size);

	Triangulate(planes, bsp, source, options, patches, out_map);
}

} // namespace fx::mapc
