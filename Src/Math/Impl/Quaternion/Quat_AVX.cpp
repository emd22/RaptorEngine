#include <Core/Defines.hpp>

#ifdef FX_USE_AVX

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Math/Quat.h>
#include <math.h>

#include <Core/Log.hpp>
#include <Math/MathUtil.hpp>
#include <Math/Quat.hpp>
#include <Math/SSE.hpp>
#include <Math/SSEUtil.hpp>

namespace fx {

const Quat Quat::sIdentity = Quat(0, 0, 0, 1);

Quat::Quat(float32 x, float32 y, float32 z, float32 w)
{
    float32 values alignas(16)[4] = { x, y, z, w };
    mIntrin = _mm_load_ps(values);
}

Quat::Quat(const JPH::Quat& other) { mIntrin = other.mValue.mValue; }

Quat Quat::FromAxisAngle(Vec3f axis, float32 angle)
{
    float32 sv, cv;
    MathUtil::SinCos(angle, &sv, &cv);

    LogInfo("Sin {} = {}, Cos {} = {}", angle, sv, angle, cv);

    const __m128 vec = _mm_mul_ps(SSE::Normalize(axis.mIntrin), _mm_set1_ps(sv));

    // return Quat(vsetq_lane_f32(cv, vec, 3));
    return Quat(_mm_insert_ps(vec, _mm_set_ss(cv), 0x30));
}

bool Quat::IsCloseTo(const JPH::Quat& other, const float32 tolerance) const { return IsCloseTo(other.mValue.mValue); }


void Quat::FromJoltQuaternion(const JPH::Quat& quat) { mIntrin = quat.mValue.mValue; }
void Quat::ToJoltQuaternion(JPH::Quat& quat) const { quat.mValue.mValue = mIntrin; }

Quat Quat::FromEulerAngles(Vec3f angles)
{
    /*
        Create the quaternion using

        cz * sx * cy - sz * cx * sy,
        cz * cx * sy + sz * sx * cy,
        sz * cx * cy - cz * sx * sy,
        cz * cx * cy + sz * sx * sy
    */

    const __m128 half_one = _mm_set1_ps(0.5);

    // Sin and Cos all lanes at once
    __m128 sv = _mm_setzero_ps();
    __m128 cv = sv;
    SSE::SinCos4(_mm_mul_ps(angles.mIntrin, half_one), &sv, &cv);

    float32 sin_vals[4];
    float32 cos_vals[4];

    // Load the values from our vector into local arrays
    _mm_store_ps(sin_vals, sv);
    _mm_store_ps(cos_vals, cv);

    const float32 sx = sin_vals[0];
    const float32 sy = sin_vals[1];
    const float32 sz = sin_vals[2];

    const float32 cx = cos_vals[0];
    const float32 cy = cos_vals[1];
    const float32 cz = cos_vals[2];

    // Note that this is faster than the NEON optimized version.

    return Quat(cz * sx * cy - sz * cx * sy, /* First Row */
                cz * cx * sy + sz * sx * cy, /* Second Row */
                sz * cx * cy - cz * sx * sy, /* Third Row */
                cz * cx * cy + sz * sx * sy);
}

Quat Quat::operator*(const Quat& other) const
{
    const float lx = X;
    const float ly = Y;
    const float lz = Z;
    const float lw = W;

    const float rx = other.X;
    const float ry = other.Y;
    const float rz = other.Z;
    const float rw = other.W;

    return Quat(lw * rx + lx * rw + ly * rz - lz * ry, /* */
                lw * ry - lx * rz + ly * rw + lz * rx, /* */
                lw * rz + lx * ry - ly * rx + lz * rw, /* */
                lw * rw - lx * rx - ly * ry - lz * rz);
}

// Quat Quat::FromEulerAngles_NeonTest(Vec3f angles)
// {
//     Quat quat;

//     /*
//         Create the quaternion using

//         cz * sx * cy - sz * cx * sy,
//         cz * cx * sy + sz * sx * cy,
//         sz * cx * cy - cz * sx * sy,
//         cz * cx * cy + sz * sx * sy
//     */

//     const float32x4_t half_one = vdupq_n_f32(0.5);

//     // Sin and Cos all lanes at once
//     float32x4_t sv, cv;
//     Neon::SinCos4(vmulq_f32(angles.mIntrin, half_one), &sv, &cv);

//     float32x4_t lhs_term;

//     float32x4_t tmp0, tmp1;

//     /*
//         LHS term:

//         cz * sx * cy
//         cz * cx * sy
//         sz * cx * cy
//         cz * cx * cy
//      */
//     {
//         // Sin -> A, Cos -> B

//         // First Vector = { BZ, BZ, AZ, BZ }
//         // Splat Z of Cos to all lanes
//         lhs_term = vdupq_lane_f32(vget_high_f32(cv), 0);
//         // Set the Z component to Sin.Z
//         lhs_term = vsetq_lane_f32(vgetq_lane_f32(sv, 2), lhs_term, 2);

//         // Second Vector = { AX, BX, BX, BX }
//         // Splat X of Cos to all lanes
//         tmp0 = vdupq_lane_f32(vget_low_f32(cv), 0);
//         // Set the X Component to Sin.X
//         tmp0 = vsetq_lane_f32(vgetq_lane_f32(sv, 0), tmp0, 0);

//         // Third Vector = { BY, AY, BY, BY }
//         // Splat Y of Cos to all lanes
//         tmp1 = vdupq_lane_f32(vget_low_f32(cv), 1);
//         // Set the Y Component to Sin.Y
//         tmp1 = vsetq_lane_f32(vgetq_lane_f32(sv, 1), tmp1, 1);

//         // {cz, cz, sz, cz} * {sx, cx, cx, cx} * {cy, sy, cy, cy}
//         lhs_term = vmulq_f32(vmulq_f32(lhs_term, tmp0), tmp1);
//     }

//     /*
//         RHS term:

//         sz * cx * sy,
//         sz * sx * cy,
//         cz * sx * sy,
//         sz * sx * sy;
//      */
//     float32x4_t rhs_term;
//     {
//         // Sin -> A, Cos -> B

//         // First Vector = { AZ, AZ, BZ, AZ }
//         // Splat Z of Cos to all lanes
//         rhs_term = vdupq_lane_f32(vget_high_f32(sv), 0);
//         // Set the Z component to Sin.Z
//         rhs_term = vsetq_lane_f32(vgetq_lane_f32(cv, 2), rhs_term, 2);

//         // Second Vector = { BX, AX, AX, AX }
//         // Splat X of Cos to all lanes
//         tmp0 = vdupq_lane_f32(vget_low_f32(sv), 0);
//         // Set the X Component to Sin.X
//         tmp0 = vsetq_lane_f32(vgetq_lane_f32(cv, 0), tmp0, 0);

//         // Third Vector = { AY, BY, AY, AY }
//         // Splat Y of Cos to all lanes
//         tmp1 = vdupq_lane_f32(vget_low_f32(sv), 1);
//         // Set the Y Component to Sin.Y
//         tmp1 = vsetq_lane_f32(vgetq_lane_f32(cv, 1), tmp1, 1);

//         // {cz, cz, sz, cz} * {sx, cx, cx, cx} * {cy, sy, cy, cy}
//         rhs_term = vmulq_f32(vmulq_f32(rhs_term, tmp0), tmp1);
//     }

//     quat.mIntrin = vaddq_f32(lhs_term, Neon::SetSign<-1, 1, -1, 1>(rhs_term));

//     return quat;
// }


Vec3f Quat::GetEulerAngles() const
{
    const float32 y_sq = Y * Y;

    // X
    float t0 = 2.0f * (W * X + Y * Z);
    float t1 = 1.0f - 2.0f * (X * X + y_sq);

    // Y
    float t2 = 2.0f * (W * Y - Z * X);
    t2 = t2 > 1.0f ? 1.0f : t2;
    t2 = t2 < -1.0f ? -1.0f : t2;

    // Z
    float t3 = 2.0f * (W * Z + X * Y);
    float t4 = 1.0f - 2.0f * (y_sq + Z * Z);

    return Vec3f(atan2(t0, t1), sin(t2), atan2(t3, t4));
}

} // namespace fx


#endif
