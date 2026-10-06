#include <Core/Defines.hpp>

#ifdef FX_USE_NEON

#include <math.h>

#include <Math/MathUtil.hpp>
#include <Math/NeonUtil.hpp>
#include <Math/Quat.hpp>
#include <raptor_ffi.h>
#include <Math/Vec3.hpp>

namespace fx {

const Quat Quat::scIdentity = Quat(0, 0, 0, 1);

Quat::Quat(float32 x, float32 y, float32 z, float32 w)
{
	float32 values[] = { x, y, z, w };
	mIntrin = vld1q_f32(values);
}


Quat Quat::FromAxisAngle(Vec3f axis, float32 angle)
{
	float32 out[4];
	rx_quat_from_axis_angle(&axis.mData[0], angle, out);

	return Quat(out[0], out[1], out[2], out[3]);
}




Quat Quat::FromEulerAngles(Vec3f angles)
{
	float32 out[4];
	rx_quat_from_euler(&angles.mData[0], out);

	return Quat(out[0], out[1], out[2], out[3]);
}

Quat Quat::operator*(const Quat& other) const
{
	float32 out[4];
	rx_quat_mul(&mData[0], &other.mData[0], out);

	return Quat(out[0], out[1], out[2], out[3]);
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
	Vec3f angles;
	rx_quat_euler_angles(&mData[0], &angles.mData[0]);

	return angles;
}

} // namespace fx

#endif
