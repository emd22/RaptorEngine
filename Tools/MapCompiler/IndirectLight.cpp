#include "IndirectLight.hpp"

#include <ktx.h>

#include <Color.hpp>
#include <Core/FilesystemIO.hpp>
#include <Material/MaterialDefinition.hpp>
#include <Math/Float16.hpp>
#include <Math/MathConsts.hpp>
#include <Renderer/ProbeEncoding.hpp>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>

namespace fx::mapc {

namespace {

constexpr float32 scMaxAlbedo = 0.9f;
constexpr float32 scNoHit = std::numeric_limits<float32>::infinity();

constexpr uint32 scBinCount = 16;

constexpr int32 scFilterRadius = 2;
constexpr int32 scFilterWidth = scFilterRadius * 2 + 1;
constexpr float32 scFilterKernel[scFilterWidth] = { 1.0f / 16.0f, 1.0f / 4.0f, 3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f };
constexpr float32 scVarianceKernel[3] = { 0.25f, 0.5f, 0.25f };
constexpr uint32 scNoShading = std::numeric_limits<uint32>::max();
constexpr uint32 scSkyHit = std::numeric_limits<uint32>::max();
constexpr uint32 scDarkHit = scSkyHit - 1;

float32 GetLuminance(const Vec3f& color) { return color.Dot(Vec3f(0.2126f, 0.7152f, 0.0722f)); }

uint8 EncodeDirection(float32 value)
{
	return static_cast<uint8>(std::lround(std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f));
}

struct AlbedoTexture
{
	uint32 Width = 1;
	uint32 Height = 1;
	SizedArray<Vec3f> Texels;

	void InitConstant(const Vec3f& color)
	{
		Width = 1;
		Height = 1;
		Texels.Free();
		Texels.InitSize(1);
		Texels[0] = color;
	}

	Vec3f Sample(float32 u, float32 v) const
	{
		const float32 x = (u - std::floor(u)) * static_cast<float32>(Width);
		const float32 y = (v - std::floor(v)) * static_cast<float32>(Height);

		const uint32 texel_x = std::min(static_cast<uint32>(x), Width - 1);
		const uint32 texel_y = std::min(static_cast<uint32>(y), Height - 1);

		return Texels[static_cast<size_t>(texel_y) * Width + texel_x];
	}
};

bool ReadAlbedoTexture(const String& path, AlbedoTexture& out_texture)
{
	constexpr uint32 scAlbedoMipSize = 128;
	constexpr uint32 scFormatRGBA8Unorm = 37;
	constexpr uint32 scFormatRGBA8Srgb = 43;

	ktxTexture* texture = nullptr;

	if (ktxTexture_CreateFromNamedFile(FilesystemIO::ResolvePath(path).CStr(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
									   &texture) != KTX_SUCCESS) {
		return false;
	}

	uint32 format = 0;

	if (texture->classId == ktxTexture2_c) {
		ktxTexture2* texture2 = reinterpret_cast<ktxTexture2*>(texture);

		if (ktxTexture2_NeedsTranscoding(texture2) &&
			ktxTexture2_TranscodeBasis(texture2, KTX_TTF_RGBA32, 0) != KTX_SUCCESS) {
			ktxTexture_Destroy(texture);
			return false;
		}

		format = texture2->vkFormat;
	}

	if (format != scFormatRGBA8Unorm && format != scFormatRGBA8Srgb) {
		ktxTexture_Destroy(texture);
		return false;
	}

	uint32 level = 0;

	while (level + 1 < texture->numLevels &&
		   std::max(texture->baseWidth >> level, texture->baseHeight >> level) > scAlbedoMipSize) {
		level++;
	}

	const uint32 width = std::max<uint32>(texture->baseWidth >> level, 1);
	const uint32 height = std::max<uint32>(texture->baseHeight >> level, 1);

	ktx_size_t offset = 0;

	if (ktxTexture_GetImageOffset(texture, level, 0, 0, &offset) != KTX_SUCCESS) {
		ktxTexture_Destroy(texture);
		return false;
	}

	const uint8* texels = ktxTexture_GetData(texture) + offset;
	const uint32 row_pitch = ktxTexture_GetRowPitch(texture, level);
	const bool is_srgb = (format == scFormatRGBA8Srgb);

	float32 decode[256];

	for (uint32 value = 0; value < 256; value++) {
		decode[value] = static_cast<float32>(value) / 255.0f;

		if (is_srgb) {
			decode[value] = Color::SrgbToLinear(decode[value]);
		}
	}

	uint32 factor = 1;

	while (std::max(width / factor, height / factor) > scAlbedoMipSize) {
		factor *= 2;
	}

	out_texture.Width = std::max<uint32>(width / factor, 1);
	out_texture.Height = std::max<uint32>(height / factor, 1);
	out_texture.Texels.Free();
	out_texture.Texels.InitSize(static_cast<size_t>(out_texture.Width) * out_texture.Height);

	for (uint32 out_y = 0; out_y < out_texture.Height; out_y++) {
		for (uint32 out_x = 0; out_x < out_texture.Width; out_x++) {
			const uint32 last_x = std::min((out_x + 1) * factor, width);
			const uint32 last_y = std::min((out_y + 1) * factor, height);

			Vec3f sum(0.0f);

			for (uint32 y = out_y * factor; y < last_y; y++) {
				for (uint32 x = out_x * factor; x < last_x; x++) {
					const uint8* texel = texels + static_cast<size_t>(y) * row_pitch + static_cast<size_t>(x) * 4;
					sum += Vec3f(decode[texel[0]], decode[texel[1]], decode[texel[2]]);
				}
			}

			const float32 count = static_cast<float32>((last_x - out_x * factor) * (last_y - out_y * factor));

			out_texture.Texels[static_cast<size_t>(out_y) * out_texture.Width + out_x] = sum * (1.0f / count);
		}
	}

	ktxTexture_Destroy(texture);

	return true;
}

SizedArray<AlbedoTexture> LoadAlbedos(const SizedArray<CompiledMapMaterial>& materials, uint32& out_missing)
{
	constexpr float32 scDefaultAlbedo = 0.5f;

	const MaterialDefinitionArray definitions = LoadMaterialDefinitions(scMaterialRoot);

	SizedArray<AlbedoTexture> albedos;
	albedos.InitSize(materials.Size);

	for (size_t i = 0; i < materials.Size; i++) {
		const CompiledMapMaterial& material = materials[i];
		AlbedoTexture& albedo = albedos[i];

		albedo.InitConstant(Vec3f(scDefaultAlbedo));

		const auto found = std::find_if(definitions.begin(), definitions.end(),
										[&material](const MaterialDefinition& definition)
										{ return definition.Name == material.Name; });

		if (found == definitions.end()) {
			if (!material.Name.IsEmpty()) {
				out_missing++;
			}

			continue;
		}

		if (found->DiffusePath.IsEmpty()) {
			albedo.InitConstant(Vec3f(1.0f));
		}
		else if (!ReadAlbedoTexture(found->DiffusePath, albedo)) {
			albedo.InitConstant(Vec3f(scDefaultAlbedo));
			out_missing++;
		}

		Vec3f tint(1.0f);

		if (found->BaseColor.has_value()) {
			tint = tint * *found->BaseColor;
		}

		if (found->Metallic.has_value() && found->OrmPath.IsEmpty()) {
			tint = tint * (1.0f - std::clamp(*found->Metallic, 0.0f, 1.0f));
		}

		for (Vec3f& texel : albedo.Texels) {
			texel = texel * tint;
		}
	}

	return albedos;
}

Vec3f UnpackColor(uint32 packed)
{
	return Vec3f(static_cast<float32>(packed & 0xFFU), static_cast<float32>((packed >> 8) & 0xFFU),
				 static_cast<float32>((packed >> 16) & 0xFFU)) *
		   (1.0f / 255.0f);
}

Vec3f CapAlbedo(const Vec3f& albedo) { return Vec3f::Min(albedo, Vec3f(scMaxAlbedo)); }

struct TriangleShading
{
	Vec3f U = Vec3f(0.0f);
	Vec3f V = Vec3f(0.0f);
	Vec2f Texcoord[3];
	Vec3f Tint[3];
	uint32 Material = 0;
};

struct SceneTriangle
{
	Vec3f A = Vec3f(0.0f);
	Vec3f EdgeAB = Vec3f(0.0f);
	Vec3f EdgeAC = Vec3f(0.0f);
	Vec3f Facing = Vec3f(0.0f);

	Vec3f Min = Vec3f(0.0f);
	Vec3f Max = Vec3f(0.0f);

	uint32 Shading = scNoShading;
};

struct alignas(16) SceneNode
{
	float32 Bounds[6][4];
	int32 Children[4];

	SceneNode()
	{
		constexpr int32 scEmptyChild = std::numeric_limits<int32>::min();

		for (uint32 lane = 0; lane < 4; lane++) {
			for (uint32 axis = 0; axis < 3; axis++) {
				Bounds[axis * 2][lane] = scNoHit;
				Bounds[axis * 2 + 1][lane] = -scNoHit;
			}

			Children[lane] = scEmptyChild;
		}
	}
};

struct alignas(16) ScenePacket
{
	float32 A[3][4] = {};
	float32 EdgeAB[3][4] = {};
	float32 EdgeAC[3][4] = {};

	uint32 Triangles[4] = {};
};

struct SceneRay
{
	FLOAT4 Origin[3];
	FLOAT4 Direction[3];
	FLOAT4 InvDirection[3];

	uint32 Near[3] = {};
	uint32 Far[3] = {};

	SceneRay(const Vec3f& origin, const Vec3f& direction)
	{
		constexpr float32 scMinDirection = 1e-9f;

		for (uint32 axis = 0; axis < 3; axis++) {
			float32 component = direction.mData[axis];

			if (std::abs(component) < scMinDirection) {
				component = std::copysign(scMinDirection, component);
			}

			Origin[axis] = simd::LoadFloat4(origin.mData[axis]);
			Direction[axis] = simd::LoadFloat4(direction.mData[axis]);
			InvDirection[axis] = simd::LoadFloat4(1.0f / component);

			Near[axis] = axis * 2;
			Far[axis] = axis * 2 + 1;

			if (component < 0.0f) {
				Near[axis] = axis * 2 + 1;
				Far[axis] = axis * 2;
			}
		}
	}
};

struct SceneHit
{
	float32 Distance = scNoHit;
	float32 U = 0.0f;
	float32 V = 0.0f;
	uint32 Triangle = 0;
};

FX_FORCE_INLINE FLOAT4 DotLanes(const FLOAT4* a, const FLOAT4* b)
{
	return simd::Add(simd::Add(simd::Mul(a[0], b[0]), simd::Mul(a[1], b[1])), simd::Mul(a[2], b[2]));
}

FX_FORCE_INLINE void CrossLanes(const FLOAT4* a, const FLOAT4* b, FLOAT4* out)
{
	out[0] = simd::Sub(simd::Mul(a[1], b[2]), simd::Mul(a[2], b[1]));
	out[1] = simd::Sub(simd::Mul(a[2], b[0]), simd::Mul(a[0], b[2]));
	out[2] = simd::Sub(simd::Mul(a[0], b[1]), simd::Mul(a[1], b[0]));
}

float32 GetSurfaceArea(const Vec3f& min, const Vec3f& max)
{
	const Vec3f extent = max - min;

	return 2.0f * (extent.X * extent.Y + extent.Y * extent.Z + extent.Z * extent.X);
}

class Scene
{
public:
	void AddTriangle(const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& facing, uint32 shading)
	{
		SceneTriangle& triangle = mTriangles.Emplace();
		triangle.A = a;
		triangle.EdgeAB = b - a;
		triangle.EdgeAC = c - a;
		triangle.Facing = facing;
		triangle.Min = Vec3f::Min(a, Vec3f::Min(b, c));
		triangle.Max = Vec3f::Max(a, Vec3f::Max(b, c));
		triangle.Shading = shading;
	}

	void Build()
	{
		mNodes.Clear();
		mPackets.Clear();

		if (mTriangles.Size == 0) {
			return;
		}

		Array<BuildReference> references;

		for (uint32 i = 0; i < mTriangles.Size; i++) {
			const SceneTriangle& triangle = mTriangles[i];

			BuildReference& reference = references.Emplace();
			reference.Min = triangle.Min;
			reference.Max = triangle.Max;
			reference.Center = (triangle.Min + triangle.Max) * 0.5f;
			reference.Triangle = i;
		}

		Array<BuildNode> build_nodes;
		BuildBinary(references, build_nodes, 0, references.Size);

		mRoot = Collapse(references, build_nodes, 0);
	}

	uint32 GetTriangleCount() const { return mTriangles.Size; }

	const SceneTriangle& GetTriangle(uint32 index) const { return mTriangles[index]; }

	bool Trace(const SceneRay& ray, float32 max_distance, SceneHit& out_hit) const
	{
		out_hit.Distance = max_distance;

		return Traverse<false>(ray, out_hit);
	}

	bool IsOccluded(const SceneRay& ray, float32 max_distance) const
	{
		SceneHit hit;
		hit.Distance = max_distance;

		return Traverse<true>(ray, hit);
	}

private:
	struct BuildReference
	{
		Vec3f Min = Vec3f(0.0f);
		Vec3f Max = Vec3f(0.0f);
		Vec3f Center = Vec3f(0.0f);

		uint32 Triangle = 0;
	};

	struct BuildNode
	{
		Vec3f Min = Vec3f(0.0f);
		Vec3f Max = Vec3f(0.0f);

		int32 Left = -1;
		int32 Right = -1;

		uint32 First = 0;
		uint32 Count = 0;
	};

	struct BuildBin
	{
		Vec3f Min = Vec3f(scNoHit);
		Vec3f Max = Vec3f(-scNoHit);

		uint32 Count = 0;
	};

	struct StackEntry
	{
		int32 Child;
		float32 Distance;
	};

	static uint32 GetBin(const BuildReference& reference, uint32 axis, float32 center_min, float32 scale)
	{
		const float32 offset = (reference.Center.mData[axis] - center_min) * scale;

		return std::min<uint32>(static_cast<uint32>(std::fmax(offset, 0.0f)), scBinCount - 1);
	}

	static uint32 HitsBoxes(const SceneNode& node, const SceneRay& ray, float32 max_distance, float32* out_enter)
	{
		FLOAT4 enter = simd::LoadFloat4(0.0f);
		FLOAT4 exit = simd::LoadFloat4(max_distance);

		for (uint32 axis = 0; axis < 3; axis++) {
			const FLOAT4 near_time = simd::Mul(
				simd::Sub(simd::LoadFloat4(node.Bounds[ray.Near[axis]]), ray.Origin[axis]), ray.InvDirection[axis]);
			const FLOAT4 far_time = simd::Mul(simd::Sub(simd::LoadFloat4(node.Bounds[ray.Far[axis]]), ray.Origin[axis]),
											  ray.InvDirection[axis]);

			enter = simd::Max(enter, near_time);
			exit = simd::Min(exit, far_time);
		}

		simd::StoreFloat4(out_enter, enter);

		return simd::GetMask(simd::CompareLessEqual(enter, exit));
	}

	static bool HitsPacket(const ScenePacket& packet, const SceneRay& ray, SceneHit& hit)
	{
		constexpr float32 scHitEpsilon = 1e-5f;
		constexpr float32 scParallelEpsilon = 1e-12f;

		FLOAT4 edge_ab[3];
		FLOAT4 edge_ac[3];
		FLOAT4 offset[3];

		for (uint32 axis = 0; axis < 3; axis++) {
			edge_ab[axis] = simd::LoadFloat4(packet.EdgeAB[axis]);
			edge_ac[axis] = simd::LoadFloat4(packet.EdgeAC[axis]);
			offset[axis] = simd::Sub(ray.Origin[axis], simd::LoadFloat4(packet.A[axis]));
		}

		FLOAT4 p[3];
		CrossLanes(ray.Direction, edge_ac, p);

		const FLOAT4 determinant = DotLanes(edge_ab, p);
		const FLOAT4 inverse = simd::Div(simd::LoadFloat4(1.0f), determinant);
		const FLOAT4 u = simd::Mul(DotLanes(offset, p), inverse);

		FLOAT4 q[3];
		CrossLanes(offset, edge_ab, q);

		const FLOAT4 v = simd::Mul(DotLanes(ray.Direction, q), inverse);
		const FLOAT4 distance = simd::Mul(DotLanes(edge_ac, q), inverse);

		const FLOAT4 zero = simd::LoadFloat4(0.0f);
		const FLOAT4 one = simd::LoadFloat4(1.0f);

		UINT4 mask = simd::CompareLessEqual(simd::LoadFloat4(scParallelEpsilon), simd::Abs(determinant));
		mask = simd::And(mask, simd::CompareLessEqual(zero, u));
		mask = simd::And(mask, simd::CompareLessEqual(u, one));
		mask = simd::And(mask, simd::CompareLessEqual(zero, v));
		mask = simd::And(mask, simd::CompareLessEqual(simd::Add(u, v), one));
		mask = simd::And(mask, simd::CompareLess(simd::LoadFloat4(scHitEpsilon), distance));
		mask = simd::And(mask, simd::CompareLess(distance, simd::LoadFloat4(hit.Distance)));

		const uint32 bits = simd::GetMask(mask);

		if (bits == 0) {
			return false;
		}

		float32 distances alignas(16)[4];
		float32 us alignas(16)[4];
		float32 vs alignas(16)[4];

		simd::StoreFloat4(distances, distance);
		simd::StoreFloat4(us, u);
		simd::StoreFloat4(vs, v);

		uint32 best = static_cast<uint32>(std::countr_zero(bits));

		for (uint32 lane = best + 1; lane < 4; lane++) {
			if ((bits & (1U << lane)) != 0 && distances[lane] < distances[best]) {
				best = lane;
			}
		}

		hit.Distance = distances[best];
		hit.U = us[best];
		hit.V = vs[best];
		hit.Triangle = packet.Triangles[best];

		return true;
	}

	template <bool TAnyHit>
	bool Traverse(const SceneRay& ray, SceneHit& hit) const
	{
		constexpr uint32 scMaxStackDepth = 128;

		if (mTriangles.Size == 0) {
			return false;
		}

		StackEntry stack[scMaxStackDepth];
		uint32 stack_size = 0;

		stack[stack_size++] = StackEntry { mRoot, 0.0f };

		bool found = false;

		while (stack_size > 0) {
			const StackEntry entry = stack[--stack_size];

			if (entry.Distance > hit.Distance) {
				continue;
			}

			if (entry.Child < 0) {
				if (HitsPacket(mPackets[static_cast<uint32>(~entry.Child)], ray, hit)) {
					found = true;

					if constexpr (TAnyHit) {
						return true;
					}
				}

				continue;
			}

			const SceneNode& node = mNodes[static_cast<uint32>(entry.Child)];

			float32 enter alignas(16)[4];
			const uint32 bits = HitsBoxes(node, ray, hit.Distance, enter);

			if (bits == 0) {
				continue;
			}

			StackEntry hits[4];
			uint32 hit_count = 0;

			for (uint32 lane = 0; lane < 4; lane++) {
				if ((bits & (1U << lane)) != 0) {
					hits[hit_count++] = StackEntry { node.Children[lane], enter[lane] };
				}
			}

			if constexpr (!TAnyHit) {
				for (uint32 i = 1; i < hit_count; i++) {
					const StackEntry current = hits[i];

					uint32 j = i;

					while (j > 0 && hits[j - 1].Distance < current.Distance) {
						hits[j] = hits[j - 1];
						j--;
					}

					hits[j] = current;
				}
			}

			if (stack_size + hit_count > scMaxStackDepth) {
				continue;
			}

			for (uint32 i = 0; i < hit_count; i++) {
				stack[stack_size++] = hits[i];
			}
		}

		return found;
	}

	static bool FindSplit(const Array<BuildReference>& references, uint32 first, uint32 count, const Vec3f& center_min,
						  const Vec3f& center_max, uint32& out_axis, uint32& out_bin)
	{
		float32 best_cost = scNoHit;
		bool found = false;

		for (uint32 axis = 0; axis < 3; axis++) {
			const float32 extent = center_max.mData[axis] - center_min.mData[axis];

			if (extent <= 0.0f) {
				continue;
			}

			const float32 scale = static_cast<float32>(scBinCount) / extent;

			BuildBin bins[scBinCount];

			for (uint32 i = first; i < first + count; i++) {
				const BuildReference& reference = references[i];
				BuildBin& bin = bins[GetBin(reference, axis, center_min.mData[axis], scale)];

				bin.Min = Vec3f::Min(bin.Min, reference.Min);
				bin.Max = Vec3f::Max(bin.Max, reference.Max);
				bin.Count++;
			}

			float32 right_areas[scBinCount];
			uint32 right_counts[scBinCount];

			Vec3f right_min(scNoHit);
			Vec3f right_max(-scNoHit);
			uint32 right_count = 0;

			for (uint32 bin = scBinCount - 1; bin > 0; bin--) {
				right_min = Vec3f::Min(right_min, bins[bin].Min);
				right_max = Vec3f::Max(right_max, bins[bin].Max);
				right_count += bins[bin].Count;

				right_areas[bin] = 0.0f;

				if (right_count > 0) {
					right_areas[bin] = GetSurfaceArea(right_min, right_max);
				}

				right_counts[bin] = right_count;
			}

			Vec3f left_min(scNoHit);
			Vec3f left_max(-scNoHit);
			uint32 left_count = 0;

			for (uint32 bin = 1; bin < scBinCount; bin++) {
				left_min = Vec3f::Min(left_min, bins[bin - 1].Min);
				left_max = Vec3f::Max(left_max, bins[bin - 1].Max);
				left_count += bins[bin - 1].Count;

				if (left_count == 0 || right_counts[bin] == 0) {
					continue;
				}

				const float32 cost = GetSurfaceArea(left_min, left_max) * static_cast<float32>(left_count) +
									 right_areas[bin] * static_cast<float32>(right_counts[bin]);

				if (cost < best_cost) {
					best_cost = cost;
					out_axis = axis;
					out_bin = bin;
					found = true;
				}
			}
		}

		return found;
	}

	static int32 BuildBinary(Array<BuildReference>& references, Array<BuildNode>& nodes, uint32 first, uint32 count)
	{
		constexpr uint32 scLeafSize = 4;

		const int32 node_index = static_cast<int32>(nodes.Size);
		nodes.Emplace();

		Vec3f min = references[first].Min;
		Vec3f max = references[first].Max;
		Vec3f center_min = references[first].Center;
		Vec3f center_max = center_min;

		for (uint32 i = first + 1; i < first + count; i++) {
			const BuildReference& reference = references[i];

			min = Vec3f::Min(min, reference.Min);
			max = Vec3f::Max(max, reference.Max);
			center_min = Vec3f::Min(center_min, reference.Center);
			center_max = Vec3f::Max(center_max, reference.Center);
		}

		int32 left = -1;
		int32 right = -1;

		if (count > scLeafSize) {
			uint32 left_count = count / 2;

			uint32 axis = 0;
			uint32 split_bin = 0;

			if (FindSplit(references, first, count, center_min, center_max, axis, split_bin)) {
				const float32 axis_min = center_min.mData[axis];
				const float32 scale = static_cast<float32>(scBinCount) / (center_max.mData[axis] - axis_min);

				BuildReference* begin = references.pData + first;
				BuildReference* middle = std::partition(
					begin, begin + count, [&](const BuildReference& reference)
					{ return GetBin(reference, axis, axis_min, scale) < split_bin; });

				const uint32 partitioned = static_cast<uint32>(middle - begin);

				if (partitioned > 0 && partitioned < count) {
					left_count = partitioned;
				}
			}

			left = BuildBinary(references, nodes, first, left_count);
			right = BuildBinary(references, nodes, first + left_count, count - left_count);
		}

		BuildNode& node = nodes[static_cast<uint32>(node_index)];
		node.Min = min;
		node.Max = max;
		node.Left = left;
		node.Right = right;
		node.First = first;
		node.Count = count;

		return node_index;
	}

	int32 Collapse(const Array<BuildReference>& references, const Array<BuildNode>& build_nodes, int32 build_index)
	{
		const BuildNode& source = build_nodes[static_cast<uint32>(build_index)];

		if (source.Left < 0) {
			return ~AddPacket(references, source.First, source.Count);
		}

		int32 children[4] = { source.Left, source.Right, -1, -1 };
		uint32 child_count = 2;

		while (child_count < 4) {
			int32 widest = -1;
			float32 widest_area = -1.0f;

			for (uint32 i = 0; i < child_count; i++) {
				const BuildNode& child = build_nodes[static_cast<uint32>(children[i])];
				const float32 area = GetSurfaceArea(child.Min, child.Max);

				if (child.Left >= 0 && area > widest_area) {
					widest = static_cast<int32>(i);
					widest_area = area;
				}
			}

			if (widest < 0) {
				break;
			}

			const BuildNode& child = build_nodes[static_cast<uint32>(children[widest])];

			children[widest] = child.Left;
			children[child_count++] = child.Right;
		}

		const int32 node_index = static_cast<int32>(mNodes.Size);
		mNodes.Emplace();

		for (uint32 i = 0; i < child_count; i++) {
			const int32 child_id = Collapse(references, build_nodes, children[i]);
			const BuildNode& child = build_nodes[static_cast<uint32>(children[i])];

			SceneNode& node = mNodes[static_cast<uint32>(node_index)];

			for (uint32 axis = 0; axis < 3; axis++) {
				node.Bounds[axis * 2][i] = child.Min.mData[axis];
				node.Bounds[axis * 2 + 1][i] = child.Max.mData[axis];
			}

			node.Children[i] = child_id;
		}

		return node_index;
	}

	int32 AddPacket(const Array<BuildReference>& references, uint32 first, uint32 count)
	{
		ScenePacket& packet = mPackets.Emplace();

		for (uint32 lane = 0; lane < count; lane++) {
			const uint32 triangle_index = references[first + lane].Triangle;
			const SceneTriangle& triangle = mTriangles[triangle_index];

			for (uint32 axis = 0; axis < 3; axis++) {
				packet.A[axis][lane] = triangle.A.mData[axis];
				packet.EdgeAB[axis][lane] = triangle.EdgeAB.mData[axis];
				packet.EdgeAC[axis][lane] = triangle.EdgeAC.mData[axis];
			}

			packet.Triangles[lane] = triangle_index;
		}

		return static_cast<int32>(mPackets.Size - 1);
	}

private:
	Array<SceneTriangle> mTriangles;
	Array<SceneNode> mNodes;
	Array<ScenePacket> mPackets;

	int32 mRoot = 0;
};

struct GatherHit
{
	Vec3f Albedo = Vec3f(0.0f);
	uint32 Texel = scSkyHit;
	float32 Distance = scNoHit;
};

struct CachedHit
{
	uint32 Texel;
	uint32 Albedo;
};

uint32 EncodeCachedAlbedo(const Vec3f& albedo)
{
	uint32 packed = 0;

	for (uint32 channel = 0; channel < 3; channel++) {
		const float32 value = std::sqrt(std::clamp(albedo.mData[channel] / scMaxAlbedo, 0.0f, 1.0f));
		packed |= static_cast<uint32>(std::lround(value * 255.0f)) << (channel * 8);
	}

	return packed;
}

Vec3f DecodeCachedAlbedo(uint32 packed)
{
	Vec3f albedo(0.0f);

	for (uint32 channel = 0; channel < 3; channel++) {
		const float32 value = static_cast<float32>((packed >> (channel * 8)) & 0xFFU) * (1.0f / 255.0f);
		albedo.mData[channel] = value * value * scMaxAlbedo;
	}

	return albedo;
}

float32 HashToUnit(uint32 value)
{
	value ^= value >> 16;
	value *= 0x7FEB352DU;
	value ^= value >> 15;
	value *= 0x846CA68BU;
	value ^= value >> 16;

	return static_cast<float32>(value >> 8) * (1.0f / 16777216.0f);
}

struct DirectionSet
{
	SizedArray<Vec2f> Points;

	explicit DirectionSet(uint32 count)
	{
		Points.InitSize(count);

		for (uint32 index = 0; index < count; index++) {
			uint32 bits = index;
			bits = (bits << 16) | (bits >> 16);
			bits = ((bits & 0x55555555U) << 1) | ((bits & 0xAAAAAAAAU) >> 1);
			bits = ((bits & 0x33333333U) << 2) | ((bits & 0xCCCCCCCCU) >> 2);
			bits = ((bits & 0x0F0F0F0FU) << 4) | ((bits & 0xF0F0F0F0U) >> 4);
			bits = ((bits & 0x00FF00FFU) << 8) | ((bits & 0xFF00FF00U) >> 8);

			const float32 u = (static_cast<float32>(index) + 0.5f) / static_cast<float32>(count);
			const float32 v = static_cast<float32>(static_cast<double>(bits) * (1.0 / 4294967296.0));

			Points[index] = Vec2f(u, v);
		}
	}

	static Vec3f GetLocal(const Vec2f& point, float32 offset_u, float32 offset_v)
	{
		const float32 u = point.X + offset_u - std::floor(point.X + offset_u);
		const float32 v = point.Y + offset_v - std::floor(point.Y + offset_v);

		const float32 radius = std::sqrt(u);
		const float32 angle = v * FX_2PI;

		return Vec3f(std::cos(angle) * radius, std::sin(angle) * radius, std::sqrt(std::fmax(1.0f - u, 0.0f)));
	}
};

void GetBasis(const Vec3f& normal, Vec3f& out_tangent, Vec3f& out_bitangent)
{
	Vec3f reference(0.0f, 1.0f, 0.0f);

	if (std::abs(normal.Y) >= 0.9f) {
		reference = Vec3f(1.0f, 0.0f, 0.0f);
	}

	out_tangent = reference.Cross(normal).Normalize();
	out_bitangent = normal.Cross(out_tangent);
}

template <typename TFunction>
void RunParallel(uint32 count, uint32 claim_size, const TFunction& function)
{
	std::atomic<uint32> next { 0 };

	const auto worker = [&]()
	{
		while (true) {
			const uint32 first = next.fetch_add(claim_size, std::memory_order_relaxed);

			if (first >= count) {
				break;
			}

			const uint32 last = std::min<uint32>(first + claim_size, count);

			for (uint32 i = first; i < last; i++) {
				function(i);
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
}

template <typename TFunction>
void RunParallel(uint32 count, const TFunction& function)
{
	constexpr uint32 scTexelsPerClaim = 64;

	RunParallel(count, scTexelsPerClaim, function);
}

struct ProbeRay
{
	Vec3f Direction = Vec3f(0.0f);
	float32 WeightedBasis[Limits::ProbeSHCoeffCount] = {};
	float32 SolidAngle = 0.0f;
	uint32 MomentTexel = 0;
};

float32 GetCubeCornerArea(float32 x, float32 y) { return std::atan2(x * y, std::sqrt(x * x + y * y + 1.0f)); }

Vec3f GetCubeDirection(uint32 face, float32 u, float32 v)
{
	switch (face) {
	case 0:
		return Vec3f(1.0f, -v, -u);
	case 1:
		return Vec3f(-1.0f, -v, u);
	case 2:
		return Vec3f(u, 1.0f, v);
	case 3:
		return Vec3f(u, -1.0f, -v);
	case 4:
		return Vec3f(u, -v, 1.0f);
	default:
		return Vec3f(-u, -v, -1.0f);
	}
}

SizedArray<ProbeRay> BuildProbeRays(uint32 rays_per_texel)
{
	const uint32 size = Limits::ProbeDepthSize * rays_per_texel;
	const float32 step = 2.0f / static_cast<float32>(size);

	SizedArray<ProbeRay> rays;
	rays.InitSize(static_cast<uint64>(Limits::ProbeDepthFaces) * size * size);

	ProbeRay* ray = rays.pData;

	for (uint32 face = 0; face < Limits::ProbeDepthFaces; face++) {
		for (uint32 y = 0; y < size; y++) {
			for (uint32 x = 0; x < size; x++, ray++) {
				const float32 x0 = -1.0f + static_cast<float32>(x) * step;
				const float32 y0 = -1.0f + static_cast<float32>(y) * step;
				const float32 x1 = x0 + step;
				const float32 y1 = y0 + step;

				ray->Direction = GetCubeDirection(face, x0 + step * 0.5f, y0 + step * 0.5f).Normalize();
				ray->SolidAngle = GetCubeCornerArea(x0, y0) - GetCubeCornerArea(x0, y1) - GetCubeCornerArea(x1, y0) +
								  GetCubeCornerArea(x1, y1);
				ray->MomentTexel = ProbeEncoding::DirectionToMomentTexel(ray->Direction);

				ProbeEncoding::EvalSHBasis(ray->Direction, ray->WeightedBasis);

				for (float32& basis : ray->WeightedBasis) {
					basis *= ray->SolidAngle;
				}
			}
		}
	}

	return rays;
}


void BleedEdges(const IndirectInput& input, Vec3f* values, Array<uint8>& filled, Array<Vec3f>& scratch_values,
				Array<uint8>& scratch_filled)
{
	for (const URect& rect : input.LightmapRects) {
		// for (uint32 rect_index = 0; rect_index < input.RectCount; rect_index++) {
		// const LightmapRect& rect = input.pRects[rect_index];

		const auto at = [&](uint32 x, uint32 y)
		{ return static_cast<uint64>(rect.Min.Y + y) * input.Width + rect.Min.X + x; };


		// Get the total size of the chart rect
		const Vec2u rect_size = rect.GetExtent();

		Fill(filled, rect_size.X * rect_size.Y, static_cast<uint8>(0));

		for (uint32 y = 0; y < rect_size.Y; y++) {
			for (uint32 x = 0; x < rect_size.X; x++) {
				filled[y * rect_size.X + x] = input.pCovered[at(x, y)];
			}
		}

		for (uint32 pass = 0; pass < rect_size.X + rect_size.Y; pass++) {
			Fill(scratch_values, rect_size.X * rect_size.Y, Vec3f(0.0f));
			Fill(scratch_filled, rect_size.X * rect_size.Y, static_cast<uint8>(0));

			bool changed = false;

			for (uint32 y = 0; y < rect_size.Y; y++) {
				for (uint32 x = 0; x < rect_size.X; x++) {
					if (filled[y * rect_size.X + x] != 0) {
						continue;
					}

					Vec3f sum(0.0f);
					uint32 count = 0;

					for (int32 offset_y = -1; offset_y <= 1; offset_y++) {
						for (int32 offset_x = -1; offset_x <= 1; offset_x++) {
							const int64 near_x = static_cast<int64>(x) + offset_x;
							const int64 near_y = static_cast<int64>(y) + offset_y;

							if (near_x < 0 || near_y < 0 || near_x >= rect_size.X || near_y >= rect_size.Y) {
								continue;
							}

							const uint32 local = static_cast<uint32>(near_y) * rect_size.X +
												 static_cast<uint32>(near_x);

							if (filled[local] != 0) {
								sum += values[at(static_cast<uint32>(near_x), static_cast<uint32>(near_y))];
								count++;
							}
						}
					}

					if (count > 0) {
						scratch_values[y * rect_size.X + x] = sum * (1.0f / static_cast<float32>(count));
						scratch_filled[y * rect_size.X + x] = 1;
						changed = true;
					}
				}
			}

			if (!changed) {
				break;
			}

			for (uint32 y = 0; y < rect_size.Y; y++) {
				for (uint32 x = 0; x < rect_size.X; x++) {
					if (scratch_filled[y * rect_size.X + x] != 0) {
						values[at(x, y)] = scratch_values[y * rect_size.X + x];
						filled[y * rect_size.X + x] = 1;
					}
				}
			}
		}
	}
}

uint32 GetDenoiseIterations(eDenoiseQuality quality)
{
	switch (quality) {
	case eDenoiseQuality::Low:
		return 3;
	case eDenoiseQuality::Medium:
		return 4;
	case eDenoiseQuality::High:
		return 5;
	default:
		return 0;
	}
}

class Denoiser
{
public:
	Denoiser(const IndirectInput& input, const Array<uint32>& texels) : mInput(input)
	{
		constexpr uint32 scNoRect = std::numeric_limits<uint32>::max();

		const uint64 texel_count = static_cast<uint64>(input.Width) * input.Height;

		mTexelRects.InitSize(texel_count, scNoRect);

		for (uint32 rect_index = 0; rect_index < input.LightmapRects.Size; rect_index++) {
			const URect& rect = input.LightmapRects[rect_index];

			for (uint32 y = rect.Min.Y; y < rect.Max.Y; y++) {
				for (uint32 x = rect.Min.X; x < rect.Max.X; x++) {
					mTexelRects[static_cast<uint64>(y) * input.Width + x] = rect_index;
				}
			}
		}

		for (const uint32 texel : texels) {
			if (mTexelRects[texel] != scNoRect) {
				mTexels.Emplace(texel);
			}
		}

		mValues.InitSize(mTexels.Size);
		mDirections.InitSize(mTexels.Size);
		mVariance.InitSize(mTexels.Size);
		mBlurredVariance.InitSize(texel_count);
	}

	void Filter(uint32 iterations, Vec3f* values, Vec3f* directions, float32* variance)
	{
		for (uint32 iteration = 0; iteration < iterations; iteration++) {
			BlurVariance(variance);

			const int32 step = 1 << iteration;

			RunParallel(mTexels.Size, [&](uint32 i) { FilterTexel(i, step, values, directions, variance); });

			RunParallel(mTexels.Size,
						[&](uint32 i)
						{
							const uint32 texel = mTexels[i];

							values[texel] = mValues[i];
							variance[texel] = mVariance[i];

							if (directions != nullptr) {
								directions[texel] = mDirections[i];
							}
						});
		}
	}

private:
	bool GetNeighbour(uint32 texel, int32 offset_x, int32 offset_y, uint64& out_near) const
	{
		const URect& rect = mInput.LightmapRects[mTexelRects[texel]];

		const int64 x = static_cast<int64>(texel % mInput.Width) + offset_x;
		const int64 y = static_cast<int64>(texel / mInput.Width) + offset_y;

		if (x < static_cast<int64>(rect.Min.X) || y < static_cast<int64>(rect.Min.Y) ||
			x >= static_cast<int64>(rect.Max.X) || y >= static_cast<int64>(rect.Max.Y)) {
			return false;
		}

		out_near = static_cast<uint64>(y) * mInput.Width + static_cast<uint64>(x);

		return mInput.pCovered[out_near] != 0;
	}

	void BlurVariance(const float32* variance)
	{
		RunParallel(mTexels.Size,
					[&](uint32 i)
					{
						const uint32 texel = mTexels[i];

						float32 sum = 0.0f;
						float32 weight_sum = 0.0f;

						for (int32 offset_y = -1; offset_y <= 1; offset_y++) {
							for (int32 offset_x = -1; offset_x <= 1; offset_x++) {
								uint64 near = 0;

								if (!GetNeighbour(texel, offset_x, offset_y, near)) {
									continue;
								}

								const float32 weight = scVarianceKernel[offset_x + 1] * scVarianceKernel[offset_y + 1];

								sum += variance[near] * weight;
								weight_sum += weight;
							}
						}

						mBlurredVariance[texel] = sum / weight_sum;
					});
	}

	void FilterTexel(uint32 index, int32 step, const Vec3f* values, const Vec3f* directions, const float32* variance)
	{
		constexpr uint32 scFilterTapCount = scFilterWidth * scFilterWidth;
		constexpr float32 scFilterNormalPower = 8.0f;
		constexpr float32 scFilterPlaneTolerance = 0.25f;
		constexpr float32 scFilterLuminanceSigma = 2.0f;
		constexpr float32 scFilterLuminanceEpsilon = 1e-3f;
		constexpr float32 scFilterRidge = 0.02f;
		constexpr float32 scFilterMinDeterminant = 1e-4f;
		constexpr float32 scFilterMinSigma = 1e-6f;

		const uint32 texel = mTexels[index];

		const Vec3f position = mInput.pPositions[texel];
		const Vec3f normal = mInput.pNormals[texel];
		const float32 luminance = GetLuminance(values[texel]);
		const float32 blurred_variance = mBlurredVariance[texel];
		const float32 plane_tolerance = mInput.TexelSize * scFilterPlaneTolerance;

		uint64 taps[scFilterTapCount];
		float32 weights[scFilterTapCount];
		float32 moments[6] = {};

		for (int32 offset_y = -scFilterRadius; offset_y <= scFilterRadius; offset_y++) {
			for (int32 offset_x = -scFilterRadius; offset_x <= scFilterRadius; offset_x++) {
				const uint32 tap = static_cast<uint32>((offset_y + scFilterRadius) * scFilterWidth + offset_x +
													   scFilterRadius);

				uint64 between = 0;

				weights[tap] = 0.0f;

				if (!GetNeighbour(texel, offset_x * step, offset_y * step, taps[tap]) ||
					!GetNeighbour(texel, offset_x * step / 2, offset_y * step / 2, between)) {
					continue;
				}

				const uint64 near = taps[tap];

				const float32 near_luminance = GetLuminance(values[near]);
				const float32 sigma = std::fmax(scFilterLuminanceSigma *
														std::sqrt(blurred_variance + mBlurredVariance[near]) +
													scFilterLuminanceEpsilon * (luminance + near_luminance),
												scFilterMinSigma);

				const float32 alignment = std::fmax(normal.Dot(mInput.pNormals[near]), 0.0f);
				const float32 plane_distance = std::abs(normal.Dot(mInput.pPositions[near] - position)) /
											   plane_tolerance;

				float32 weight = scFilterKernel[offset_x + scFilterRadius] * scFilterKernel[offset_y + scFilterRadius];
				weight *= std::pow(alignment, scFilterNormalPower);
				weight *= std::exp(-plane_distance * plane_distance);
				weight *= std::exp(-std::abs(luminance - near_luminance) / sigma);

				const float32 x = static_cast<float32>(offset_x);
				const float32 y = static_cast<float32>(offset_y);

				weights[tap] = weight;

				moments[0] += weight;
				moments[1] += weight * x;
				moments[2] += weight * y;
				moments[3] += weight * x * x;
				moments[4] += weight * x * y;
				moments[5] += weight * y * y;
			}
		}

		const float32 ridge = moments[0] * scFilterRidge;

		const float32 a = moments[0];
		const float32 b = moments[1];
		const float32 c = moments[2];
		const float32 d = moments[3] + ridge;
		const float32 e = moments[4];
		const float32 f = moments[5] + ridge;

		const float32 cofactor_0 = d * f - e * e;
		const float32 cofactor_1 = c * e - b * f;
		const float32 cofactor_2 = b * e - c * d;
		const float32 determinant = a * cofactor_0 + b * cofactor_1 + c * cofactor_2;

		Vec3f fit(1.0f / std::fmax(a, 1e-6f), 0.0f, 0.0f);

		if (determinant > scFilterMinDeterminant * a * d * f) {
			fit = Vec3f(cofactor_0, cofactor_1, cofactor_2) / determinant;
		}

		Vec3f value_sum(0.0f);
		Vec3f direction_sum(0.0f);
		float32 variance_sum = 0.0f;

		for (uint32 tap = 0; tap < scFilterTapCount; tap++) {
			if (weights[tap] == 0.0f) {
				continue;
			}

			const float32 x = static_cast<float32>(static_cast<int32>(tap % scFilterWidth) - scFilterRadius);
			const float32 y = static_cast<float32>(static_cast<int32>(tap / scFilterWidth) - scFilterRadius);
			const float32 weight = weights[tap] * fit.Dot(Vec3f(1.0f, x, y));
			const uint64 near = taps[tap];

			value_sum += values[near] * weight;
			variance_sum += variance[near] * weight * weight;

			if (directions != nullptr) {
				direction_sum += directions[near] * weight;
			}
		}

		mValues[index] = Vec3f(std::fmax(value_sum.X, 0.0f), std::fmax(value_sum.Y, 0.0f),
							   std::fmax(value_sum.Z, 0.0f));
		mVariance[index] = variance_sum;
		mDirections[index] = direction_sum;
	}

private:
	const IndirectInput& mInput;

	Array<uint32> mTexels;
	SizedArray<uint32> mTexelRects;

	SizedArray<Vec3f> mValues;
	SizedArray<Vec3f> mDirections;
	SizedArray<float32> mVariance;
	SizedArray<float32> mBlurredVariance;
};

bool WriteImage(const String& path, uint32 width, uint32 height, const Vec3f* irradiance, double mean_luminance)
{
	std::FILE* file = std::fopen(path.CStr(), "wb");

	if (file == nullptr) {
		return false;
	}

	std::fprintf(file, "P6\n%u %u\n255\n", width, height);

	const float32 scale = static_cast<float32>(std::log(2.0) / std::fmax(mean_luminance, 1e-6));
	const uint64 texel_count = static_cast<uint64>(width) * height;

	SizedArray<uint8> pixels;
	pixels.InitSize(texel_count * 3);

	for (uint64 texel = 0; texel < texel_count; texel++) {
		for (uint32 channel = 0; channel < 3; channel++) {
			const float32 value = 1.0f - std::exp(-irradiance[texel].mData[channel] * scale);
			float32 encoded = 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;

			if (value <= 0.0031308f) {
				encoded = value * 12.92f;
			}

			pixels[texel * 3 + channel] = static_cast<uint8>(std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
		}
	}

	const size_t written = std::fwrite(pixels.pData, 1, pixels.Size, file);
	const bool closed = (std::fclose(file) == 0);

	return written == pixels.Size && closed;
}

class IndirectBake
{
public:
	IndirectBake(const MapSource& source, const IndirectInput& input, const Scene& scene,
				 const Array<TriangleShading>& shadings, const SizedArray<AlbedoTexture>& albedos)
		: mSource(source), mInput(input), mScene(scene), mShadings(shadings), mAlbedos(albedos)
	{
	}

	Vec3f GetDirect(uint64 texel) const
	{
		constexpr float32 scShadowEpsilon = 1e-3f;

		const Vec3f position = mInput.pPositions[texel];
		const Vec3f normal = mInput.pNormals[texel];

		Vec3f irradiance(0.0f);

		if (mSource.bHasSun) {
			const Vec3f sun_direction = mSource.SunDirection.ToVec3f();
			const float32 sun_cosine = std::fmax(normal.Dot(sun_direction), 0.0f);
			const float32 visibility = static_cast<float32>(mInput.pSunVisibility[texel]) / 255.0f;

			irradiance += mSource.SunColor.ToVec3f() * (sun_cosine * visibility);
		}

		for (const SourceLight& light : mSource.Lights) {
			const Vec3f to_light = light.Position.ToVec3f() - position;
			const float32 distance_sq = to_light.Dot(to_light);

			const float32 factor = distance_sq * static_cast<float32>(light.InvRadiusSq);
			const float32 smooth = std::clamp(1.0f - factor * factor, 0.0f, 1.0f);

			float32 attenuation = (smooth * smooth) / std::fmax(distance_sq, 1e-4f);

			if (attenuation <= 0.0f) {
				continue;
			}

			const float32 distance = std::sqrt(distance_sq);
			const Vec3f direction = to_light * (1.0f / distance);

			if (light.bSpot) {
				const float32 cone_cosine = -direction.Dot(light.Direction.ToVec3f());
				const float32 cone = std::clamp((cone_cosine - static_cast<float32>(light.SpotCosOuter)) *
													static_cast<float32>(light.SpotAngleScale),
												0.0f, 1.0f);

				attenuation *= cone * cone;
			}

			const float32 cosine = normal.Dot(direction);

			if (attenuation <= 0.0f || cosine <= 0.0f) {
				continue;
			}

			if (mScene.IsOccluded(SceneRay(position, direction), distance - scShadowEpsilon)) {
				continue;
			}

			irradiance += light.Color.ToVec3f() * (attenuation * cosine);
		}

		return irradiance * FX_1_OVER_PI;
	}

	GatherHit TraceHit(const Vec3f& origin, const Vec3f& direction) const
	{
		GatherHit cached;

		SceneHit hit;

		if (!mScene.Trace(SceneRay(origin, direction), scNoHit, hit)) {
			return cached;
		}

		cached.Texel = scDarkHit;
		cached.Distance = hit.Distance;

		const SceneTriangle& triangle = mScene.GetTriangle(hit.Triangle);

		if (triangle.Shading == scNoShading || direction.Dot(triangle.Facing) >= 0.0f) {
			return cached;
		}

		const TriangleShading& shading = mShadings[triangle.Shading];
		const float32 w = 1.0f - hit.U - hit.V;

		const Vec3f barycentric(w, hit.U, hit.V);
		const float32 x = shading.U.Dot(barycentric);
		const float32 y = shading.V.Dot(barycentric);

		const uint32 texel_x = static_cast<uint32>(
			std::clamp(static_cast<int64>(std::floor(x)), static_cast<int64>(0), static_cast<int64>(mInput.Width) - 1));
		const uint32 texel_y = static_cast<uint32>(std::clamp(static_cast<int64>(std::floor(y)), static_cast<int64>(0),
															  static_cast<int64>(mInput.Height) - 1));
		const Vec2f texcoord = shading.Texcoord[0] * w + shading.Texcoord[1] * hit.U + shading.Texcoord[2] * hit.V;
		const Vec3f tint = shading.Tint[0] * w + shading.Tint[1] * hit.U + shading.Tint[2] * hit.V;
		const Vec3f albedo = CapAlbedo(mAlbedos[shading.Material].Sample(texcoord.X, texcoord.Y) * tint);

		cached.Texel = texel_y * mInput.Width + texel_x;
		cached.Albedo = albedo;

		return cached;
	}

	static Vec3f GetRadiance(const GatherHit& hit, const Vec3f* direct, const Vec3f* indirect)
	{
		constexpr float32 scSkyRadiance = 1500.0f / FX_PI;

		if (hit.Texel == scSkyHit) {
			return Vec3f(scSkyRadiance);
		}

		if (hit.Texel == scDarkHit) {
			return Vec3f(0.0f);
		}

		return hit.Albedo * (direct[hit.Texel] + indirect[hit.Texel]);
	}

	void Gather(uint64 texel, const DirectionSet& directions, const Vec3f* direct, const Vec3f* indirect,
				CachedHit* cache, bool replay, Vec3f& out_irradiance, Vec3f& out_direction, float32& out_variance) const
	{
		constexpr float32 scUniformDirectionality = 2.0f / 3.0f;

		const Vec3f origin = mInput.pPositions[texel];
		const Vec3f normal = mInput.pNormals[texel];

		Vec3f tangent;
		Vec3f bitangent;
		GetBasis(normal, tangent, bitangent);

		const float32 offset_u = HashToUnit(static_cast<uint32>(texel) * 2);
		const float32 offset_v = HashToUnit(static_cast<uint32>(texel) * 2 + 1);

		Vec3f sum(0.0f);
		Vec3f direction_sum(0.0f);
		float32 luminance_sum = 0.0f;
		float32 luminance_sq_sum = 0.0f;

		for (uint32 ray = 0; ray < directions.Points.Size; ray++) {
			const Vec3f local = DirectionSet::GetLocal(directions.Points[ray], offset_u, offset_v);
			const Vec3f direction = (tangent * local.X + bitangent * local.Y + normal * local.Z).Normalize();

			GatherHit hit;

			if (replay) {
				hit.Texel = cache[ray].Texel;
				hit.Albedo = DecodeCachedAlbedo(cache[ray].Albedo);
			}
			else {
				hit = TraceHit(origin, direction);

				if (cache != nullptr) {
					cache[ray] = CachedHit { hit.Texel, EncodeCachedAlbedo(hit.Albedo) };
				}
			}

			const Vec3f radiance = GetRadiance(hit, direct, indirect);
			const float32 luminance = GetLuminance(radiance);

			sum += radiance;
			direction_sum += direction * luminance;
			luminance_sum += luminance;
			luminance_sq_sum += luminance * luminance;
		}

		const float32 inv_count = 1.0f / static_cast<float32>(directions.Points.Size);
		const float32 mean_luminance = luminance_sum * inv_count;

		out_irradiance = sum * inv_count;
		out_variance = std::fmax(luminance_sq_sum * inv_count - mean_luminance * mean_luminance, 0.0f) * inv_count;
		out_direction = normal * scUniformDirectionality;

		if (luminance_sum > 0.0f) {
			out_direction = direction_sum * (1.0f / luminance_sum);
		}
	}

	/// Traces the same rays as Gather() and writes the texel each one hits (or scSkyHit / scDarkHit), and its albedo
	/// when `out_albedos` is given
	void TraceGatherHits(uint64 texel, const DirectionSet& directions, uint32* out_hits,
						 Vec3f* out_albedos = nullptr) const
	{
		const Vec3f origin = mInput.pPositions[texel];
		const Vec3f normal = mInput.pNormals[texel];

		Vec3f tangent;
		Vec3f bitangent;
		GetBasis(normal, tangent, bitangent);

		const float32 offset_u = HashToUnit(static_cast<uint32>(texel) * 2);
		const float32 offset_v = HashToUnit(static_cast<uint32>(texel) * 2 + 1);

		for (uint32 ray = 0; ray < directions.Points.Size; ray++) {
			const Vec3f local = DirectionSet::GetLocal(directions.Points[ray], offset_u, offset_v);
			const Vec3f direction = (tangent * local.X + bitangent * local.Y + normal * local.Z).Normalize();

			const GatherHit hit = TraceHit(origin, direction);

			out_hits[ray] = hit.Texel;

			if (out_albedos != nullptr) {
				out_albedos[ray] = hit.Albedo;
			}
		}
	}

	void BakeProbe(const Vec3f& position, const SizedArray<ProbeRay>& rays, const Vec3f* direct, const Vec3f* indirect,
				   float32* out_irradiance, uint16* out_moments) const
	{
		constexpr uint32 cTexelCount = ProbeEncoding::scMomentTexelCount;
		constexpr float32 cMaxDistance = Limits::ProbeDepthMaxDistance;

		Vec3f sh[Limits::ProbeSHCoeffCount] = {};

		double weights[cTexelCount] = {};
		double sums[cTexelCount] = {};
		double sums_sq[cTexelCount] = {};

		for (const ProbeRay& ray : rays) {
			const GatherHit hit = TraceHit(position, ray.Direction);
			const Vec3f radiance = GetRadiance(hit, direct, indirect);

			for (uint32 k = 0; k < Limits::ProbeSHCoeffCount; k++) {
				sh[k] += radiance * ray.WeightedBasis[k];
			}

			const double distance = std::fmin(hit.Distance, cMaxDistance);

			weights[ray.MomentTexel] += ray.SolidAngle;
			sums[ray.MomentTexel] += distance * ray.SolidAngle;
			sums_sq[ray.MomentTexel] += distance * distance * ray.SolidAngle;
		}

		for (uint32 k = 0; k < Limits::ProbeSHCoeffCount; k++) {
			const Vec3f coefficient = sh[k] * ProbeEncoding::scCosineLobe[k];

			out_irradiance[k * 3 + 0] = coefficient.X;
			out_irradiance[k * 3 + 1] = coefficient.Y;
			out_irradiance[k * 3 + 2] = coefficient.Z;
		}

		for (uint32 texel = 0; texel < cTexelCount; texel++) {
			double mean = cMaxDistance;
			double variance = 0.0;

			if (weights[texel] > 0.0) {
				mean = sums[texel] / weights[texel];
				variance = std::fmax(sums_sq[texel] / weights[texel] - mean * mean, 0.0);
			}

			out_moments[texel * 2 + 0] = ProbeEncoding::EncodeUNorm16(static_cast<float32>(mean / cMaxDistance));
			out_moments[texel * 2 + 1] = ProbeEncoding::EncodeUNorm16(
				static_cast<float32>(std::sqrt(variance) / cMaxDistance));
		}
	}

private:
	const MapSource& mSource;
	const IndirectInput& mInput;
	const Scene& mScene;
	const Array<TriangleShading>& mShadings;
	const SizedArray<AlbedoTexture>& mAlbedos;
};

constexpr uint32 scTransportLevels = TransportStats::scLevelCount;
constexpr uint32 scNoCluster = std::numeric_limits<uint32>::max();

/// Square blocks of texels per chart at each level. Level 0 is the texel itself.
class TransportClusters
{
public:
	explicit TransportClusters(const IndirectInput& input)
	{
		const uint64 texel_count = static_cast<uint64>(input.Width) * input.Height;

		for (uint32 level = 1; level < scTransportLevels; level++) {
			mClusters[level].InitSize(texel_count, scNoCluster);
		}

		for (const URect& rect : input.LightmapRects) {
			const Vec2u extent = rect.GetExtent();

			for (uint32 level = 1; level < scTransportLevels; level++) {
				const uint32 block = 1U << level;
				const uint32 blocks_x = (extent.X + block - 1) / block;
				const uint32 blocks_y = (extent.Y + block - 1) / block;
				const uint32 first = mClusterCounts[level];

				mClusterCounts[level] += blocks_x * blocks_y;

				for (uint32 y = 0; y < extent.Y; y++) {
					for (uint32 x = 0; x < extent.X; x++) {
						const uint64 texel = static_cast<uint64>(rect.Min.Y + y) * input.Width + rect.Min.X + x;
						mClusters[level][texel] = first + (y / block) * blocks_x + x / block;
					}
				}
			}
		}

		for (uint32 level = 1; level < scTransportLevels; level++) {
			SizedArray<uint8> covered;
			covered.InitSize(mClusterCounts[level], static_cast<uint8>(0));

			for (uint64 texel = 0; texel < texel_count; texel++) {
				const uint32 cluster = mClusters[level][texel];

				if (input.pCovered[texel] != 0 && cluster != scNoCluster) {
					covered[cluster] = 1;
				}
			}

			mCoveredCounts[level] = 0;

			for (uint8 flag : covered) {
				mCoveredCounts[level] += flag;
			}
		}

		for (uint64 texel = 0; texel < texel_count; texel++) {
			if (input.pCovered[texel] != 0 && mClusters[1][texel] != scNoCluster) {
				mCoveredCounts[0]++;
			}
		}
	}

	uint32 Get(uint32 level, uint32 texel) const
	{
		if (level == 0) {
			if (mClusters[1][texel] == scNoCluster) {
				return scNoCluster;
			}

			return texel;
		}

		return mClusters[level][texel];
	}

	uint32 GetClusterCount(uint32 level) const { return mClusterCounts[level]; }
	uint64 GetCoveredCount(uint32 level) const { return mCoveredCounts[level]; }

private:
	SizedArray<uint32> mClusters[scTransportLevels];
	uint32 mClusterCounts[scTransportLevels] = {};
	uint64 mCoveredCounts[scTransportLevels] = {};
};

struct LinkAccumulator
{
	std::atomic<uint64> Receivers { 0 };
	std::atomic<uint64> Unique { 0 };
	std::atomic<uint64> Link90 { 0 };
	std::atomic<uint64> Link95 { 0 };
	std::atomic<uint64> Link99 { 0 };
	std::atomic<uint32> MaxLink95 { 0 };

	void Add(uint32 unique, uint32 link90, uint32 link95, uint32 link99)
	{
		Receivers.fetch_add(1, std::memory_order_relaxed);
		Unique.fetch_add(unique, std::memory_order_relaxed);
		Link90.fetch_add(link90, std::memory_order_relaxed);
		Link95.fetch_add(link95, std::memory_order_relaxed);
		Link99.fetch_add(link99, std::memory_order_relaxed);

		uint32 previous = MaxLink95.load(std::memory_order_relaxed);

		while (previous < link95 && !MaxLink95.compare_exchange_weak(previous, link95, std::memory_order_relaxed)) {
		}
	}

	void Store(TransportLinkStats& out) const
	{
		out.ReceiverCount = Receivers.load();
		out.UniqueLinkSum = Unique.load();
		out.Link90Sum = Link90.load();
		out.Link95Sum = Link95.load();
		out.Link99Sum = Link99.load();
		out.MaxLink95 = MaxLink95.load();
	}
};

/// Sorts the source cluster of every ray a receiver shot, then counts how many distinct sources it needs to
/// keep 90/95/99% of its rays when the most-hit sources are kept first
void CountLinks(Array<uint32>& keys, Array<uint32>& runs, LinkAccumulator& accumulator)
{
	if (keys.Size == 0) {
		accumulator.Add(0, 0, 0, 0);
		return;
	}

	std::sort(keys.pData, keys.pData + keys.Size);

	runs.Clear();

	uint32 run = 1;

	for (uint32 i = 1; i < keys.Size; i++) {
		if (keys[i] == keys[i - 1]) {
			run++;
			continue;
		}

		runs.Emplace(run);
		run = 1;
	}

	runs.Emplace(run);

	std::sort(runs.pData, runs.pData + runs.Size, [](uint32 a, uint32 b) { return a > b; });

	const uint64 total = keys.Size;
	const uint32 percents[3] = { 90, 95, 99 };
	uint32 needed[3] = { runs.Size, runs.Size, runs.Size };

	uint64 sum = 0;
	uint32 next = 0;

	for (uint32 i = 0; i < runs.Size && next < 3; i++) {
		sum += runs[i];

		while (next < 3 && sum * 100 >= total * percents[next]) {
			needed[next] = i + 1;
			next++;
		}
	}

	accumulator.Add(runs.Size, needed[0], needed[1], needed[2]);
}

void MeasureTransport(const IndirectBake& bake, const IndirectInput& input, const Array<uint32>& texels,
					  const DirectionSet& directions, const CompiledMap& map, const LightmapOptions& options,
					  TransportStats& out_stats)
{
	constexpr uint32 scJobLevel = scTransportLevels - 1;
	constexpr uint32 scMaxJobTexels = (1U << scJobLevel) * (1U << scJobLevel);

	const auto start = std::chrono::steady_clock::now();

	const TransportClusters clusters(input);
	const uint32 ray_count = directions.Points.Size;
	const uint32 job_count = clusters.GetClusterCount(scJobLevel);

	SizedArray<uint32> job_offsets;
	job_offsets.InitSize(static_cast<uint64>(job_count) + 1, 0U);

	for (const uint32 texel : texels) {
		const uint32 job = clusters.Get(scJobLevel, texel);

		if (job != scNoCluster) {
			job_offsets[job + 1]++;
		}
	}

	for (uint32 job = 0; job < job_count; job++) {
		job_offsets[job + 1] += job_offsets[job];
	}

	SizedArray<uint32> job_texels;
	job_texels.InitSize(std::max<uint64>(job_offsets[job_count], 1), 0U);

	{
		SizedArray<uint32> cursor;
		cursor.InitSize(job_count, 0U);

		for (const uint32 texel : texels) {
			const uint32 job = clusters.Get(scJobLevel, texel);

			if (job != scNoCluster) {
				job_texels[job_offsets[job] + cursor[job]++] = texel;
			}
		}
	}

	LinkAccumulator texel_links[scTransportLevels][scTransportLevels];
	std::atomic<uint64> total_rays { 0 };
	std::atomic<uint64> sky_rays { 0 };
	std::atomic<uint64> dark_rays { 0 };
	std::atomic<uint64> unmapped_rays { 0 };

	RunParallel(
		job_count, 4,
		[&](uint32 job)
		{
			const uint32 first = job_offsets[job];
			const uint32 count = job_offsets[job + 1] - first;

			if (count == 0) {
				return;
			}

			const uint32* job_texel = job_texels.pData + first;

			SizedArray<uint32> hits;
			hits.InitSize(static_cast<uint64>(count) * ray_count);

			uint64 sky = 0;
			uint64 dark = 0;
			uint64 unmapped = 0;

			for (uint32 i = 0; i < count; i++) {
				uint32* texel_hits = hits.pData + static_cast<uint64>(i) * ray_count;

				bake.TraceGatherHits(job_texel[i], directions, texel_hits);

				for (uint32 ray = 0; ray < ray_count; ray++) {
					const uint32 hit = texel_hits[ray];

					if (hit == scSkyHit) {
						sky++;
					}
					else if (hit == scDarkHit) {
						dark++;
					}
					else if (clusters.Get(1, hit) == scNoCluster) {
						unmapped++;
					}
				}
			}

			total_rays.fetch_add(static_cast<uint64>(count) * ray_count, std::memory_order_relaxed);
			sky_rays.fetch_add(sky, std::memory_order_relaxed);
			dark_rays.fetch_add(dark, std::memory_order_relaxed);
			unmapped_rays.fetch_add(unmapped, std::memory_order_relaxed);

			Array<uint32> keys;
			Array<uint32> runs;

			for (uint32 receiver_level = 0; receiver_level < scTransportLevels; receiver_level++) {
				bool grouped[scMaxJobTexels] = {};
				uint32 members[scMaxJobTexels];

				for (uint32 i = 0; i < count; i++) {
					if (grouped[i]) {
						continue;
					}

					const uint32 receiver = clusters.Get(receiver_level, job_texel[i]);
					uint32 member_count = 0;

					for (uint32 j = i; j < count; j++) {
						if (!grouped[j] && clusters.Get(receiver_level, job_texel[j]) == receiver) {
							grouped[j] = true;
							members[member_count++] = j;
						}
					}

					for (uint32 source_level = 0; source_level < scTransportLevels; source_level++) {
						keys.Clear();

						for (uint32 m = 0; m < member_count; m++) {
							const uint32* texel_hits = hits.pData + static_cast<uint64>(members[m]) * ray_count;

							for (uint32 ray = 0; ray < ray_count; ray++) {
								const uint32 hit = texel_hits[ray];

								if (hit == scSkyHit || hit == scDarkHit) {
									continue;
								}

								const uint32 source = clusters.Get(source_level, hit);

								if (source != scNoCluster) {
									keys.Emplace(source);
								}
							}
						}

						CountLinks(keys, runs, texel_links[receiver_level][source_level]);
					}
				}
			}
		});

	LinkAccumulator probe_links[scTransportLevels];
	const uint32 probe_count = map.Probes.GetProbeCount();

	if (probe_count > 0 && options.ProbeTexelRays > 0) {
		const SizedArray<ProbeRay> probe_rays = BuildProbeRays(options.ProbeTexelRays);

		out_stats.ProbeRaysPerProbe = static_cast<uint32>(probe_rays.Size);

		RunParallel(probe_count, 1,
					[&](uint32 probe)
					{
						const Vec3f position = map.Probes.Positions[probe];

						SizedArray<uint32> hits;
						hits.InitSize(probe_rays.Size);

						for (uint64 ray = 0; ray < probe_rays.Size; ray++) {
							hits[ray] = bake.TraceHit(position, probe_rays[ray].Direction).Texel;
						}

						Array<uint32> keys;
						Array<uint32> runs;

						for (uint32 source_level = 0; source_level < scTransportLevels; source_level++) {
							keys.Clear();

							for (const uint32 hit : hits) {
								if (hit == scSkyHit || hit == scDarkHit) {
									continue;
								}

								const uint32 source = clusters.Get(source_level, hit);

								if (source != scNoCluster) {
									keys.Emplace(source);
								}
							}

							CountLinks(keys, runs, probe_links[source_level]);
						}
					});
	}

	for (uint32 receiver_level = 0; receiver_level < scTransportLevels; receiver_level++) {
		out_stats.ClusterCounts[receiver_level] = clusters.GetCoveredCount(receiver_level);
		probe_links[receiver_level].Store(out_stats.Probes[receiver_level]);

		for (uint32 source_level = 0; source_level < scTransportLevels; source_level++) {
			texel_links[receiver_level][source_level].Store(out_stats.Texels[receiver_level][source_level]);
		}
	}

	out_stats.bMeasured = true;
	out_stats.RaysPerTexel = ray_count;
	out_stats.RayCount = total_rays.load();
	out_stats.SkyRayCount = sky_rays.load();
	out_stats.DarkRayCount = dark_rays.load();
	out_stats.UnmappedRayCount = unmapped_rays.load();
	out_stats.Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

constexpr uint32 scTransportClusterSize = 8;
constexpr uint32 scTransportSamplesPerSide = 2;
/// Fraction of a cluster's surface hits that its kept links have to account for
constexpr float32 scTransportCoverage = 0.95f;
constexpr float32 scTransportDefaultAlbedo = 0.5f;

struct TransportHit
{
	uint32 Source;
	uint32 Ray;
};

struct TransportRun
{
	uint32 Source;
	uint32 Count;
	Vec3f AlbedoSum;
};

/**
 * @brief Splits every chart into square clusters, picks the points their direct light is measured at, and links each
 * cluster to the clusters its gather rays hit
 */
void BuildTransport(const IndirectBake& bake, const IndirectInput& input, const Array<uint32>& texels,
					const DirectionSet& directions, CompiledMapTransport& out_transport, LightmapStats& out_stats)
{
	constexpr uint32 cSize = scTransportClusterSize;
	constexpr uint32 cQuadrant = cSize / scTransportSamplesPerSide;

	const auto start = std::chrono::steady_clock::now();
	const uint64 texel_count = static_cast<uint64>(input.Width) * input.Height;
	const uint32 ray_count = directions.Points.Size;

	out_transport = CompiledMapTransport {};
	out_transport.ClusterSize = cSize;

	if (input.LightmapRects.Size == 0 || texels.Size == 0) {
		return;
	}

	out_transport.Charts.InitSize(input.LightmapRects.Size, CompiledMapTransportChart {});

	SizedArray<uint32> texel_clusters;
	texel_clusters.InitSize(texel_count, scNoCluster);

	uint32 cluster_count = 0;

	for (uint32 chart_index = 0; chart_index < input.LightmapRects.Size; chart_index++) {
		const URect& rect = input.LightmapRects[chart_index];
		const Vec2u extent = rect.GetExtent();

		CompiledMapTransportChart& chart = out_transport.Charts[chart_index];
		chart.Min[0] = rect.Min.X;
		chart.Min[1] = rect.Min.Y;
		chart.Size[0] = extent.X;
		chart.Size[1] = extent.Y;
		chart.Blocks[0] = (extent.X + cSize - 1) / cSize;
		chart.Blocks[1] = (extent.Y + cSize - 1) / cSize;
		chart.FirstCluster = cluster_count;

		cluster_count += chart.Blocks[0] * chart.Blocks[1];

		for (uint32 y = 0; y < extent.Y; y++) {
			for (uint32 x = 0; x < extent.X; x++) {
				const uint64 texel = static_cast<uint64>(rect.Min.Y + y) * input.Width + rect.Min.X + x;
				texel_clusters[texel] = chart.FirstCluster + (y / cSize) * chart.Blocks[0] + x / cSize;
			}
		}
	}

	out_transport.Clusters.InitSize(cluster_count, CompiledMapTransportCluster {});

	// Covered texels grouped by cluster
	SizedArray<uint32> cluster_offsets;
	cluster_offsets.InitSize(static_cast<uint64>(cluster_count) + 1, 0U);

	for (const uint32 texel : texels) {
		if (texel_clusters[texel] != scNoCluster) {
			cluster_offsets[texel_clusters[texel] + 1]++;
		}
	}

	for (uint32 cluster = 0; cluster < cluster_count; cluster++) {
		cluster_offsets[cluster + 1] += cluster_offsets[cluster];
	}

	SizedArray<uint32> cluster_texels;
	cluster_texels.InitSize(std::max<uint64>(cluster_offsets[cluster_count], 1), 0U);

	{
		SizedArray<uint32> cursor;
		cursor.InitSize(cluster_count, 0U);

		for (const uint32 texel : texels) {
			const uint32 cluster = texel_clusters[texel];

			if (cluster != scNoCluster) {
				cluster_texels[cluster_offsets[cluster] + cursor[cluster]++] = texel;
			}
		}
	}

	// One sample per quadrant, at the covered texel closest to the quadrant's centre
	Array<CompiledMapTransportSample> samples;

	for (const CompiledMapTransportChart& chart : out_transport.Charts) {
		for (uint32 block_y = 0; block_y < chart.Blocks[1]; block_y++) {
			for (uint32 block_x = 0; block_x < chart.Blocks[0]; block_x++) {
				const uint32 cluster_index = chart.FirstCluster + block_y * chart.Blocks[0] + block_x;
				CompiledMapTransportCluster& cluster = out_transport.Clusters[cluster_index];

				cluster.FirstSample = samples.Size;

				for (uint32 quadrant = 0; quadrant < scTransportSamplesPerSide * scTransportSamplesPerSide;
					 quadrant++) {
					const uint32 quadrant_x = quadrant % scTransportSamplesPerSide;
					const uint32 quadrant_y = quadrant / scTransportSamplesPerSide;

					const float32 center_x = static_cast<float32>(quadrant_x * cQuadrant) + cQuadrant * 0.5f;
					const float32 center_y = static_cast<float32>(quadrant_y * cQuadrant) + cQuadrant * 0.5f;

					uint32 best_texel = scNoCluster;
					float32 best_distance = std::numeric_limits<float32>::max();

					for (uint32 i = cluster_offsets[cluster_index]; i < cluster_offsets[cluster_index + 1]; i++) {
						const uint32 texel = cluster_texels[i];
						const uint32 local_x = texel % input.Width - chart.Min[0] - block_x * cSize;
						const uint32 local_y = texel / input.Width - chart.Min[1] - block_y * cSize;

						if (local_x / cQuadrant != quadrant_x || local_y / cQuadrant != quadrant_y) {
							continue;
						}

						const float32 dx = static_cast<float32>(local_x) + 0.5f - center_x;
						const float32 dy = static_cast<float32>(local_y) + 0.5f - center_y;
						const float32 distance = dx * dx + dy * dy;

						if (distance < best_distance) {
							best_distance = distance;
							best_texel = texel;
						}
					}

					if (best_texel == scNoCluster) {
						continue;
					}

					const Vec3f position = input.pPositions[best_texel];
					const Vec3f normal = input.pNormals[best_texel];

					CompiledMapTransportSample& sample = samples.Emplace();
					sample.Position[0] = position.X;
					sample.Position[1] = position.Y;
					sample.Position[2] = position.Z;
					sample.Normal[0] = normal.X;
					sample.Normal[1] = normal.Y;
					sample.Normal[2] = normal.Z;
				}

				cluster.SampleCount = samples.Size - cluster.FirstSample;
			}
		}
	}

	SizedArray<double> albedo_sums;
	albedo_sums.InitSize(static_cast<uint64>(cluster_count) * 3, 0.0);

	SizedArray<uint64> albedo_counts;
	albedo_counts.InitSize(cluster_count, static_cast<uint64>(0));

	Array<CompiledMapTransportLink> links;
	std::mutex merge_mutex;

	RunParallel(cluster_count, 4,
				[&](uint32 cluster_index)
				{
					const uint32 first = cluster_offsets[cluster_index];
					const uint32 count = cluster_offsets[cluster_index + 1] - first;

					if (count == 0) {
						return;
					}

					const uint64 total_rays = static_cast<uint64>(count) * ray_count;

					SizedArray<uint32> hits;
					hits.InitSize(total_rays);

					SizedArray<Vec3f> albedos;
					albedos.InitSize(total_rays);

					for (uint32 i = 0; i < count; i++) {
						bake.TraceGatherHits(cluster_texels[first + i], directions,
											 hits.pData + static_cast<uint64>(i) * ray_count,
											 albedos.pData + static_cast<uint64>(i) * ray_count);
					}

					Array<TransportHit> surface_hits;

					for (uint32 ray = 0; ray < total_rays; ray++) {
						const uint32 hit = hits[ray];

						if (hit == scSkyHit || hit == scDarkHit || texel_clusters[hit] == scNoCluster) {
							continue;
						}

						surface_hits.Emplace(TransportHit { texel_clusters[hit], ray });
					}

					if (surface_hits.Size == 0) {
						return;
					}

					std::sort(surface_hits.pData, surface_hits.pData + surface_hits.Size,
							  [](const TransportHit& a, const TransportHit& b) { return a.Source < b.Source; });

					Array<TransportRun> runs;

					for (const TransportHit& hit : surface_hits) {
						if (runs.Size == 0 || GetLast(runs).Source != hit.Source) {
							runs.Emplace(TransportRun { hit.Source, 0, Vec3f(0.0f) });
						}

						TransportRun& run = GetLast(runs);
						run.Count++;
						run.AlbedoSum += albedos[hit.Ray];
					}

					std::sort(runs.pData, runs.pData + runs.Size,
							  [](const TransportRun& a, const TransportRun& b) { return a.Count > b.Count; });

					const float32 needed = scTransportCoverage * static_cast<float32>(surface_hits.Size);

					uint32 kept = 0;
					uint32 kept_hits = 0;

					while (kept < runs.Size && static_cast<float32>(kept_hits) < needed) {
						kept_hits += runs[kept].Count;
						kept++;
					}

					// The dropped hits are spread over the kept links, so the cluster still sees as much surface
					const float32 scale = static_cast<float32>(surface_hits.Size) /
										  (static_cast<float32>(kept_hits) * static_cast<float32>(total_rays));

					std::lock_guard<std::mutex> guard(merge_mutex);

					CompiledMapTransportCluster& cluster = out_transport.Clusters[cluster_index];
					cluster.FirstLink = links.Size;
					cluster.LinkCount = kept;

					for (uint32 i = 0; i < kept; i++) {
						links.Emplace(CompiledMapTransportLink {
							runs[i].Source, static_cast<float32>(runs[i].Count) * scale });
					}

					for (const TransportRun& run : runs) {
						albedo_sums[static_cast<uint64>(run.Source) * 3 + 0] += run.AlbedoSum.X;
						albedo_sums[static_cast<uint64>(run.Source) * 3 + 1] += run.AlbedoSum.Y;
						albedo_sums[static_cast<uint64>(run.Source) * 3 + 2] += run.AlbedoSum.Z;
						albedo_counts[run.Source] += run.Count;
					}
				});

	for (uint32 cluster_index = 0; cluster_index < cluster_count; cluster_index++) {
		CompiledMapTransportCluster& cluster = out_transport.Clusters[cluster_index];
		const uint64 hit_count = albedo_counts[cluster_index];

		for (uint32 channel = 0; channel < 3; channel++) {
			cluster.Albedo[channel] = scTransportDefaultAlbedo;

			if (hit_count > 0) {
				cluster.Albedo[channel] = static_cast<float32>(albedo_sums[cluster_index * 3 + channel] /
															   static_cast<double>(hit_count));
			}
		}
	}

	if (samples.Size > 0) {
		out_transport.Samples.InitSize(samples.Size, CompiledMapTransportSample {});
		std::copy(samples.pData, samples.pData + samples.Size, out_transport.Samples.pData);
	}

	if (links.Size > 0) {
		out_transport.Links.InitSize(links.Size, CompiledMapTransportLink {});
		std::copy(links.pData, links.pData + links.Size, out_transport.Links.pData);
	}

	out_stats.TransportClusterCount = cluster_count;
	out_stats.TransportSampleCount = samples.Size;
	out_stats.TransportLinkCount = links.Size;
	out_stats.TransportSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

bool BakeIndirectLight(const MapSource& source, const SurfaceBuilder& surfaces, const IndirectInput& input,
					   const LightmapOptions& options, CompiledMap& out_map, LightmapStats& out_stats)
{
	constexpr float32 scMaxIrradiance = 65000.0f;
	constexpr uint64 scBytesPerMegabyte = 1024 * 1024;

	if (!options.bIndirect || options.IndirectBounces == 0 || options.IndirectRays == 0) {
		return false;
	}

	const SizedArray<AlbedoTexture> albedos = LoadAlbedos(out_map.Materials, out_stats.MissingAlbedoCount);

	Scene scene;
	Array<TriangleShading> shadings;

	for (const CompiledMapSurface& surface : out_map.Surfaces) {
		if (source.OpaqueMaterials[surface.MaterialIndex] == 0) {
			continue;
		}

		const bool has_texels = surface.HasLightmapTexcoords();

		for (size_t index = 0; index + 2 < surface.Indices.Size; index += 3) {
			const uint32 corners[3] = { surface.Indices[index], surface.Indices[index + 1],
										surface.Indices[index + 2] };

			uint32 shading_index = scNoShading;

			if (has_texels) {
				shading_index = shadings.Size;

				TriangleShading& shading = shadings.Emplace();
				shading.Material = surface.MaterialIndex;

				for (uint32 corner = 0; corner < 3; corner++) {
					const uint32 vertex = corners[corner];

					shading.U.mData[corner] = surface.LightmapTexcoords[vertex * 2] * static_cast<float32>(input.Width);
					shading.V.mData[corner] = surface.LightmapTexcoords[vertex * 2 + 1] *
											  static_cast<float32>(input.Height);

					shading.Texcoord[corner] = surface.Vertices[vertex].Texcoord;
					shading.Tint[corner] = Vec3f(1.0f);

					if (surface.HasVertexColors()) {
						shading.Tint[corner] = UnpackColor(surface.Colors[vertex]);
					}
				}
			}

			const Vec3f facing = surface.Vertices[corners[0]].Normal + surface.Vertices[corners[1]].Normal +
								 surface.Vertices[corners[2]].Normal;

			scene.AddTriangle(surface.Vertices[corners[0]].Position, surface.Vertices[corners[1]].Position,
							  surface.Vertices[corners[2]].Position, facing, shading_index);
		}
	}

	const uint32 surface_triangle_count = scene.GetTriangleCount();

	for (uint32 index = 0; index + 2 < surfaces.BlockerPoints.Size; index += 3) {
		scene.AddTriangle(surfaces.BlockerPoints[index].ToVec3f(), surfaces.BlockerPoints[index + 1].ToVec3f(),
						  surfaces.BlockerPoints[index + 2].ToVec3f(), Vec3f(0.0f), scNoShading);
	}

	scene.Build();

	const uint64 texel_count = static_cast<uint64>(input.Width) * input.Height;

	Array<uint32> texels;

	for (uint64 texel = 0; texel < texel_count; texel++) {
		if (input.pCovered[texel] != 0) {
			texels.Emplace(static_cast<uint32>(texel));
		}
	}

	SizedArray<Vec3f> direct;
	SizedArray<Vec3f> indirect;
	SizedArray<Vec3f> gathered;
	SizedArray<Vec3f> directions;
	SizedArray<float32> variance;

	direct.InitSize(texel_count, Vec3f(0.0f));
	indirect.InitSize(texel_count, Vec3f(0.0f));
	gathered.InitSize(texel_count, Vec3f(0.0f));
	directions.InitSize(texel_count, Vec3f(0.0f));
	variance.InitSize(texel_count, 0.0f);

	const IndirectBake bake(source, input, scene, shadings, albedos);

	RunParallel(texels.Size, [&](uint32 i) { direct[texels[i]] = bake.GetDirect(texels[i]); });

	Array<uint8> filled;
	Array<Vec3f> scratch_values;
	Array<uint8> scratch_filled;

	BleedEdges(input, direct.pData, filled, scratch_values, scratch_filled);

	const DirectionSet direction_set(options.IndirectRays);

	const uint64 cache_bytes_per_texel = static_cast<uint64>(options.IndirectRays) * sizeof(CachedHit);
	uint32 cached_texel_count = 0;

	if (options.IndirectBounces > 1) {
		const uint64 budget = static_cast<uint64>(options.IndirectCacheMegabytes) * scBytesPerMegabyte;
		cached_texel_count = static_cast<uint32>(std::min<uint64>(texels.Size, budget / cache_bytes_per_texel));
	}

	SizedArray<CachedHit> cache;

	if (cached_texel_count > 0) {
		cache.InitSize(static_cast<uint64>(cached_texel_count) * options.IndirectRays);
	}

	Denoiser denoiser(input, texels);
	const uint32 denoise_iterations = GetDenoiseIterations(options.IndirectDenoise);

	Vec3f* previous = indirect.pData;
	Vec3f* current = gathered.pData;

	for (uint32 bounce = 0; bounce < options.IndirectBounces; bounce++) {
		RunParallel(texels.Size,
					[&](uint32 i)
					{
						const uint32 texel = texels[i];

						CachedHit* hits = nullptr;

						if (i < cached_texel_count) {
							hits = cache.pData + static_cast<uint64>(i) * options.IndirectRays;
						}

						bake.Gather(texel, direction_set, direct.pData, previous, hits, bounce > 0 && hits != nullptr,
									current[texel], directions[texel], variance[texel]);
					});

		Vec3f* filtered_directions = nullptr;

		if (bounce + 1 == options.IndirectBounces) {
			filtered_directions = directions.pData;
		}

		denoiser.Filter(denoise_iterations, current, filtered_directions, variance.pData);
		BleedEdges(input, current, filled, scratch_values, scratch_filled);
		std::swap(previous, current);
	}

	const Vec3f* result = previous;

	BleedEdges(input, directions.pData, filled, scratch_values, scratch_filled);

	CompiledMapLightmap& lightmap = out_map.Lightmap;
	lightmap.Irradiance.InitSize(texel_count * 3);
	lightmap.Directions.InitSize(texel_count * 3);

	double luminance_sum = 0.0;

	for (uint64 texel = 0; texel < texel_count; texel++) {
		for (uint32 channel = 0; channel < 3; channel++) {
			float32 value = std::max(result[texel].mData[channel], 0.0f);

			lightmap.Irradiance[texel * 3 + channel] = Float16(std::fmin(value, scMaxIrradiance)).GetBits();
			lightmap.Directions[texel * 3 + channel] = EncodeDirection(directions[texel].mData[channel]);
		}

		if (input.pCovered[texel] != 0) {
			luminance_sum += GetLuminance(result[texel]);
		}
	}

	if (!options.ImagePath.IsEmpty()) {
		double mean_luminance = 0.0;

		if (texels.Size > 0) {
			mean_luminance = luminance_sum / static_cast<double>(texels.Size);
		}

		WriteImage(options.ImagePath + ".indirect.ppm", input.Width, input.Height, result, mean_luminance);
	}

	const uint32 probe_count = out_map.Probes.GetProbeCount();

	if (probe_count > 0 && options.ProbeTexelRays > 0) {
		const SizedArray<ProbeRay> probe_rays = BuildProbeRays(options.ProbeTexelRays);

		out_map.ProbeIrradiance.InitSize(static_cast<uint64>(probe_count) * CompiledMap::scProbeSHFloats);
		out_map.ProbeMoments.InitSize(static_cast<uint64>(probe_count) * CompiledMap::scProbeMomentValues);

		RunParallel(probe_count, 1,
					[&](uint32 probe)
					{
						bake.BakeProbe(
							out_map.Probes.Positions[probe], probe_rays, direct.pData, result,
							out_map.ProbeIrradiance.pData + static_cast<uint64>(probe) * CompiledMap::scProbeSHFloats,
							out_map.ProbeMoments.pData + static_cast<uint64>(probe) * CompiledMap::scProbeMomentValues);
					});

		double probe_luminance_sum = 0.0;

		for (uint32 probe = 0; probe < probe_count; probe++) {
			const float32* sh = out_map.ProbeIrradiance.pData +
								static_cast<uint64>(probe) * CompiledMap::scProbeSHFloats;
			probe_luminance_sum += GetLuminance(Vec3f(sh[0], sh[1], sh[2]) * ProbeEncoding::scSHNorm0);
		}

		out_stats.BakedProbeCount = probe_count;
		out_stats.ProbeRayCount = static_cast<uint64>(probe_count) * probe_rays.Size;
		out_stats.MeanProbeIrradiance = (probe_luminance_sum * FX_PI) / static_cast<double>(probe_count);
	}

	if (options.bTransportStats) {
		MeasureTransport(bake, input, texels, direction_set, out_map, options, out_stats.Transport);
	}

	out_map.Transport = CompiledMapTransport {};

	if (options.bLightTransport) {
		BuildTransport(bake, input, texels, direction_set, out_map.Transport, out_stats);
	}

	out_stats.bHasIndirect = true;
	out_stats.IndirectTriangleCount = surface_triangle_count;
	out_stats.IndirectBlockerCount = scene.GetTriangleCount() - surface_triangle_count;
	out_stats.IndirectLightCount = source.Lights.Size;
	out_stats.IndirectRayCount = static_cast<uint64>(texels.Size) * options.IndirectRays +
								 static_cast<uint64>(texels.Size - cached_texel_count) * options.IndirectRays *
									 (options.IndirectBounces - 1);
	out_stats.IndirectCachedTexelCount = cached_texel_count;
	out_stats.IndirectTexelCount = texels.Size;
	out_stats.IndirectCacheMegabytes = static_cast<double>(cached_texel_count * cache_bytes_per_texel) /
									   static_cast<double>(scBytesPerMegabyte);

	if (texels.Size > 0) {
		out_stats.MeanIndirect = (luminance_sum * FX_PI) / static_cast<double>(texels.Size);
	}

	return true;
}

} // namespace fx::mapc
