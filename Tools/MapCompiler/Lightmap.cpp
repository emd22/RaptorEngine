#include "Lightmap.hpp"

#include "IndirectLight.hpp"
#include "Occluders.hpp"

#include <Math/MathConsts.hpp>
#include <Math/MathUtil.hpp>
#include <Math/Rect.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>

namespace fx::mapc {

namespace {

constexpr uint32 scChartPadding = 1;
constexpr double scCoverageEpsilon = 1e-9;
constexpr uint32 scBuriedRays = 16;


DVec3 GetHemisphereDirection(uint32 index, uint32 count)
{
	const double u = (static_cast<double>(index) + 0.5) / count;
	const double radius = std::sqrt(u);
	const double angle = MathUtil::RadicalInverse<double>(index) * FX_2PI;

	return DVec3(std::cos(angle) * radius, std::sin(angle) * radius, std::sqrt(1.0 - u));
}

struct SunSampler
{
public:
	SunSampler(const DVec3& direction, double radius_degrees) : Direction(direction)
	{
		DVec3 reference(1.0, 0.0, 0.0);

		if (std::abs(direction.Y) < 0.9) {
			reference = DVec3(0.0, 1.0, 0.0);
		}

		Right = reference.Cross(direction).Normalized();
		Up = direction.Cross(Right);

		Spread = std::tan(radius_degrees * (FX_2PI / 360.0));
	}

	DVec3 Sample(uint32 index, uint32 count) const
	{
		const double u = (static_cast<double>(index) + 0.5) / count;
		const double v = MathUtil::RadicalInverse<double>(index);

		const double radius = std::sqrt(u) * Spread;
		const double angle = v * FX_2PI;

		return (Direction + Right * (std::cos(angle) * radius) + Up * (std::sin(angle) * radius)).Normalized();
	}

public:
	DVec3 Direction = DVec3(0.0, 1.0, 0.0);
	DVec3 Right = DVec3(1.0, 0.0, 0.0);
	DVec3 Up = DVec3(0.0, 0.0, 1.0);

	double Spread = 0.0;
};

struct ChartLayout
{
public:
	uint32 GetPaddedWidth() const { return Width + scChartPadding * 2; }
	uint32 GetPaddedHeight() const { return Height + scChartPadding * 2; }

	Vec2u GetSize() const { return Vec2u { Width, Height }; }
	Vec2u GetPosition() const { return Vec2u { X, Y }; }


	Vec2u GetPaddedSize() const
	{
		// Padding is for all sides of the chart. Multiply by 2 for this.
		// e.g. Padding on left and right, padding on top and bottom
		const Vec2u padding_size = Vec2u(scChartPadding * 2u);

		return (GetSize() + padding_size);
	}

public:
	Vec2d Min = Vec2d(0.0, 0.0);

	// TODO: Convert to Vec2u's. Note that floats would cause us to lose a good amount of precision here.

	uint32 Width = 0;
	uint32 Height = 0;

	uint32 X = 0;
	uint32 Y = 0;


	bool bFacesSun = false;
};

bool MeasureCharts(const Array<LightmapChart>& charts, double density, uint32 max_size, Array<ChartLayout>& out_layouts)
{
	constexpr double scSizeEpsilon = 1e-6;

	out_layouts.Clear();

	for (const LightmapChart& chart : charts) {
		DRect bounds;

		for (const Vec2d& point : chart.TrianglePoints) {
			bounds.Min = Vec2d::Min(bounds.Min, point);
			bounds.Max = Vec2d::Max(bounds.Max, point);
		}

		ChartLayout layout;
		layout.Min = Vec2d(std::floor(bounds.Min.X * density) / density, std::floor(bounds.Min.Y * density) / density);

		layout.Width = static_cast<uint32>(
			std::fmax(1.0, std::ceil((bounds.Max.X - layout.Min.X) * density - scSizeEpsilon)));
		layout.Height = static_cast<uint32>(
			std::fmax(1.0, std::ceil((bounds.Max.Y - layout.Min.Y) * density - scSizeEpsilon)));

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
	constexpr uint32 scMinAtlasSize = 64;
	constexpr uint32 scAtlasHeightStep = 4;
	constexpr double scAtlasFillEstimate = 1.15;
	constexpr double scDensityStep = 0.8;
	constexpr double scMinDensity = 0.25;

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

	const DVec3* pDirections = nullptr;
	uint32 DirectionsPerSample = 1;

	const DVec3* pBuriedDirections = nullptr;

	uint8* pTexels = nullptr;
	uint8* pValid = nullptr;

	FVec3* pPositions = nullptr;
	FVec3* pNormals = nullptr;

	std::atomic<uint64> RayCount { 0 };
};

struct RowJob
{
	uint32 Chart = 0;
	uint32 Row = 0;
};

Vec2d GetClosestPointOnSegment(const Vec2d& from, const Vec2d& to, const Vec2d& point)
{
	const Vec2d edge = to - from;
	const double length_sq = edge.LengthSquared();

	if (length_sq <= 0.0) {
		return from;
	}

	const double fraction = std::fmin(std::fmax((point - from).Dot(edge) / length_sq, 0.0), 1.0);

	return from + edge * fraction;
}

bool IsInsideTriangle(const Vec2d corners[3], double x, double y)
{
	bool all_positive = true;
	bool all_negative = true;

	for (uint32 edge = 0; edge < 3; edge++) {
		const Vec2d& from = corners[edge];
		const Vec2d& to = corners[(edge + 1) % 3];

		const double side = to.CrossOffset(Vec2d(x, y), from);

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
	DVec3 Position = DVec3(0.0);
	DVec3 Normal = DVec3(0.0);
};

SurfacePoint InterpolateChart(const LightmapChart& chart, const Vec2d texel_corners[3], uint32 first_corner, double x,
							  double y)
{
	const double total = texel_corners[1].CrossOffset(texel_corners[2], texel_corners[0]);

	DVec3 weights(1.0 / 3.0);

	if (std::abs(total) > scCoverageEpsilon) {
		weights.X = texel_corners[2].CrossOffset(Vec2d(x, y), texel_corners[1]) / total;
		weights.Y = texel_corners[0].CrossOffset(Vec2d(x, y), texel_corners[2]) / total;
		weights.Z = 1.0 - weights.X - weights.Y;

		double sum = 0.0;

		for (uint32 corner = 0; corner < 3; corner++) {
			weights.mData[corner] = std::fmax(weights.mData[corner], 0.0);
			sum += weights.mData[corner];
		}

		for (uint32 corner = 0; corner < 3; corner++) {
			weights.mData[corner] /= sum;
		}
	}

	SurfacePoint point;
	DVec3 normal = DVec3(0.0);

	for (uint32 corner = 0; corner < 3; corner++) {
		point.Position = point.Position + chart.WorldPoints[first_corner + corner] * weights.mData[corner];
		normal = normal + chart.Normals[first_corner + corner] * weights.mData[corner];
	}

	point.Normal = normal.Normalized();

	return point;
}

Vec2d GetTexelPoint(const Vec2d& point, const ChartLayout& layout, double density)
{
	return Vec2d((point.X - layout.Min.X) * density + scChartPadding,
				 (point.Y - layout.Min.Y) * density + scChartPadding);
}

void BakeRow(BakeContext& context, const RowJob& job, RowScratch& scratch)
{
	constexpr double scSurfaceBias = 1e-3;
	constexpr double scThinInset = 0.05;

	const LightmapChart& chart = context.Charts[job.Chart];
	const ChartLayout& layout = context.Layouts[job.Chart];
	const bool is_planar = chart.IsPlanar();

	DVec3 plane_normal = DVec3(0.0);
	DVec3 plane_origin = DVec3(0.0);

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
			corners[corner] = GetTexelPoint(chart.TrianglePoints[triangle + corner], layout, density);
		}

		DRect bounds;

		for (const Vec2d& corner : corners) {
			bounds.Min = Vec2d::Min(bounds.Min, corner);
			bounds.Max = Vec2d::Max(bounds.Max, corner);
		}

		if (bounds.Max.Y < row_min || bounds.Min.Y > row_max) {
			continue;
		}

		for (const Vec2d& corner : corners) {
			scratch.Corners.Emplace(corner);
		}

		scratch.CornerTriangles.Emplace(static_cast<int32>(triangle));

		const int64 first_column = std::max<int64>(0, static_cast<int64>(std::floor(bounds.Min.X * samples - 0.5)));
		const int64 last_column = std::min<int64>(static_cast<int64>(sample_columns) - 1,
												  static_cast<int64>(std::ceil(bounds.Max.X * samples - 0.5)));

		for (uint32 sample_row = 0; sample_row < samples; sample_row++) {
			const double y = row_min + (sample_row + 0.5) * sample_step;

			if (y < bounds.Min.Y - scCoverageEpsilon || y > bounds.Max.Y + scCoverageEpsilon) {
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
			out_corners[corner] = GetTexelPoint(chart.TrianglePoints[first_corner + corner], layout, density);
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

	const auto is_buried = [&](const DVec3& origin, const DVec3& normal)
	{
		ray_count += scBuriedRays;

		return context.Occluders.IsBuried(origin, normal, context.pBuriedDirections, scBuriedRays);
	};

	const auto count_lit = [&](const DVec3& origin, const DVec3& normal, uint32 sample)
	{
		const DVec3* directions = context.pDirections + sample * direction_count;

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
		bool touches_polygon = false;

		DVec3 position_sum = DVec3(0.0);
		DVec3 normal_sum = DVec3(0.0);
		uint32 position_count = 0;

		DVec3 nearest_position = DVec3(0.0);
		double nearest_distance = scNoHit;

		for (uint32 sample = 0; sample < sample_count; sample++) {
			const uint32 sample_x = sample % samples;
			const uint32 sample_y = sample / samples;

			if (coverage[sample_y * sample_columns + column * samples + sample_x] == 0) {
				continue;
			}

			touches_polygon = true;

			const double texel_x = column + (sample_x + 0.5) * sample_step;
			const double texel_y = job.Row + (sample_y + 0.5) * sample_step;

			DVec3 origin = DVec3(0.0);
			DVec3 normal = plane_normal;

			if (is_planar) {
				origin = get_origin(texel_x, texel_y);
			}
			else {
				const int32 triangle = scratch
										   .CoverageTriangles[sample_y * sample_columns + column * samples + sample_x];
				const SurfacePoint point = get_surface(texel_x, texel_y, static_cast<uint32>(triangle));

				origin = point.Position;
				normal = point.Normal;
			}

			if (is_buried(origin, normal)) {
				continue;
			}

			covered += direction_count;

			position_sum = position_sum + origin;
			normal_sum = normal_sum + normal;
			position_count++;

			if (!is_planar) {
				const double offset_x = (sample_x + 0.5) * sample_step - 0.5;
				const double offset_y = (sample_y + 0.5) * sample_step - 0.5;
				const double distance = offset_x * offset_x + offset_y * offset_y;

				if (distance < nearest_distance) {
					nearest_distance = distance;
					nearest_position = origin;
				}
			}

			if (layout.bFacesSun) {
				lit += count_lit(origin, normal, sample);
			}
		}

		Vec2d thin_point(0.0, 0.0);
		uint32 thin_corner = 0;

		if (!touches_polygon &&
			FindPointInTexel(scratch.Corners, column + 0.5, job.Row + 0.5, thin_point, thin_corner)) {
			const Vec2d* thin_corners = &scratch.Corners[thin_corner];
			const Vec2d inward = (thin_corners[0] + thin_corners[1] + thin_corners[2]) / 3.0 - thin_point;
			const double inward_length = inward.Length();

			if (inward_length > 0.0) {
				const double step = std::fmin(scThinInset, inward_length * 0.5) / inward_length;
				thin_point = thin_point + inward * step;
			}

			DVec3 origin = DVec3(0.0);
			DVec3 normal = plane_normal;

			if (is_planar) {
				origin = get_origin(thin_point.X, thin_point.Y);
			}
			else {
				const uint32 triangle = static_cast<uint32>(scratch.CornerTriangles[thin_corner / 3]);
				const SurfacePoint point = get_surface(thin_point.X, thin_point.Y, triangle);

				origin = point.Position;
				normal = point.Normal;
			}

			if (!is_buried(origin, normal)) {
				covered = sample_count * direction_count;

				position_sum = origin;
				normal_sum = normal;
				position_count = 1;
				nearest_position = origin;

				for (uint32 sample = 0; sample < sample_count && layout.bFacesSun; sample++) {
					lit += count_lit(origin, normal, sample);
				}
			}
		}

		if (covered == 0) {
			continue;
		}

		context.pValid[texel] = 1;
		context.pTexels[texel] = static_cast<uint8>((lit * 255 + covered / 2) / covered);

		if (context.pPositions != nullptr) {
			// Averaging points across a curved chart's triangles can sink behind the surface
			if (is_planar) {
				context.pPositions[texel] = (position_sum * (1.0 / position_count)).ToFVec3();
			}
			else {
				context.pPositions[texel] = nearest_position.ToFVec3();
			}

			context.pNormals[texel] = normal_sum.Normalized().ToFVec3();
		}
	}

	context.RayCount.fetch_add(ray_count, std::memory_order_relaxed);
}

void DilateChart(const ChartLayout& layout, uint32 atlas_width, uint8* texels, uint8* valid,
				 Array<uint8>& scratch_texels, Array<uint8>& scratch_valid)
{
	const uint32 width = layout.GetPaddedWidth();
	const uint32 height = layout.GetPaddedHeight();

	const auto at = [&](uint32 x, uint32 y) { return static_cast<uint64>(layout.Y + y) * atlas_width + layout.X + x; };

	for (uint32 pass = 0; pass < width + height; pass++) {
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
						 const PatchBuilder* patches, const ModelBuilder* models, const LightmapOptions& options,
						 CompiledMap& out_map)
{
	constexpr uint32 scRowsPerClaim = 8;
	constexpr uint32 scDirectionShuffle = 7919;
	constexpr uint8 scFullyLit = 255;

	Stats = LightmapStats {};

	const Array<LightmapChart>& charts = surfaces.Charts;

	if (!options.bEnabled || charts.Size == 0 || (!source.bHasSun && !options.bIndirect)) {
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
	occluders.Build(planes, source, patches, models);

	const SunSampler sun(source.SunDirection, options.SunRadiusDegrees);

	Array<RowJob> jobs;

	for (uint32 i = 0; i < charts.Size && source.bHasSun; i++) {
		if (charts[i].IsPlanar()) {
			layouts[i].bFacesSun = planes[charts[i].PlaneIndex].Normal.Dot(source.SunDirection) > 0.0;
		}
		else {
			for (const DVec3& normal : charts[i].Normals) {
				layouts[i].bFacesSun = layouts[i].bFacesSun || normal.Dot(source.SunDirection) > 0.0;
			}
		}
	}

	for (uint32 i = 0; i < charts.Size; i++) {
		for (uint32 row = 0; row < layouts[i].GetPaddedHeight(); row++) {
			jobs.Emplace(RowJob { i, row });
		}
	}

	const uint64 texel_count = static_cast<uint64>(width) * height;

	CompiledMapLightmap& lightmap = out_map.Lightmap;
	lightmap.Width = width;
	lightmap.Height = height;
	lightmap.Flags = 0;

	if (source.bHasSun) {
		lightmap.Flags |= CompiledMapLightmap::scFlagSun;
	}
	lightmap.SunVisibility.InitSize(texel_count, scFullyLit);

	SizedArray<uint8> valid;
	valid.InitSize(texel_count, static_cast<uint8>(0));

	lightmap.SunDirection[0] = static_cast<float32>(source.SunDirection.X);
	lightmap.SunDirection[1] = static_cast<float32>(source.SunDirection.Y);
	lightmap.SunDirection[2] = static_cast<float32>(source.SunDirection.Z);

	BakeContext context { planes, charts, layouts, occluders, sun };
	context.Density = density;
	context.Samples = std::max<uint32>(options.Samples, 1);
	context.Width = width;
	context.pTexels = lightmap.SunVisibility.pData;
	context.pValid = valid.pData;

	SizedArray<FVec3> positions;
	SizedArray<FVec3> normals;

	if (options.bIndirect) {
		positions.InitSize(texel_count);
		normals.InitSize(texel_count);

		context.pPositions = positions.pData;
		context.pNormals = normals.pData;
	}

	const uint32 direction_total = context.Samples * context.Samples * context.Samples;

	SizedArray<DVec3> directions;
	directions.InitSize(direction_total);

	for (uint32 i = 0; i < direction_total; i++) {
		directions[i] = sun.Sample((i * scDirectionShuffle) % direction_total, direction_total);
	}

	context.pDirections = directions.pData;
	context.DirectionsPerSample = context.Samples;

	SizedArray<DVec3> buried_directions;
	buried_directions.InitSize(scBuriedRays);

	for (uint32 i = 0; i < scBuriedRays; i++) {
		buried_directions[i] = GetHemisphereDirection(i, scBuriedRays);
	}

	context.pBuriedDirections = buried_directions.pData;

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

	SizedArray<uint8> covered;
	covered.InitAsCopyOf(valid);

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

	if (options.bIndirect) {
		// Array<LightmapRect> rects;
		IndirectInput input;
		input.LightmapRects.InitCapacity(layouts.Size);

		for (const ChartLayout& layout : layouts) {
			input.LightmapRects.Insert(URect::FromRelative(layout.GetPosition(), layout.GetPaddedSize()));

			// rects.Emplace(LightmapRect { layout.X, layout.Y, layout.GetPaddedWidth(), layout.GetPaddedHeight() });
		}

		input.Width = width;
		input.Height = height;
		input.TexelSize = static_cast<float32>(1.0 / density);
		input.pCovered = covered.pData;
		input.pSunVisibility = lightmap.SunVisibility.pData;
		input.pPositions = positions.pData;
		input.pNormals = normals.pData;
		input.pPlanes = &planes;

		// input.pRects = rects.pData;
		// input.RectCount = rects.Size;

		BakeIndirectLight(source, surfaces, models, input, options, out_map, Stats);
	}

	uint32 probe_count = 0;

	if (source.bHasSun) {
		probe_count = out_map.Probes.GetProbeCount();
	}

	const uint32 probe_strata = std::max<uint32>(
		static_cast<uint32>(std::ceil(std::sqrt(static_cast<double>(std::max<uint32>(options.ProbeRays, 1))))), 1);
	const uint32 probe_rays = probe_strata * probe_strata;

	out_map.ProbeSunVisibility.InitSize(probe_count);

	for (uint32 probe = 0; probe < probe_count; probe++) {
		const FVec3 position = out_map.Probes.Positions[probe];
		const DVec3 origin(position.X, position.Y, position.Z);

		uint32 lit = 0;

		for (uint32 ray = 0; ray < probe_rays; ray++) {
			if (!occluders.IsOccluded(origin, sun.Sample(ray, probe_rays))) {
				lit++;
			}
		}

		out_map.ProbeSunVisibility[probe] = static_cast<uint8>((lit * 255 + probe_rays / 2) / probe_rays);

		if (lit * 2 >= probe_rays) {
			Stats.LitProbeCount++;
		}
	}

	if (source.bHasSun) {
		Stats.bHasFogShadow = BakeFogShadow(occluders, source.SunDirection, options.FogShadow, out_map.FogShadow,
											Stats.FogShadow);
	}

	Stats.Width = width;
	Stats.Height = height;
	Stats.ChartCount = charts.Size;
	Stats.OccluderCount = occluders.GetCount();
	Stats.OccluderTriangleCount = occluders.GetTriangleCount();
	Stats.OccluderModelTriangleCount = occluders.GetModelTriangleCount();
	Stats.Density = density;
	Stats.TexelCount = valid_count;
	Stats.RayCount = context.RayCount.load() + static_cast<uint64>(probe_count) * probe_rays;
	Stats.ProbeCount = out_map.Probes.GetProbeCount();
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
