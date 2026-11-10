#include <Core/Defines.hpp>

#ifdef FX_USE_NEON

#include <Math/DVec3.hpp>

namespace fx {

const DVec3 DVec3::scZero = DVec3(0.0f, 0.0f, 0.0f);
const DVec3 DVec3::scOne = DVec3(1.0f, 1.0f, 1.0f);

const DVec3 DVec3::scUp = DVec3(0.0f, 1.0f, 0.0f);
const DVec3 DVec3::scRight = DVec3(1.0f, 0.0f, 0.0f);
const DVec3 DVec3::scForward = DVec3(0.0f, 0.0f, 1.0f);

} // namespace fx


#endif
