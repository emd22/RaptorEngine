#pragma once

#include <Core/Types.hpp>
#include <Math/Vec3.hpp>

namespace fx::ReflectionFilter {

static constexpr uint32 scFaces = 6;

Vec3f FaceUVToDirection(uint32 face, float32 u, float32 v);

void DirectionToFaceUV(const Vec3f& direction, uint32& out_face, float32& out_u, float32& out_v);

uint64 GetTexelCount(uint32 size, uint32 mip_count);

float32 MipToRoughness(uint32 mip, uint32 mip_count);

uint16 FloatToHalf(float32 value);

void Prefilter(const float32* const faces[scFaces], uint32 size, uint32 mip_count, uint16* out_texels);

}
