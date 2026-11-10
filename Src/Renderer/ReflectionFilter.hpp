#pragma once

#include <Core/Types.hpp>
#include <Math/FVec3.hpp>
#include <Math/Float16.hpp>

namespace fx::ReflectionFilter {

static constexpr uint32 scFaces = 6;

FVec3 FaceUVToDirection(uint32 face, float32 u, float32 v);

void DirectionToFaceUV(const FVec3 direction, uint32& out_face, float32& out_u, float32& out_v);

uint64 GetTexelCount(uint32 size, uint32 mip_count);

float32 MipToRoughness(uint32 mip, uint32 mip_count);

void Prefilter(const float32* const faces[scFaces], uint32 size, uint32 mip_count, Float16* out_texels);

} // namespace fx::ReflectionFilter
