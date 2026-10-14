#include <Core/Defines.hpp>

#ifdef FX_USE_NEON

#include <arm_neon.h>

#include <Math/Vec3.hpp>
#include <Math/Vec4.hpp>

namespace fx {

const Vec4f Vec4f::sZero = Vec4f(0.0f, 0.0f, 0.0f, 0.0f);
const Vec4f Vec4f::sOne = Vec4f(1.0f, 1.0f, 1.0f, 1.0f);

Vec4f::Vec4f(const Vec3f& xyz, float32 w) { mIntrin = vsetq_lane_f32(w, xyz.mIntrin, 3); }

} // namespace fx

#endif // #ifdef FX_USE_NEON
