#ifdef FX_USE_NEON

#include <arm_neon.h>

#include <Math/FxVec3.hpp>

const FxVec3f FxVec3f::Up = FxVec3f(0.0f, 1.0f, 0.0f);
const FxVec3f FxVec3f::Zero = FxVec3f(0.0f, 0.0f, 0.0f);
const FxVec3f FxVec3f::One = FxVec3f(1.0f, 1.0f, 1.0f);

FxVec3f::FxVec3f(float32 x, float32 y, float32 z)
{
    const float32 values[4] = {x, y, z, 0};
    mIntrin = vld1q_f32(values);
}

FxVec3f::FxVec3f(float32 scalar)
{
    mIntrin = vdupq_n_f32(scalar);
}

void FxVec3f::Set(float32 x, float32 y, float32 z)
{
    const float32 values[4] = {x, y, z, 0};
    mIntrin = vld1q_f32(values);
}


FxVec3f FxVec3f::MulAdd(const FxVec3f& add_value, const FxVec3f& mul_a, const FxVec3f& mul_b)
{
    FxVec3f result;
    result.mIntrin = vmlaq_f32(add_value, mul_a, mul_b);
    return result;
}

void FxVec3f::Print() const
{
    Log::Info("Vec3f {X=%.06f, Y=%.06f, Z=%.06f}", X, Y, Z);
}

float32 FxVec3f::Length() const
{
    float32x4_t vec = mIntrin;

    // Square the vector
    vec = vmulq_f32(vec, vec);
    // Add all components (horizontal add)
    float32 len2 = vaddvq_f32(vec);

    return sqrtf(len2);
}

FxVec3f FxVec3f::Normalize() const
{
    // Calculate length
    const float32 len = Length();
    // Splat to register
    const float32x4_t len_v = vdupq_n_f32(len);

    // Load vector into register
    float32x4_t v = mIntrin;
    // Divide vector by length
    v = vdivq_f32(v, len_v);

    return FxVec3f(v);
}

void FxVec3f::NormalizeIP()
{
    // Calculate length
    const float32 len = Length();
    // Splat to register
    const float32x4_t len_v = vdupq_n_f32(len);

    // Divide vector by length
    mIntrin = vdivq_f32(mIntrin, len_v);
}

FxVec3f FxVec3f::CrossSlow(const FxVec3f& other) const
{
    const float32 ax = mData[0];
    const float32 bx = other.mData[0];

    const float32 ay = mData[1];
    const float32 by = other.mData[1];

    const float32 az = mData[2];
    const float32 bz = other.mData[2];

    return FxVec3f(ay * bz - by * az, az * bx - bz * ax, ax * by - bx * ay);
}

static inline float32x4_t ShuffleYZXW(float32x4_t xyzw)
{
    // X Y Z W -> Y Z W X
    xyzw = vextq_f32(xyzw, xyzw, 1);

    // Y Z
    float32x2_t a_lo = vget_low_f32(xyzw);

    // W X -> X W
    float32x2_t a_hi = vrev64_f32(vget_high_f32(xyzw));

    // Y Z X W
    return vcombine_f32(a_lo, a_hi);
}

FxVec3f FxVec3f::Cross(const FxVec3f& other) const
{
    const float32x4_t a = mIntrin;
    const float32x4_t b = other.mIntrin;

    // Shuffle this vector and the other vector
    float32x4_t a_yzxw = ShuffleYZXW(a);
    float32x4_t b_yzxw = ShuffleYZXW(b);

    const float32x4_t result_yzxw = vsubq_f32(vmulq_f32(a, b_yzxw), vmulq_f32(a_yzxw, b));

    return FxVec3f(ShuffleYZXW(result_yzxw));
}

float32 FxVec3f::Dot(const FxVec3f& other) const
{
    float32x4_t prod = vmulq_f32(mIntrin, other.mIntrin);
    return vaddvq_f32(prod);
}

//////////////////////////////
// Operator Overloads
//////////////////////////////

FxVec3f FxVec3f::operator+(const FxVec3f& other) const
{
    float32x4_t result = vaddq_f32(mIntrin, other.mIntrin);
    return FxVec3f(result);
}

FxVec3f FxVec3f::operator-(const FxVec3f& other) const
{
    float32x4_t result = vsubq_f32(mIntrin, other.mIntrin);
    return FxVec3f(result);
}

FxVec3f FxVec3f::operator*(const FxVec3f& other) const
{
    float32x4_t result = vmulq_f32(mIntrin, other.mIntrin);
    return FxVec3f(result);
}

FxVec3f FxVec3f::operator*(float32 scalar) const
{
    float32x4_t result = vmulq_n_f32(mIntrin, scalar);
    return FxVec3f(result);
}

FxVec3f FxVec3f::operator-() const
{
    float32x4_t result = vnegq_f32(mIntrin);
    return FxVec3f(result);
}

FxVec3f FxVec3f::operator/(float32 scalar) const
{
    float32x4_t result = vdivq_f32(mIntrin, vdupq_n_f32(scalar));
    return FxVec3f(result);
}

FxVec3f& FxVec3f::operator+=(const FxVec3f& other)
{
    mIntrin = vaddq_f32(mIntrin, other);
    return *this;
}

FxVec3f& FxVec3f::operator-=(const FxVec3f& other)
{
    mIntrin = vsubq_f32(mIntrin, other);
    return *this;
}

FxVec3f& FxVec3f::operator*=(const FxVec3f& other)
{
    mIntrin = vmulq_f32(mIntrin, other);
    return *this;
}


#endif
