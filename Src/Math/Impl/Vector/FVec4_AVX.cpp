#include <Core/Defines.hpp>

#ifdef FX_USE_AVX

#include <Math/FVec3.hpp>
#include <Math/FVec4.hpp>
#include <Math/SSE.hpp>

namespace fx {

const FVec4 FVec4::sZero = FVec4(0.0f, 0.0f, 0.0f, 0.0f);
const FVec4 FVec4::sOne = FVec4(1.0f, 1.0f, 1.0f, 1.0f);

FVec4::FVec4(const FVec3& xyz, float32 w) { mIntrin = _mm_insert_ps(xyz.mIntrin, _mm_set_ss(w), 0x30); }

} // namespace fx


#endif // #ifdef FX_USE_AVX
