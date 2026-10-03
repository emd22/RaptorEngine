#include "ReflectionFilter.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <vector>

namespace fx::ReflectionFilter {

namespace {

constexpr uint32 scSampleCount = 128;
constexpr float32 scPi = 3.14159265359f;
constexpr float32 scMaxHalf = 65504.0f;

struct CubeLevel
{
	uint32 Size = 0;
	std::vector<float32> Faces[scFaces];
};

struct LobeSample
{
	float32 Direction[3];
	float32 Weight;
	float32 Lod;
};

void SampleFace(const CubeLevel& level, uint32 face, float32 u, float32 v, float32 out[3])
{
	const float32 size = static_cast<float32>(level.Size);
	const float32 max_coord = size - 1.0f;

	const float32 x = std::clamp((u * 0.5f + 0.5f) * size - 0.5f, 0.0f, max_coord);
	const float32 y = std::clamp((v * 0.5f + 0.5f) * size - 0.5f, 0.0f, max_coord);

	const uint32 x0 = static_cast<uint32>(x);
	const uint32 y0 = static_cast<uint32>(y);
	const uint32 x1 = std::min(x0 + 1, level.Size - 1);
	const uint32 y1 = std::min(y0 + 1, level.Size - 1);

	const float32 fx = x - static_cast<float32>(x0);
	const float32 fy = y - static_cast<float32>(y0);

	const float32* texels = level.Faces[face].data();
	const float32* t00 = texels + (y0 * level.Size + x0) * 3;
	const float32* t10 = texels + (y0 * level.Size + x1) * 3;
	const float32* t01 = texels + (y1 * level.Size + x0) * 3;
	const float32* t11 = texels + (y1 * level.Size + x1) * 3;

	for (uint32 c = 0; c < 3; c++) {
		const float32 top = t00[c] + (t10[c] - t00[c]) * fx;
		const float32 bottom = t01[c] + (t11[c] - t01[c]) * fx;

		out[c] = top + (bottom - top) * fy;
	}
}

void SampleCube(const std::vector<CubeLevel>& levels, const Vec3f& direction, float32 lod, float32 out[3])
{
	uint32 face;
	float32 u;
	float32 v;
	DirectionToFaceUV(direction, face, u, v);

	const float32 max_lod = static_cast<float32>(levels.size() - 1);
	lod = std::clamp(lod, 0.0f, max_lod);

	const uint32 lower = static_cast<uint32>(lod);
	const uint32 upper = std::min(lower + 1, static_cast<uint32>(levels.size() - 1));
	const float32 blend = lod - static_cast<float32>(lower);

	SampleFace(levels[lower], face, u, v, out);

	if (blend > 0.0f && upper != lower) {
		float32 coarse[3];
		SampleFace(levels[upper], face, u, v, coarse);

		for (uint32 c = 0; c < 3; c++) {
			out[c] += (coarse[c] - out[c]) * blend;
		}
	}
}

std::vector<CubeLevel> BuildSourceLevels(const float32* const faces[scFaces], uint32 size)
{
	std::vector<CubeLevel> levels;

	CubeLevel base;
	base.Size = size;

	for (uint32 face = 0; face < scFaces; face++) {
		base.Faces[face].assign(faces[face], faces[face] + static_cast<uint64>(size) * size * 3);
	}

	levels.push_back(std::move(base));

	while (levels.back().Size > 1) {
		const CubeLevel& parent = levels.back();

		CubeLevel child;
		child.Size = parent.Size / 2;

		for (uint32 face = 0; face < scFaces; face++) {
			child.Faces[face].resize(static_cast<uint64>(child.Size) * child.Size * 3);

			const float32* src = parent.Faces[face].data();
			float32* dst = child.Faces[face].data();

			for (uint32 y = 0; y < child.Size; y++) {
				for (uint32 x = 0; x < child.Size; x++) {
					const float32* row0 = src + ((y * 2) * parent.Size + (x * 2)) * 3;
					const float32* row1 = row0 + parent.Size * 3;

					for (uint32 c = 0; c < 3; c++) {
						*dst++ = (row0[c] + row0[c + 3] + row1[c] + row1[c + 3]) * 0.25f;
					}
				}
			}
		}

		levels.push_back(std::move(child));
	}

	return levels;
}

float32 RadicalInverse(uint32 bits)
{
	bits = (bits << 16) | (bits >> 16);
	bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
	bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
	bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
	bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);

	return static_cast<float32>(bits) * 2.3283064365386963e-10f;
}

std::vector<LobeSample> BuildLobeSamples(float32 roughness, uint32 source_size)
{
	const float32 alpha = roughness * roughness;
	const float32 alpha_sq = alpha * alpha;

	const float32 texel_solid_angle = (4.0f * scPi) / (6.0f * static_cast<float32>(source_size * source_size));

	std::vector<LobeSample> samples;
	samples.reserve(scSampleCount);

	for (uint32 i = 0; i < scSampleCount; i++) {
		const float32 xi_x = (static_cast<float32>(i) + 0.5f) / static_cast<float32>(scSampleCount);
		const float32 xi_y = RadicalInverse(i);

		const float32 phi = 2.0f * scPi * xi_x;
		const float32 cos_theta = std::sqrt((1.0f - xi_y) / (1.0f + (alpha_sq - 1.0f) * xi_y));
		const float32 sin_theta = std::sqrt(std::max(1.0f - cos_theta * cos_theta, 0.0f));

		const float32 half_x = sin_theta * std::cos(phi);
		const float32 half_y = sin_theta * std::sin(phi);
		const float32 half_z = cos_theta;

		const float32 n_dot_l = 2.0f * half_z * half_z - 1.0f;

		if (n_dot_l <= 0.0f) {
			continue;
		}

		const float32 denom = half_z * half_z * (alpha_sq - 1.0f) + 1.0f;
		const float32 distribution = alpha_sq / (scPi * denom * denom);

		const float32 pdf = distribution * 0.25f;
		const float32 sample_solid_angle = 1.0f / (static_cast<float32>(scSampleCount) * pdf);

		LobeSample sample;
		sample.Direction[0] = 2.0f * half_z * half_x;
		sample.Direction[1] = 2.0f * half_z * half_y;
		sample.Direction[2] = n_dot_l;
		sample.Weight = n_dot_l;
		sample.Lod = std::max(0.5f * std::log2(sample_solid_angle / texel_solid_angle) + 1.0f, 0.0f);

		samples.push_back(sample);
	}

	return samples;
}

void WriteTexel(uint16*& out, const float32 rgb[3])
{
	*out++ = FloatToHalf(rgb[0]);
	*out++ = FloatToHalf(rgb[1]);
	*out++ = FloatToHalf(rgb[2]);
	*out++ = FloatToHalf(1.0f);
}

}

Vec3f FaceUVToDirection(uint32 face, float32 u, float32 v)
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

void DirectionToFaceUV(const Vec3f& direction, uint32& out_face, float32& out_u, float32& out_v)
{
	const float32 x = direction.X;
	const float32 y = direction.Y;
	const float32 z = direction.Z;

	const float32 ax = std::abs(x);
	const float32 ay = std::abs(y);
	const float32 az = std::abs(z);

	if (ax >= ay && ax >= az) {
		const float32 inv = 1.0f / std::max(ax, 1e-20f);

		out_face = (x > 0.0f) ? 0 : 1;
		out_u = ((x > 0.0f) ? -z : z) * inv;
		out_v = -y * inv;
	}
	else if (ay >= az) {
		const float32 inv = 1.0f / ay;

		out_face = (y > 0.0f) ? 2 : 3;
		out_u = x * inv;
		out_v = ((y > 0.0f) ? z : -z) * inv;
	}
	else {
		const float32 inv = 1.0f / az;

		out_face = (z > 0.0f) ? 4 : 5;
		out_u = ((z > 0.0f) ? x : -x) * inv;
		out_v = -y * inv;
	}
}

uint64 GetTexelCount(uint32 size, uint32 mip_count)
{
	uint64 count = 0;

	for (uint32 mip = 0; mip < mip_count; mip++) {
		const uint64 mip_size = std::max(size >> mip, 1U);
		count += mip_size * mip_size * scFaces;
	}

	return count;
}

float32 MipToRoughness(uint32 mip, uint32 mip_count)
{
	if (mip_count <= 1) {
		return 0.0f;
	}

	return static_cast<float32>(mip) / static_cast<float32>(mip_count - 1);
}

uint16 FloatToHalf(float32 value)
{
	if (!(value > 0.0f)) {
		return 0;
	}

	const uint32 bits = std::bit_cast<uint32>(std::min(value, scMaxHalf));
	const int32 exponent = static_cast<int32>((bits >> 23) & 0xFF) - 127 + 15;
	const uint32 mantissa = bits & 0x7FFFFF;

	if (exponent <= 0) {
		if (exponent < -10) {
			return 0;
		}

		const uint32 shift = static_cast<uint32>(14 - exponent);
		const uint32 full = mantissa | 0x800000;

		return static_cast<uint16>((full >> shift) + ((full >> (shift - 1)) & 1));
	}

	return static_cast<uint16>(((static_cast<uint32>(exponent) << 10) | (mantissa >> 13)) + ((mantissa >> 12) & 1));
}

void Prefilter(const float32* const faces[scFaces], uint32 size, uint32 mip_count, uint16* out_texels)
{
	const std::vector<CubeLevel> levels = BuildSourceLevels(faces, size);

	uint16* out = out_texels;

	for (uint32 face = 0; face < scFaces; face++) {
		const float32* texel = levels[0].Faces[face].data();

		for (uint32 i = 0; i < size * size; i++, texel += 3) {
			WriteTexel(out, texel);
		}
	}

	for (uint32 mip = 1; mip < mip_count; mip++) {
		const uint32 mip_size = std::max(size >> mip, 1U);
		const std::vector<LobeSample> samples = BuildLobeSamples(MipToRoughness(mip, mip_count), size);

		for (uint32 face = 0; face < scFaces; face++) {
			for (uint32 y = 0; y < mip_size; y++) {
				for (uint32 x = 0; x < mip_size; x++) {
					const float32 u = ((static_cast<float32>(x) + 0.5f) / static_cast<float32>(mip_size)) * 2.0f - 1.0f;
					const float32 v = ((static_cast<float32>(y) + 0.5f) / static_cast<float32>(mip_size)) * 2.0f - 1.0f;

					const Vec3f normal = FaceUVToDirection(face, u, v).Normalize();
					const Vec3f up = (std::abs(normal.Z) < 0.999f) ? Vec3f(0.0f, 0.0f, 1.0f) : Vec3f(1.0f, 0.0f, 0.0f);
					const Vec3f tangent = up.Cross(normal).Normalize();
					const Vec3f bitangent = normal.Cross(tangent);

					float32 sum[3] = {};
					float32 weight = 0.0f;

					for (const LobeSample& sample : samples) {
						const Vec3f direction = tangent * sample.Direction[0] + bitangent * sample.Direction[1] +
												normal * sample.Direction[2];

						float32 radiance[3];
						SampleCube(levels, direction, sample.Lod, radiance);

						for (uint32 c = 0; c < 3; c++) {
							sum[c] += radiance[c] * sample.Weight;
						}

						weight += sample.Weight;
					}

					const float32 inv_weight = (weight > 0.0f) ? (1.0f / weight) : 0.0f;
					const float32 filtered[3] = { sum[0] * inv_weight, sum[1] * inv_weight, sum[2] * inv_weight };

					WriteTexel(out, filtered);
				}
			}
		}
	}
}

}
