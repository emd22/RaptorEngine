#include "Lightmap.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>

namespace fx::mapc {

namespace {

constexpr uint32 scChartPadding = 1;
constexpr uint32 scMinAtlasSize = 64;
constexpr uint32 scAtlasHeightStep = 4;
constexpr double scAtlasFillEstimate = 1.15;
constexpr double scDensityStep = 0.8;
constexpr double scMinDensity = 0.25;
constexpr double scSizeEpsilon = 1e-6;
constexpr double scSurfaceBias = 1e-3;
constexpr double scParallelEpsilon = 1e-14;
constexpr double scTriangleMinDistance = 1e-6;
constexpr double scCoverageEpsilon = 1e-9;
constexpr double scTwoPi = 6.283185307179586;
constexpr double scNoHit = std::numeric_limits<double>::infinity();
constexpr uint32 scLeafSize = 4;
constexpr uint32 scMaxStackDepth = 64;
constexpr uint32 scDilatePasses = 4;
constexpr uint32 scRowsPerClaim = 8;
constexpr uint32 scDirectionShuffle = 7919;
constexpr uint8 scFullyLit = 255;

struct OccluderPlane
{
	Vec3d Normal = Vec3d(0.0);
	double Distance = 0.0;
};

struct Occluder
{
	uint32 FirstPlane = 0;
	uint32 PlaneCount = 0;

	int32 FirstCorner = -1;

	Vec3d Min = Vec3d(0.0);
	Vec3d Max = Vec3d(0.0);
};

struct BvhNode
{
	Vec3d Min = Vec3d(0.0);
	Vec3d Max = Vec3d(0.0);

	int32 Left = -1;
	int32 Right = -1;

	uint32 First = 0;
	uint32 Count = 0;
};

class OccluderSet
{
public:
	void Build(const PlaneSet& planes, const MapSource& source, const PatchBuilder* patches)
	{
		for (const SourceBrush& brush : source.Brushes) {
			if (!brush.bVisible || !brush.bCastsShadow) {
				continue;
			}

			Occluder occluder;
			occluder.FirstPlane = mPlanes.Size;
			occluder.PlaneCount = brush.PlaneIndices.Size;
			occluder.Min = brush.Shape.Min;
			occluder.Max = brush.Shape.Max;

			for (const int32 plane_index : brush.PlaneIndices) {
				const Plane& plane = planes[plane_index];
				mPlanes.Emplace(OccluderPlane { plane.Normal, plane.Distance });
			}

			mOccluders.Emplace(occluder);
		}

		if (patches != nullptr) {
			for (const PatchSurface& surface : patches->Surfaces) {
				if (!surface.bCastsShadow) {
					continue;
				}

				for (size_t index = 0; index + 2 < surface.Indices.Size; index += 3) {
					Occluder occluder;
					occluder.FirstCorner = static_cast<int32>(mCorners.Size);

					for (uint32 corner = 0; corner < 3; corner++) {
						const Vec3d& point = surface.Vertices[static_cast<size_t>(surface.Indices[index + corner])]
												 .Position;

						mCorners.Emplace(point);

						if (corner == 0) {
							occluder.Min = point;
							occluder.Max = point;
						}

						occluder.Min = MinVec(occluder.Min, point);
						occluder.Max = MaxVec(occluder.Max, point);
					}

					mOccluders.Emplace(occluder);
					mTriangleCount++;
				}
			}
		}

		if (mOccluders.Size > 0) {
			BuildNode(0, mOccluders.Size);
		}
	}

	uint32 GetCount() const { return mOccluders.Size - mTriangleCount; }
	uint32 GetTriangleCount() const { return mTriangleCount; }

	bool IsOccluded(const Vec3d& origin, const Vec3d& direction) const
	{
		if (mNodes.Size == 0) {
			return false;
		}

		int32 stack[scMaxStackDepth];
		uint32 stack_size = 0;

		stack[stack_size++] = 0;

		while (stack_size > 0) {
			const BvhNode& node = mNodes[static_cast<uint32>(stack[--stack_size])];

			if (!HitsBox(node, origin, direction)) {
				continue;
			}

			if (node.Left < 0) {
				for (uint32 i = 0; i < node.Count; i++) {
					if (HitsOccluder(mOccluders[node.First + i], origin, direction)) {
						return true;
					}
				}

				continue;
			}

			if (stack_size + 2 <= scMaxStackDepth) {
				stack[stack_size++] = node.Left;
				stack[stack_size++] = node.Right;
			}
		}

		return false;
	}

private:
	static bool HitsBox(const BvhNode& node, const Vec3d& origin, const Vec3d& direction)
	{
		double enter = 0.0;
		double exit = scNoHit;

		for (int32 axis = 0; axis < 3; axis++) {
			const double start = origin.mData[axis];
			const double speed = direction.mData[axis];

			if (std::abs(speed) < scParallelEpsilon) {
				if (start < node.Min.mData[axis] || start > node.Max.mData[axis]) {
					return false;
				}

				continue;
			}

			const double near_time = (node.Min.mData[axis] - start) / speed;
			const double far_time = (node.Max.mData[axis] - start) / speed;

			enter = std::fmax(enter, std::fmin(near_time, far_time));
			exit = std::fmin(exit, std::fmax(near_time, far_time));
		}

		return enter <= exit;
	}

	bool HitsTriangle(const Occluder& occluder, const Vec3d& origin, const Vec3d& direction) const
	{
		const Vec3d& a = mCorners[static_cast<uint32>(occluder.FirstCorner)];
		const Vec3d edge_ab = mCorners[static_cast<uint32>(occluder.FirstCorner) + 1] - a;
		const Vec3d edge_ac = mCorners[static_cast<uint32>(occluder.FirstCorner) + 2] - a;

		const Vec3d p = direction.Cross(edge_ac);
		const double determinant = edge_ab.Dot(p);

		if (std::abs(determinant) < scParallelEpsilon) {
			return false;
		}

		const double inverse = 1.0 / determinant;
		const Vec3d offset = origin - a;
		const double u = offset.Dot(p) * inverse;

		if (u < 0.0 || u > 1.0) {
			return false;
		}

		const Vec3d q = offset.Cross(edge_ab);
		const double v = direction.Dot(q) * inverse;

		if (v < 0.0 || u + v > 1.0) {
			return false;
		}

		return edge_ac.Dot(q) * inverse > scTriangleMinDistance;
	}

	bool HitsOccluder(const Occluder& occluder, const Vec3d& origin, const Vec3d& direction) const
	{
		if (occluder.FirstCorner >= 0) {
			return HitsTriangle(occluder, origin, direction);
		}

		double enter = 0.0;
		double exit = scNoHit;

		for (uint32 i = 0; i < occluder.PlaneCount; i++) {
			const OccluderPlane& plane = mPlanes[occluder.FirstPlane + i];

			const double distance = plane.Normal.Dot(origin) - plane.Distance;
			const double speed = plane.Normal.Dot(direction);

			if (std::abs(speed) < scParallelEpsilon) {
				if (distance > 0.0) {
					return false;
				}

				continue;
			}

			const double time = -distance / speed;

			if (speed < 0.0) {
				enter = std::fmax(enter, time);
			}
			else {
				exit = std::fmin(exit, time);
			}

			if (enter >= exit) {
				return false;
			}
		}

		return true;
	}

	int32 BuildNode(uint32 first, uint32 count)
	{
		const int32 node_index = static_cast<int32>(mNodes.Size);
		mNodes.Emplace();

		Vec3d min = mOccluders[first].Min;
		Vec3d max = mOccluders[first].Max;

		Vec3d center_min = (min + max) * 0.5;
		Vec3d center_max = center_min;

		for (uint32 i = 1; i < count; i++) {
			const Occluder& occluder = mOccluders[first + i];
			const Vec3d center = (occluder.Min + occluder.Max) * 0.5;

			min = MinVec(min, occluder.Min);
			max = MaxVec(max, occluder.Max);
			center_min = MinVec(center_min, center);
			center_max = MaxVec(center_max, center);
		}

		int32 left = -1;
		int32 right = -1;

		if (count > scLeafSize) {
			const Vec3d extent = center_max - center_min;

			int32 axis = 0;

			for (int32 other = 1; other < 3; other++) {
				if (extent.mData[other] > extent.mData[axis]) {
					axis = other;
				}
			}

			Occluder* begin = mOccluders.pData + first;
			const uint32 half = count / 2;

			std::nth_element(
				begin, begin + half, begin + count, [axis](const Occluder& a, const Occluder& b)
				{ return (a.Min.mData[axis] + a.Max.mData[axis]) < (b.Min.mData[axis] + b.Max.mData[axis]); });

			left = BuildNode(first, half);
			right = BuildNode(first + half, count - half);
		}

		BvhNode& node = mNodes[static_cast<uint32>(node_index)];
		node.Min = min;
		node.Max = max;
		node.Left = left;
		node.Right = right;
		node.First = first;
		node.Count = count;

		return node_index;
	}

private:
	Array<OccluderPlane> mPlanes;
	Array<Vec3d> mCorners;
	Array<Occluder> mOccluders;
	Array<BvhNode> mNodes;
	uint32 mTriangleCount = 0;
};

struct SunSampler
{
	Vec3d Direction = Vec3d(0.0, 1.0, 0.0);
	Vec3d Right = Vec3d(1.0, 0.0, 0.0);
	Vec3d Up = Vec3d(0.0, 0.0, 1.0);

	double Spread = 0.0;

	SunSampler(const Vec3d& direction, double radius_degrees) : Direction(direction)
	{
		const Vec3d reference = (std::abs(direction.Y) < 0.9) ? Vec3d(0.0, 1.0, 0.0) : Vec3d(1.0, 0.0, 0.0);

		Right = reference.Cross(direction).Normalized();
		Up = direction.Cross(Right);

		Spread = std::tan(radius_degrees * (scTwoPi / 360.0));
	}

	Vec3d Sample(uint32 index, uint32 count) const
	{
		uint32 bits = index;
		bits = (bits << 16) | (bits >> 16);
		bits = ((bits & 0x55555555U) << 1) | ((bits & 0xAAAAAAAAU) >> 1);
		bits = ((bits & 0x33333333U) << 2) | ((bits & 0xCCCCCCCCU) >> 2);
		bits = ((bits & 0x0F0F0F0FU) << 4) | ((bits & 0xF0F0F0F0U) >> 4);
		bits = ((bits & 0x00FF00FFU) << 8) | ((bits & 0xFF00FF00U) >> 8);

		const double u = (static_cast<double>(index) + 0.5) / count;
		const double v = static_cast<double>(bits) * (1.0 / 4294967296.0);

		const double radius = std::sqrt(u) * Spread;
		const double angle = v * scTwoPi;

		return (Direction + Right * (std::cos(angle) * radius) + Up * (std::sin(angle) * radius)).Normalized();
	}
};

struct ChartLayout
{
	Vec2d Min = Vec2d(0.0, 0.0);

	uint32 Width = 0;
	uint32 Height = 0;

	uint32 X = 0;
	uint32 Y = 0;

	bool bFacesSun = false;

	uint32 GetPaddedWidth() const { return Width + scChartPadding * 2; }
	uint32 GetPaddedHeight() const { return Height + scChartPadding * 2; }
};

bool MeasureCharts(const Array<LightmapChart>& charts, double density, uint32 max_size, Array<ChartLayout>& out_layouts)
{
	out_layouts.Clear();

	for (const LightmapChart& chart : charts) {
		Vec2d min = chart.TrianglePoints[0];
		Vec2d max = min;

		for (const Vec2d& point : chart.TrianglePoints) {
			min = Vec2d(std::fmin(min.X, point.X), std::fmin(min.Y, point.Y));
			max = Vec2d(std::fmax(max.X, point.X), std::fmax(max.Y, point.Y));
		}

		ChartLayout layout;
		layout.Min = Vec2d(std::floor(min.X * density) / density, std::floor(min.Y * density) / density);

		layout.Width = static_cast<uint32>(std::fmax(1.0, std::ceil((max.X - layout.Min.X) * density - scSizeEpsilon)));
		layout.Height = static_cast<uint32>(
			std::fmax(1.0, std::ceil((max.Y - layout.Min.Y) * density - scSizeEpsilon)));

		if (layout.GetPaddedWidth() > max_size || layout.GetPaddedHeight() > max_size) {
			return false;
		}

		out_layouts.Emplace(layout);
	}

	return true;
}

bool PackCharts(Array<ChartLayout>& layouts, const Array<uint32>& order, uint32 width, uint32 max_height,
				uint32& out_height)
{
	uint32 shelf_x = 0;
	uint32 shelf_y = 0;
	uint32 shelf_height = 0;

	for (const uint32 index : order) {
		ChartLayout& layout = layouts[index];

		if (layout.GetPaddedWidth() > width) {
			return false;
		}

		if (shelf_x + layout.GetPaddedWidth() > width) {
			shelf_y += shelf_height;
			shelf_x = 0;
			shelf_height = 0;
		}

		if (shelf_y + layout.GetPaddedHeight() > max_height) {
			return false;
		}

		layout.X = shelf_x;
		layout.Y = shelf_y;

		shelf_x += layout.GetPaddedWidth();
		shelf_height = std::max(shelf_height, layout.GetPaddedHeight());
	}

	out_height = shelf_y + shelf_height;

	return true;
}

bool LayOutCharts(const Array<LightmapChart>& charts, const LightmapOptions& options, Array<ChartLayout>& out_layouts,
				  uint32& out_width, uint32& out_height, double& out_density)
{
	const uint32 max_size = std::max(options.MaxSize, scMinAtlasSize);

	Array<uint32> order;

	for (double density = options.Density; density >= scMinDensity; density *= scDensityStep) {
		if (!MeasureCharts(charts, density, max_size, out_layouts)) {
			continue;
		}

		order.Clear();

		double area = 0.0;

		for (uint32 i = 0; i < out_layouts.Size; i++) {
			order.Emplace(i);
			area += static_cast<double>(out_layouts[i].GetPaddedWidth()) * out_layouts[i].GetPaddedHeight();
		}

		std::sort(order.begin(), order.end(),
				  [&out_layouts](uint32 a, uint32 b)
				  {
					  if (out_layouts[a].Height != out_layouts[b].Height) {
						  return out_layouts[a].Height > out_layouts[b].Height;
					  }

					  return a < b;
				  });

		uint32 size = scMinAtlasSize;

		while (size < max_size && static_cast<double>(size) * size < area * scAtlasFillEstimate) {
			size *= 2;
		}

		for (; size <= max_size; size *= 2) {
			uint32 used_height = 0;

			if (PackCharts(out_layouts, order, size, size, used_height)) {
				out_width = size;
				out_height = ((used_height + scAtlasHeightStep - 1) / scAtlasHeightStep) * scAtlasHeightStep;
				out_density = density;

				return true;
			}
		}
	}

	return false;
}

struct BakeContext
{
	const PlaneSet& Planes;
	const Array<LightmapChart>& Charts;
	const Array<ChartLayout>& Layouts;
	const OccluderSet& Occluders;
	const SunSampler& Sun;

	double Density = 0.0;
	uint32 Samples = 1;
	uint32 Width = 0;

	const Vec3d* pDirections = nullptr;
	uint32 DirectionsPerSample = 1;

	uint8* pTexels = nullptr;
	uint8* pValid = nullptr;

	std::atomic<uint64> RayCount { 0 };
};

struct RowJob
{
	uint32 Chart = 0;
	uint32 Row = 0;
};

Vec2d GetClosestPointOnSegment(const Vec2d& from, const Vec2d& to, const Vec2d& point)
{
	const double edge_x = to.X - from.X;
	const double edge_y = to.Y - from.Y;
	const double length_sq = edge_x * edge_x + edge_y * edge_y;

	if (length_sq <= 0.0) {
		return from;
	}

	const double fraction = std::fmin(
		std::fmax(((point.X - from.X) * edge_x + (point.Y - from.Y) * edge_y) / length_sq, 0.0), 1.0);

	return Vec2d(from.X + edge_x * fraction, from.Y + edge_y * fraction);
}

bool IsInsideTriangle(const Vec2d corners[3], double x, double y)
{
	bool all_positive = true;
	bool all_negative = true;

	for (uint32 edge = 0; edge < 3; edge++) {
		const Vec2d& from = corners[edge];
		const Vec2d& to = corners[(edge + 1) % 3];

		const double side = (to.X - from.X) * (y - from.Y) - (to.Y - from.Y) * (x - from.X);

		all_positive = all_positive && (side >= -scCoverageEpsilon);
		all_negative = all_negative && (side <= scCoverageEpsilon);
	}

	return all_positive || all_negative;
}

struct RowScratch
{
	Array<uint8> Coverage;
	Array<int32> CoverageTriangles;
	Array<Vec2d> Corners;
	Array<int32> CornerTriangles;
};

bool FindPointInTexel(const Array<Vec2d>& corners, double center_x, double center_y, Vec2d& out_point,
					  uint32& out_corner)
{
	const Vec2d center(center_x, center_y);

	double best_distance = scNoHit;

	for (uint32 triangle = 0; triangle + 2 < corners.Size; triangle += 3) {
		const Vec2d* points = &corners[triangle];

		for (uint32 edge = 0; edge < 3; edge++) {
			const Vec2d closest = GetClosestPointOnSegment(points[edge], points[(edge + 1) % 3], center);
			const double distance = std::fmax(std::abs(closest.X - center_x), std::abs(closest.Y - center_y));

			if (distance < best_distance) {
				best_distance = distance;
				out_point = closest;
				out_corner = triangle;
			}
		}
	}

	return best_distance <= 0.5;
}

struct SurfacePoint
{
	Vec3d Position = Vec3d(0.0);
	Vec3d Normal = Vec3d(0.0);
};

SurfacePoint InterpolateChart(const LightmapChart& chart, const Vec2d texel_corners[3], uint32 first_corner, double x,
							  double y)
{
	const auto signed_area = [](const Vec2d& a, const Vec2d& b, double px, double py)
	{ return (b.X - a.X) * (py - a.Y) - (b.Y - a.Y) * (px - a.X); };

	const double total = signed_area(texel_corners[0], texel_corners[1], texel_corners[2].X, texel_corners[2].Y);

	double weights[3] = { 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0 };

	if (std::abs(total) > scCoverageEpsilon) {
		weights[0] = signed_area(texel_corners[1], texel_corners[2], x, y) / total;
		weights[1] = signed_area(texel_corners[2], texel_corners[0], x, y) / total;
		weights[2] = 1.0 - weights[0] - weights[1];

		double sum = 0.0;

		for (double& weight : weights) {
			weight = std::fmax(weight, 0.0);
			sum += weight;
		}

		for (double& weight : weights) {
			weight /= sum;
		}
	}

	SurfacePoint point;
	Vec3d normal = Vec3d(0.0);

	for (uint32 corner = 0; corner < 3; corner++) {
		point.Position = point.Position + chart.WorldPoints[first_corner + corner] * weights[corner];
		normal = normal + chart.Normals[first_corner + corner] * weights[corner];
	}

	point.Normal = normal.Normalized();

	return point;
}

void BakeRow(BakeContext& context, const RowJob& job, RowScratch& scratch)
{
	const LightmapChart& chart = context.Charts[job.Chart];
	const ChartLayout& layout = context.Layouts[job.Chart];
	const bool is_planar = chart.IsPlanar();

	Vec3d plane_normal = Vec3d(0.0);
	Vec3d plane_origin = Vec3d(0.0);

	if (is_planar) {
		const Plane& plane = context.Planes[chart.PlaneIndex];

		plane_normal = plane.Normal;
		plane_origin = plane.Normal * (plane.Distance + scSurfaceBias);
	}

	const uint32 samples = context.Samples;
	const uint32 padded_width = layout.GetPaddedWidth();
	const uint32 sample_columns = padded_width * samples;

	const double density = context.Density;
	const double sample_step = 1.0 / samples;

	Array<uint8>& coverage = scratch.Coverage;

	Fill(coverage, sample_columns * samples, static_cast<uint8>(0));
	Fill(scratch.CoverageTriangles, sample_columns * samples, -1);
	scratch.Corners.Clear();
	scratch.CornerTriangles.Clear();

	const double row_min = static_cast<double>(job.Row);
	const double row_max = row_min + 1.0;

	for (uint32 triangle = 0; triangle + 2 < chart.TrianglePoints.Size; triangle += 3) {
		Vec2d corners[3];

		for (uint32 corner = 0; corner < 3; corner++) {
			const Vec2d& point = chart.TrianglePoints[triangle + corner];

			corners[corner] = Vec2d((point.X - layout.Min.X) * density + scChartPadding,
									(point.Y - layout.Min.Y) * density + scChartPadding);
		}

		const double min_x = std::fmin(corners[0].X, std::fmin(corners[1].X, corners[2].X));
		const double max_x = std::fmax(corners[0].X, std::fmax(corners[1].X, corners[2].X));
		const double min_y = std::fmin(corners[0].Y, std::fmin(corners[1].Y, corners[2].Y));
		const double max_y = std::fmax(corners[0].Y, std::fmax(corners[1].Y, corners[2].Y));

		if (max_y < row_min || min_y > row_max) {
			continue;
		}

		for (const Vec2d& corner : corners) {
			scratch.Corners.Emplace(corner);
		}

		scratch.CornerTriangles.Emplace(static_cast<int32>(triangle));

		const int64 first_column = std::max<int64>(0, static_cast<int64>(std::floor(min_x * samples - 0.5)));
		const int64 last_column = std::min<int64>(static_cast<int64>(sample_columns) - 1,
												  static_cast<int64>(std::ceil(max_x * samples - 0.5)));

		for (uint32 sample_row = 0; sample_row < samples; sample_row++) {
			const double y = row_min + (sample_row + 0.5) * sample_step;

			if (y < min_y - scCoverageEpsilon || y > max_y + scCoverageEpsilon) {
				continue;
			}

			for (int64 column = first_column; column <= last_column; column++) {
				const double x = (static_cast<double>(column) + 0.5) * sample_step;

				if (IsInsideTriangle(corners, x, y)) {
					coverage[sample_row * sample_columns + static_cast<uint32>(column)] = 1;
					scratch.CoverageTriangles[sample_row * sample_columns + static_cast<uint32>(column)] =
						static_cast<int32>(triangle);
				}
			}
		}
	}

	const uint32 sample_count = samples * samples;

	uint64 ray_count = 0;

	const auto get_origin = [&](double texel_x, double texel_y)
	{
		const double u = layout.Min.X + (texel_x - scChartPadding) / density;
		const double v = layout.Min.Y + (texel_y - scChartPadding) / density;

		return plane_origin + chart.U * u + chart.V * v;
	};

	const uint32 direction_count = context.DirectionsPerSample;

	const auto texel_corners = [&](uint32 first_corner, Vec2d out_corners[3])
	{
		for (uint32 corner = 0; corner < 3; corner++) {
			const Vec2d& point = chart.TrianglePoints[first_corner + corner];

			out_corners[corner] = Vec2d((point.X - layout.Min.X) * density + scChartPadding,
										(point.Y - layout.Min.Y) * density + scChartPadding);
		}
	};

	const auto get_surface = [&](double texel_x, double texel_y, uint32 first_corner)
	{
		Vec2d corners[3];
		texel_corners(first_corner, corners);

		SurfacePoint point = InterpolateChart(chart, corners, first_corner, texel_x, texel_y);
		point.Position = point.Position + point.Normal * scSurfaceBias;

		return point;
	};

	const auto count_lit = [&](const Vec3d& origin, const Vec3d& normal, uint32 sample)
	{
		const Vec3d* directions = context.pDirections + sample * direction_count;

		uint32 lit_count = 0;

		for (uint32 i = 0; i < direction_count; i++) {
			if (normal.Dot(directions[i]) > 0.0 && !context.Occluders.IsOccluded(origin, directions[i])) {
				lit_count++;
			}
		}

		ray_count += direction_count;

		return lit_count;
	};

	for (uint32 column = 0; column < padded_width; column++) {
		const uint32 atlas_x = layout.X + column;
		const uint32 atlas_y = layout.Y + job.Row;
		const uint64 texel = static_cast<uint64>(atlas_y) * context.Width + atlas_x;

		uint32 covered = 0;
		uint32 lit = 0;

		for (uint32 sample = 0; sample < sample_count; sample++) {
			const uint32 sample_x = sample % samples;
			const uint32 sample_y = sample / samples;

			if (coverage[sample_y * sample_columns + column * samples + sample_x] == 0) {
				continue;
			}

			covered += direction_count;

			if (!layout.bFacesSun) {
				continue;
			}

			const double texel_x = column + (sample_x + 0.5) * sample_step;
			const double texel_y = job.Row + (sample_y + 0.5) * sample_step;

			if (is_planar) {
				lit += count_lit(get_origin(texel_x, texel_y), plane_normal, sample);
			}
			else {
				const int32 triangle = scratch
										   .CoverageTriangles[sample_y * sample_columns + column * samples + sample_x];
				const SurfacePoint point = get_surface(texel_x, texel_y, static_cast<uint32>(triangle));

				lit += count_lit(point.Position, point.Normal, sample);
			}
		}

		Vec2d thin_point(0.0, 0.0);
		uint32 thin_corner = 0;

		if (covered == 0 && FindPointInTexel(scratch.Corners, column + 0.5, job.Row + 0.5, thin_point, thin_corner)) {
			covered = sample_count * direction_count;

			Vec3d origin = Vec3d(0.0);
			Vec3d normal = plane_normal;

			if (is_planar) {
				origin = get_origin(thin_point.X, thin_point.Y);
			}
			else {
				const uint32 triangle = static_cast<uint32>(scratch.CornerTriangles[thin_corner / 3]);
				const SurfacePoint point = get_surface(thin_point.X, thin_point.Y, triangle);

				origin = point.Position;
				normal = point.Normal;
			}

			for (uint32 sample = 0; sample < sample_count && layout.bFacesSun; sample++) {
				lit += count_lit(origin, normal, sample);
			}
		}

		if (covered == 0) {
			continue;
		}

		context.pValid[texel] = 1;
		context.pTexels[texel] = static_cast<uint8>((lit * 255 + covered / 2) / covered);
	}

	context.RayCount.fetch_add(ray_count, std::memory_order_relaxed);
}

void DilateChart(const ChartLayout& layout, uint32 atlas_width, uint8* texels, uint8* valid,
				 Array<uint8>& scratch_texels, Array<uint8>& scratch_valid)
{
	const uint32 width = layout.GetPaddedWidth();
	const uint32 height = layout.GetPaddedHeight();

	const auto at = [&](uint32 x, uint32 y) { return static_cast<uint64>(layout.Y + y) * atlas_width + layout.X + x; };

	for (uint32 pass = 0; pass < scDilatePasses; pass++) {
		Fill(scratch_texels, width * height, static_cast<uint8>(0));
		Fill(scratch_valid, width * height, static_cast<uint8>(0));

		bool changed = false;

		for (uint32 y = 0; y < height; y++) {
			for (uint32 x = 0; x < width; x++) {
				if (valid[at(x, y)] != 0) {
					continue;
				}

				uint32 sum = 0;
				uint32 count = 0;

				for (int32 offset_y = -1; offset_y <= 1; offset_y++) {
					for (int32 offset_x = -1; offset_x <= 1; offset_x++) {
						const int64 near_x = static_cast<int64>(x) + offset_x;
						const int64 near_y = static_cast<int64>(y) + offset_y;

						if (near_x < 0 || near_y < 0 || near_x >= width || near_y >= height) {
							continue;
						}

						const uint64 near = at(static_cast<uint32>(near_x), static_cast<uint32>(near_y));

						if (valid[near] != 0) {
							sum += texels[near];
							count++;
						}
					}
				}

				if (count > 0) {
					scratch_texels[y * width + x] = static_cast<uint8>((sum + count / 2) / count);
					scratch_valid[y * width + x] = 1;
					changed = true;
				}
			}
		}

		if (!changed) {
			break;
		}

		for (uint32 y = 0; y < height; y++) {
			for (uint32 x = 0; x < width; x++) {
				if (scratch_valid[y * width + x] != 0) {
					texels[at(x, y)] = scratch_texels[y * width + x];
					valid[at(x, y)] = 1;
				}
			}
		}
	}
}

bool WriteImage(const String& path, const CompiledMapLightmap& lightmap)
{
	std::FILE* file = std::fopen(path.CStr(), "wb");

	if (file == nullptr) {
		return false;
	}

	std::fprintf(file, "P5\n%u %u\n255\n", lightmap.Width, lightmap.Height);

	const size_t written = std::fwrite(lightmap.SunVisibility.pData, 1, lightmap.SunVisibility.Size, file);
	const bool closed = (std::fclose(file) == 0);

	return written == lightmap.SunVisibility.Size && closed;
}

} // namespace

bool LightmapBaker::Bake(const PlaneSet& planes, const MapSource& source, const SurfaceBuilder& surfaces,
						 const PatchBuilder* patches, const LightmapOptions& options, CompiledMap& out_map)
{
	Stats = LightmapStats {};

	const Array<LightmapChart>& charts = surfaces.Charts;

	if (!options.bEnabled || !source.bHasSun || charts.Size == 0) {
		return false;
	}

	Array<ChartLayout> layouts;

	uint32 width = 0;
	uint32 height = 0;
	double density = 0.0;

	if (!LayOutCharts(charts, options, layouts, width, height, density)) {
		return false;
	}

	OccluderSet occluders;
	occluders.Build(planes, source, patches);

	const SunSampler sun(source.SunDirection, options.SunRadiusDegrees);

	Array<RowJob> jobs;

	for (uint32 i = 0; i < charts.Size; i++) {
		if (charts[i].IsPlanar()) {
			layouts[i].bFacesSun = planes[charts[i].PlaneIndex].Normal.Dot(source.SunDirection) > 0.0;
		}
		else {
			for (const Vec3d& normal : charts[i].Normals) {
				layouts[i].bFacesSun = layouts[i].bFacesSun || normal.Dot(source.SunDirection) > 0.0;
			}
		}

		for (uint32 row = 0; row < layouts[i].GetPaddedHeight(); row++) {
			jobs.Emplace(RowJob { i, row });
		}
	}

	const uint64 texel_count = static_cast<uint64>(width) * height;

	CompiledMapLightmap& lightmap = out_map.Lightmap;
	lightmap.Width = width;
	lightmap.Height = height;
	lightmap.SunVisibility.InitSize(texel_count);

	SizedArray<uint8> valid;
	valid.InitSize(texel_count);

	std::fill(lightmap.SunVisibility.begin(), lightmap.SunVisibility.end(), scFullyLit);
	std::fill(valid.begin(), valid.end(), static_cast<uint8>(0));

	lightmap.SunDirection[0] = static_cast<float32>(source.SunDirection.X);
	lightmap.SunDirection[1] = static_cast<float32>(source.SunDirection.Y);
	lightmap.SunDirection[2] = static_cast<float32>(source.SunDirection.Z);

	BakeContext context { planes, charts, layouts, occluders, sun };
	context.Density = density;
	context.Samples = std::max<uint32>(options.Samples, 1);
	context.Width = width;
	context.pTexels = lightmap.SunVisibility.pData;
	context.pValid = valid.pData;

	const uint32 direction_total = context.Samples * context.Samples * context.Samples;

	SizedArray<Vec3d> directions;
	directions.InitSize(direction_total);

	for (uint32 i = 0; i < direction_total; i++) {
		directions[i] = sun.Sample((i * scDirectionShuffle) % direction_total, direction_total);
	}

	context.pDirections = directions.pData;
	context.DirectionsPerSample = context.Samples;

	std::atomic<uint32> next_job { 0 };

	const auto worker = [&]()
	{
		RowScratch scratch;

		while (true) {
			const uint32 first = next_job.fetch_add(scRowsPerClaim, std::memory_order_relaxed);

			if (first >= jobs.Size) {
				break;
			}

			const uint32 last = std::min<uint32>(first + scRowsPerClaim, jobs.Size);

			for (uint32 i = first; i < last; i++) {
				BakeRow(context, jobs[i], scratch);
			}
		}
	};

	const uint32 thread_count = std::max<uint32>(std::thread::hardware_concurrency(), 1);

	SizedArray<std::thread> threads;
	threads.InitCapacity(thread_count);

	for (uint32 i = 1; i < thread_count; i++) {
		threads.Emplace(worker);
	}

	worker();

	for (std::thread& thread : threads) {
		thread.join();
	}

	uint64 valid_count = 0;
	uint64 lit_sum = 0;

	for (uint64 i = 0; i < texel_count; i++) {
		if (valid[i] != 0) {
			valid_count++;
			lit_sum += lightmap.SunVisibility[i];
		}
	}

	Array<uint8> scratch_texels;
	Array<uint8> scratch_valid;

	for (const ChartLayout& layout : layouts) {
		DilateChart(layout, width, lightmap.SunVisibility.pData, valid.pData, scratch_texels, scratch_valid);
	}

	for (size_t surface_index = 0; surface_index < out_map.Surfaces.Size; surface_index++) {
		CompiledMapSurface& surface = out_map.Surfaces[surface_index];
		const Array<LightmapVertex>& vertices = surfaces.LightmapVertices[static_cast<uint32>(surface_index)];

		bool has_charts = vertices.Size == surface.Vertices.Size;

		for (const LightmapVertex& vertex : vertices) {
			has_charts = has_charts && vertex.Chart >= 0;
		}

		if (!has_charts) {
			continue;
		}

		surface.LightmapTexcoords.InitSize(surface.Vertices.Size * 2);

		for (uint32 i = 0; i < vertices.Size; i++) {
			const LightmapVertex& vertex = vertices[i];
			const ChartLayout& layout = layouts[static_cast<uint32>(vertex.Chart)];

			const double x = layout.X + scChartPadding + (vertex.Point.X - layout.Min.X) * density;
			const double y = layout.Y + scChartPadding + (vertex.Point.Y - layout.Min.Y) * density;

			surface.LightmapTexcoords[i * 2] = static_cast<float32>(x / width);
			surface.LightmapTexcoords[i * 2 + 1] = static_cast<float32>(y / height);
		}
	}

	const uint32 probe_count = out_map.Probes.GetProbeCount();
	const uint32 probe_strata = std::max<uint32>(
		static_cast<uint32>(std::ceil(std::sqrt(static_cast<double>(std::max<uint32>(options.ProbeRays, 1))))), 1);
	const uint32 probe_rays = probe_strata * probe_strata;

	out_map.ProbeSunVisibility.InitSize(probe_count);

	for (uint32 probe = 0; probe < probe_count; probe++) {
		const Vec3f position = out_map.Probes.Positions[probe];
		const Vec3d origin(position.X, position.Y, position.Z);

		uint32 lit = 0;

		for (uint32 ray = 0; ray < probe_rays; ray++) {
			if (!occluders.IsOccluded(origin, sun.Sample(ray, probe_rays))) {
				lit++;
			}
		}

		out_map.ProbeSunVisibility[probe] = static_cast<uint8>((lit * 255 + probe_rays / 2) / probe_rays);

		Stats.LitProbeCount += (lit * 2 >= probe_rays) ? 1 : 0;
	}

	Stats.Width = width;
	Stats.Height = height;
	Stats.ChartCount = charts.Size;
	Stats.OccluderCount = occluders.GetCount();
	Stats.OccluderTriangleCount = occluders.GetTriangleCount();
	Stats.Density = density;
	Stats.TexelCount = valid_count;
	Stats.RayCount = context.RayCount.load() + static_cast<uint64>(probe_count) * probe_rays;
	Stats.ProbeCount = probe_count;
	Stats.LitFraction = 0.0;

	if (valid_count > 0) {
		Stats.LitFraction = static_cast<double>(lit_sum) / (255.0 * valid_count);
	}

	if (!options.ImagePath.IsEmpty()) {
		WriteImage(options.ImagePath, lightmap);
	}

	return true;
}

} // namespace fx::mapc
