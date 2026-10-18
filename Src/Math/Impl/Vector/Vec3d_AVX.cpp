#include <Core/Defines.hpp>

#ifdef FX_USE_AVX

#include <Math/Vec3d.hpp>

namespace fx {

const Vec3d Vec3d::scZero = Vec3d(0.0f, 0.0f, 0.0f);
const Vec3d Vec3d::scOne = Vec3d(1.0f, 1.0f, 1.0f);

const Vec3d Vec3d::scUp = Vec3d(0.0f, 1.0f, 0.0f);
const Vec3d Vec3d::scRight = Vec3d(1.0f, 0.0f, 0.0f);
const Vec3d Vec3d::scForward = Vec3d(0.0f, 0.0f, 1.0f);

} // namespace fx

#endif
