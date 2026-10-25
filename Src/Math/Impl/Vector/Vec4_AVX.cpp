#include <Core/Defines.hpp>

#ifdef FX_USE_AVX

#include <Math/SSE.hpp>
#include <Math/Vec3.hpp>
#include <Math/Vec4.hpp>

namespace fx {

const Vec4f Vec4f::sZero = Vec4f(0.0f, 0.0f, 0.0f, 0.0f);
const Vec4f Vec4f::sOne = Vec4f(1.0f, 1.0f, 1.0f, 1.0f);

Vec4f::Vec4f(const Vec3f& xyz, float32 w) { mIntrin = _mm_insert_ps(xyz.mIntrin, _mm_set_ss(w), 0x30); }

} // namespace fx


#endif // #ifdef FX_USE_AVX
