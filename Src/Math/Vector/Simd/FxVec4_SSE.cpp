#include <Core/FxDefines.hpp>

#ifdef FX_USE_SSE

#include <nmmintrin.h>

#include <Math/FxVec4.hpp>


const FxVec4f FxVec4f::sZero = FxVec4f(0.0f, 0.0f, 0.0f, 0.0f);
const FxVec4f FxVec4f::sOne = FxVec4f(1.0f, 1.0f, 1.0f, 1.0f);

///////////////////////////////
// Value Constructors
///////////////////////////////

FxVec4f::FxVec4f(float32 x, float32 y, float32 z, float32 w)
{
    // TODO: Check perf on cast (*reinterpret_cast<__m128*>(values_ptr)) vs using the load intrinsic

    const float32 values[4] = { x, y, z, w };
    mIntrin = _mm_load_ps(values);
}


FxVec4f::FxVec4f(float32 values[4]) { mIntrin = _mm_load_ps(values); }
FxVec4f::FxVec4f(float32 scalar) { mIntrin = _mm_set1_ps(scalar); }

void FxVec4f::Set(float32 x, float32 y, float32 z, float32 w)
{
    const float32 values[4] = { x, y, z, w };
    mIntrin = _mm_load_ps(values);
}

void FxVec4f::Load4Ptr(float32* values) { mIntrin = _mm_load_ps(values); }

void FxVec4f::Load1(float32 value) { mIntrin = _mm_set1_ps(value); }

void FxVec4f::Load4(float32 x, float32 y, float32 z, float32 w)
{
    const float32 values[4] = { x, y, z, w };
    mIntrin = _mm_load_ps(values);
}




#endif // #ifdef FX_USE_SSE