#include "FogShadow.hpp"

#include "Occluders.hpp"

#include <atomic>
#include <thread>

namespace fx::mapc {

namespace {

constexpr uint16 scUnshadowed = 0xFFFF;
constexpr uint16 scMaxShadowedDepth = 0xFFFE;
constexpr double scDepthMargin = 1.0;
constexpr double scMinSunHeight = 0.05;
constexpr double scMinHorizontal = 1e-6;
constexpr uint32 scRowsPerClaim = 4;

struct PageFrame
{
	int32 CellX = 0;
	int32 CellZ = 0;

	double LightMin[2] = {};
	double LightSize[2] = {};

	double DepthMin = 0.0;
	double DepthMax = 0.0;

	uint32 Width = 0;
	uint32 Height = 0;

	SizedArray<uint16> Depths;
};

struct RowJob
{
	uint32 Page = 0;
	uint32 Row = 0;
};

struct LightBasis
{
	Vec3d Right = Vec3d(0.0);
	Vec3d Up = Vec3d(0.0);
	Vec3d Forward = Vec3d(0.0);

	Vec3d ToWorld(double x, double y, double depth) const { return Right * x + Up * y + Forward * depth; }
};

LightBasis MakeLightBasis(const Vec3d& sun_direction)
{
	LightBasis basis;
	basis.Forward = (sun_direction * -1.0).Normalized();

	Vec3d reference(0.0, 1.0, 0.0);

	if (std::abs(basis.Forward.Y) > 0.9) {
		reference = Vec3d(0.0, 0.0, 1.0);
	}

	basis.Right = reference.Cross(basis.Forward).Normalized();
	basis.Up = basis.Forward.Cross(basis.Right);

	return basis;
}

uint32 RoundUpToMultiple(uint32 value, uint32 multiple) { return ((value + multiple - 1) / multiple) * multiple; }

uint32 GetLevelMultiple()
{
	uint32 multiple = 1;

	for (uint32 level = 1; level < CompiledMapFogShadow::scLevelCount; level++) {
		multiple *= CompiledMapFogShadow::scLevelStep;
	}

	return multiple;
}

PageFrame MakePageFrame(const LightBasis& basis, int32 cell_x, int32 cell_z, double page_size, double min_y,
						double max_y, const FogShadowOptions& options)
{
	PageFrame frame;
	frame.CellX = cell_x;
	frame.CellZ = cell_z;

	const double x0 = cell_x * page_size;
	const double z0 = cell_z * page_size;

	double light_min[2] = { scNoHit, scNoHit };
	double light_max[2] = { -scNoHit, -scNoHit };

	frame.DepthMin = scNoHit;
	frame.DepthMax = -scNoHit;

	for (uint32 corner = 0; corner < 8; corner++) {
		const Vec3d point(x0 + ((corner & 1) ? page_size : 0.0), (corner & 2) ? max_y : min_y,
						  z0 + ((corner & 4) ? page_size : 0.0));

		const double light_x = basis.Right.Dot(point);
		const double light_y = basis.Up.Dot(point);
		const double depth = basis.Forward.Dot(point);

		light_min[0] = std::min(light_min[0], light_x);
		light_min[1] = std::min(light_min[1], light_y);
		light_max[0] = std::max(light_max[0], light_x);
		light_max[1] = std::max(light_max[1], light_y);
		frame.DepthMin = std::min(frame.DepthMin, depth);
		frame.DepthMax = std::max(frame.DepthMax, depth);
	}

	const uint32 multiple = GetLevelMultiple();
	const uint32 max_size = std::max(RoundUpToMultiple(options.MaxLevelSize, multiple), multiple);

	for (uint32 axis = 0; axis < 2; axis++) {
		frame.LightMin[axis] = light_min[axis];
		frame.LightSize[axis] = std::max(light_max[axis] - light_min[axis], options.TexelSize);
	}

	const uint32 width = static_cast<uint32>(std::ceil(frame.LightSize[0] / options.TexelSize));
	const uint32 height = static_cast<uint32>(std::ceil(frame.LightSize[1] / options.TexelSize));

	frame.Width = std::min(RoundUpToMultiple(std::max(width, 1U), multiple), max_size);
	frame.Height = std::min(RoundUpToMultiple(std::max(height, 1U), multiple), max_size);

	return frame;
}

void TraceRow(const OccluderSet& occluders, const LightBasis& basis, double start_depth, uint32 samples,
			  PageFrame& frame, uint32 row)
{
	const double texel_x = frame.LightSize[0] / frame.Width;
	const double texel_y = frame.LightSize[1] / frame.Height;
	const double depth_range = frame.DepthMax - frame.DepthMin;
	const double max_time = frame.DepthMax - start_depth;
	const double inv_samples = 1.0 / samples;

	for (uint32 column = 0; column < frame.Width; column++) {
		double nearest = scNoHit;

		for (uint32 sample_y = 0; sample_y < samples; sample_y++) {
			const double light_y = frame.LightMin[1] + (row + (sample_y + 0.5) * inv_samples) * texel_y;

			for (uint32 sample_x = 0; sample_x < samples; sample_x++) {
				const double light_x = frame.LightMin[0] + (column + (sample_x + 0.5) * inv_samples) * texel_x;

				const Vec3d origin = basis.ToWorld(light_x, light_y, start_depth);
				const double time = occluders.TraceClosest(origin, basis.Forward, max_time);

				nearest = std::min(nearest, time);
			}
		}

		uint16 value = scUnshadowed;

		if (nearest != scNoHit) {
			const double depth = start_depth + nearest;
			const double normalized = std::clamp((depth - frame.DepthMin) / depth_range, 0.0, 1.0);

			value = static_cast<uint16>(std::lround(normalized * scMaxShadowedDepth));
		}

		frame.Depths[static_cast<size_t>(row) * frame.Width + column] = value;
	}
}

CompiledMapFogShadowLevel ReduceLevel(const CompiledMapFogShadowLevel& source)
{
	const uint32 step = CompiledMapFogShadow::scLevelStep;

	CompiledMapFogShadowLevel level;
	level.Width = std::max(source.Width / step, 1U);
	level.Height = std::max(source.Height / step, 1U);
	level.Depths.InitSize(static_cast<size_t>(level.Width) * level.Height);

	for (uint32 y = 0; y < level.Height; y++) {
		for (uint32 x = 0; x < level.Width; x++) {
			uint16 nearest = scUnshadowed;

			for (uint32 source_y = y * step; source_y < std::min((y + 1) * step, source.Height); source_y++) {
				for (uint32 source_x = x * step; source_x < std::min((x + 1) * step, source.Width); source_x++) {
					nearest = std::min(nearest, source.Depths[static_cast<size_t>(source_y) * source.Width + source_x]);
				}
			}

			level.Depths[static_cast<size_t>(y) * level.Width + x] = nearest;
		}
	}

	return level;
}

} // namespace

bool BakeFogShadow(const OccluderSet& occluders, const Vec3d& sun_direction, const FogShadowOptions& options,
				   CompiledMapFogShadow& out_shadow, FogShadowStats& out_stats)
{
	out_shadow = CompiledMapFogShadow {};
	out_stats = FogShadowStats {};

	Vec3d bounds_min(0.0);
	Vec3d bounds_max(0.0);

	if (!options.bEnabled || options.PageSize <= 0.0 || options.TexelSize <= 0.0 ||
		!occluders.GetBounds(bounds_min, bounds_max)) {
		return false;
	}

	const LightBasis basis = MakeLightBasis(sun_direction);

	if (basis.Forward.Y > -scMinSunHeight) {
		return false;
	}

	const double horizontal = std::sqrt(basis.Forward.X * basis.Forward.X + basis.Forward.Z * basis.Forward.Z);
	const double reach = std::min((bounds_max.Y - bounds_min.Y) * horizontal / std::abs(basis.Forward.Y),
								  options.MaxReach);

	double reach_x = 0.0;
	double reach_z = 0.0;

	if (horizontal > scMinHorizontal) {
		reach_x = basis.Forward.X / horizontal * reach;
		reach_z = basis.Forward.Z / horizontal * reach;
	}

	const double extent_min_x = std::min(bounds_min.X, bounds_min.X + reach_x);
	const double extent_max_x = std::max(bounds_max.X, bounds_max.X + reach_x);
	const double extent_min_z = std::min(bounds_min.Z, bounds_min.Z + reach_z);
	const double extent_max_z = std::max(bounds_max.Z, bounds_max.Z + reach_z);

	const int32 cell_min_x = static_cast<int32>(std::floor(extent_min_x / options.PageSize));
	const int32 cell_min_z = static_cast<int32>(std::floor(extent_min_z / options.PageSize));
	const int32 cell_max_x = static_cast<int32>(std::floor(extent_max_x / options.PageSize));
	const int32 cell_max_z = static_cast<int32>(std::floor(extent_max_z / options.PageSize));

	const uint32 cells_x = static_cast<uint32>(cell_max_x - cell_min_x + 1);
	const uint32 cells_z = static_cast<uint32>(cell_max_z - cell_min_z + 1);

	double start_depth = scNoHit;

	for (uint32 corner = 0; corner < 8; corner++) {
		const Vec3d point((corner & 1) ? bounds_max.X : bounds_min.X, (corner & 2) ? bounds_max.Y : bounds_min.Y,
						  (corner & 4) ? bounds_max.Z : bounds_min.Z);

		start_depth = std::min(start_depth, basis.Forward.Dot(point));
	}

	start_depth -= scDepthMargin;

	Array<PageFrame> frames;
	Array<RowJob> jobs;

	for (int32 cell_z = cell_min_z; cell_z <= cell_max_z; cell_z++) {
		for (int32 cell_x = cell_min_x; cell_x <= cell_max_x; cell_x++) {
			PageFrame frame = MakePageFrame(basis, cell_x, cell_z, options.PageSize, bounds_min.Y, bounds_max.Y,
											options);
			frame.Depths.InitSize(static_cast<size_t>(frame.Width) * frame.Height);

			for (uint32 row = 0; row < frame.Height; row++) {
				jobs.Emplace(RowJob { frames.Size, row });
			}

			frames.Emplace(std::move(frame));
		}
	}

	const uint32 samples = std::max<uint32>(options.Samples, 1);

	std::atomic<uint32> next_job { 0 };

	const auto worker = [&]()
	{
		while (true) {
			const uint32 first = next_job.fetch_add(scRowsPerClaim, std::memory_order_relaxed);

			if (first >= jobs.Size) {
				break;
			}

			const uint32 last = std::min<uint32>(first + scRowsPerClaim, jobs.Size);

			for (uint32 i = first; i < last; i++) {
				TraceRow(occluders, basis, start_depth, samples, frames[jobs[i].Page], jobs[i].Row);
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

	Array<CompiledMapFogShadowPage> pages;

	for (PageFrame& frame : frames) {
		out_stats.TexelCount += frame.Depths.Size;
		out_stats.RayCount += static_cast<uint64>(frame.Depths.Size) * samples * samples;

		bool has_shadow = false;

		for (const uint16 depth : frame.Depths) {
			if (depth != scUnshadowed) {
				has_shadow = true;
				break;
			}
		}

		if (!has_shadow) {
			continue;
		}

		CompiledMapFogShadowPage page;
		page.CellX = frame.CellX;
		page.CellZ = frame.CellZ;
		page.LightMin[0] = static_cast<float32>(frame.LightMin[0]);
		page.LightMin[1] = static_cast<float32>(frame.LightMin[1]);
		page.LightSize[0] = static_cast<float32>(frame.LightSize[0]);
		page.LightSize[1] = static_cast<float32>(frame.LightSize[1]);
		page.DepthMin = static_cast<float32>(frame.DepthMin);
		page.DepthMax = static_cast<float32>(frame.DepthMax);

		page.Levels.InitSize(CompiledMapFogShadow::scLevelCount);

		CompiledMapFogShadowLevel& detail = page.Levels[0];
		detail.Width = frame.Width;
		detail.Height = frame.Height;
		detail.Depths = std::move(frame.Depths);

		for (uint32 level = 1; level < CompiledMapFogShadow::scLevelCount; level++) {
			page.Levels[level] = ReduceLevel(page.Levels[level - 1]);
		}

		for (const CompiledMapFogShadowLevel& level : page.Levels) {
			out_stats.ByteCount += level.Depths.Size * sizeof(uint16);
		}

		pages.Emplace(std::move(page));
	}

	for (uint32 axis = 0; axis < 3; axis++) {
		out_shadow.Right[axis] = static_cast<float32>(basis.Right.mData[axis]);
		out_shadow.Up[axis] = static_cast<float32>(basis.Up.mData[axis]);
		out_shadow.Forward[axis] = static_cast<float32>(basis.Forward.mData[axis]);
	}

	out_shadow.PageSize = static_cast<float32>(options.PageSize);
	out_shadow.TexelSize = static_cast<float32>(options.TexelSize);
	out_shadow.CellMin[0] = cell_min_x;
	out_shadow.CellMin[1] = cell_min_z;
	out_shadow.CellCount[0] = cells_x;
	out_shadow.CellCount[1] = cells_z;

	out_shadow.Pages.InitSize(pages.Size);

	for (uint32 i = 0; i < pages.Size; i++) {
		out_shadow.Pages[i] = std::move(pages[i]);
	}

	out_stats.CellCount = cells_x * cells_z;
	out_stats.PageCount = out_shadow.Pages.Size;

	return true;
}

} // namespace fx::mapc
