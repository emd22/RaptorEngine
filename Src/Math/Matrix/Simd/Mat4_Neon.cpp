
#include <arm_neon.h>
#ifdef FX_USE_NEON

#include <Math/Mat4.hpp>

const FxMat4f FxMat4f::Identity = FxMat4f((float32[16]) { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 });

float32x4_t FxMat4f::MultiplyVec4f_Neon(FxVec4f& vec)
{
    float32x4_t result = vmovq_n_f32(0);

    float32x4_t a0 = Columns[0].mIntrin;
    float32x4_t a1 = Columns[1].mIntrin;
    float32x4_t a2 = Columns[2].mIntrin;
    float32x4_t a3 = Columns[3].mIntrin;

    // Multiply each lane (component) from the vector to each column in the matrix.
    result = vfmaq_laneq_f32(result, a0, vec.mIntrin, 0);
    result = vfmaq_laneq_f32(result, a1, vec.mIntrin, 1);
    result = vfmaq_laneq_f32(result, a2, vec.mIntrin, 2);
    result = vfmaq_laneq_f32(result, a3, vec.mIntrin, 3);

    return result;
}

FxVec4f FxMat4f::MultiplyVec4f(FxVec4f& vec) { return FxVec4f(MultiplyVec4f_Neon(vec)); }

FxMat4f FxMat4f::AsRotationX(float rad)
{
    // I doubt these functions are any more efficient than just plopping the sin's and cos's into
    // a matrix directly. But hey, gotta sharpen my neon skills somehow

    FxMat4f result;

    const float cr = cos(rad);
    const float sr = sin(rad);

    const float32x4_t zero = vdupq_n_f32(0);
    // {1, 0, 0, 0}
    result.Columns[0].mIntrin = vsetq_lane_f32(1, zero, 0);

    // {0, cos(x), sin(x), 0}
    const float32 c1_v[4] = { 0, cr, sr, 0 };
    result.Columns[1].mIntrin = vld1q_f32(c1_v);

    // {0, sin(x), cos(x), 0}
    const float32x4_t c2 = vrev64q_f32(result.Columns[1].mIntrin);

    // {0, sin(x)}, {cos(x), 0}
    const float32x2_t c2_lo = vget_low_f32(c2);
    const float32x2_t c2_hi = vget_high_f32(c2);

    // {0, -sin(x), cos(x), 0}
    result.Columns[2].mIntrin = vcombine_f32(vmul_f32(c2_hi, vdup_n_f32(-1)), c2_lo);

    // {0, 0, 0, 1}
    result.Columns[3].mIntrin = vsetq_lane_f32(1, zero, 3);

    /*
     CX = cos(x), SX = sin(x)

        0    1    2   3
     ----------------------
     [   1   0    0   0   ]
     [   0  CX  -SX   0   ]
     [   0  SX   CX   0   ]
     [   0   0    0   1   ]

     */

    return result;
}

FxMat4f FxMat4f::AsRotationY(float rad)
{
    FxMat4f result;

    const float cr = cos(rad);
    const float sr = sin(rad);

    // {sin(x), 0, cos(x), 0}
    float c2_v[4] = { sr, 0, cr, 0 };
    const float32x4_t c2 = vld1q_f32(c2_v);
    result.Columns[2].mIntrin = c2;

    // {0, 1, 0, 0}
    const float32x4_t c1 = vsetq_lane_f32(1, vdupq_n_f32(0), 1);
    result.Columns[1].mIntrin = c1;

    // {0, 0, 0, 1}
    result.Columns[3].mIntrin = vcombine_f32(vget_high_f32(c1), vget_low_f32(c1));

    // {cos(x), 0, -sin(x), 0}
    result.Columns[0].mIntrin = vcombine_f32(vget_high_f32(c2), vmul_f32(vget_low_f32(c2), vdup_n_f32(-1)));

    /*
     CX = cos(x), SX = sin(x)

        0     1    2   3
     -----------------------
     [   CX   0   SX   0   ]
     [    0   1    0   0   ]
     [  -SX   0   CX   0   ]
     [    0   0    0   1   ]

     */

    return result;
}


FxMat4f FxMat4f::AsRotationZ(float rad)
{
    FxMat4f result;

    const float cr = cos(rad);
    const float sr = sin(rad);

    float32x4_t zero = vdupq_n_f32(0);

    // {cos(x), sin(x), 0, 0}
    float c0_low_v[2] = { cr, sr };
    float32x4_t c0 = vcombine_f32(vld1_f32(c0_low_v), vget_low_f32(zero));
    result.Columns[0].mIntrin = c0;

    // {-1, 1}
    float32x2_t nmask = vset_lane_f32(-1, vdup_n_f32(1), 0);

    // {-sin(x), cos(x), 0, 0}
    result.Columns[1].mIntrin = vcombine_f32(vmul_f32(vrev64_f32(vget_low_f32(c0)), nmask), vget_high_f32(c0));

    // {0, 0, 1, 0}
    result.Columns[2].mIntrin = vsetq_lane_f32(1, zero, 2);

    // {0, 0, 0, 1}
    result.Columns[3].mIntrin = vsetq_lane_f32(1, zero, 3);

    /*
     CX = cos(x), SX = sin(x)

          0    1    2    3
     -------------------------
     [   CX  -SX    0    0   ]
     [   SX   CX    0    0   ]
     [    0    0    1    0   ]
     [    0    0    0    1   ]

     */

    return result;
}


#define MulVecFma(creg_, breg_)                                                                                        \
    creg_ = vfmaq_laneq_f32(creg_, a0, breg_, 0);                                                                      \
    creg_ = vfmaq_laneq_f32(creg_, a1, breg_, 1);                                                                      \
    creg_ = vfmaq_laneq_f32(creg_, a2, breg_, 2);                                                                      \
    creg_ = vfmaq_laneq_f32(creg_, a3, breg_, 3);


FxMat4f FxMat4f::operator*(const FxMat4f& other) const
{
    // Since we will be reusing a[0-3] a lot, this ensures the values
    // are loaded into the q registers.
    const float32x4_t a0 = other.Columns[0].mIntrin;
    const float32x4_t a1 = other.Columns[1].mIntrin;
    const float32x4_t a2 = other.Columns[2].mIntrin;
    const float32x4_t a3 = other.Columns[3].mIntrin;

    float32x4_t c0 = vmovq_n_f32(0);
    float32x4_t c1 = vmovq_n_f32(0);
    float32x4_t c2 = vmovq_n_f32(0);
    float32x4_t c3 = vmovq_n_f32(0);

    FxMat4f result;
    {
        const float32x4_t b0 = Columns[0].mIntrin;
        // Accumulate to c0
        MulVecFma(c0, b0);
        result.Columns[0].mIntrin = c0;
    }
    {
        const float32x4_t b1 = Columns[1].mIntrin;
        // Accumulate to c1
        MulVecFma(c1, b1);
        result.Columns[1].mIntrin = c1;
    }
    {
        const float32x4_t b2 = Columns[2].mIntrin;
        // Accumulate to c2
        MulVecFma(c2, b2);
        result.Columns[2].mIntrin = c2;
    }
    {
        const float32x4_t b3 = Columns[3].mIntrin;
        // Accumulate to c3
        MulVecFma(c3, b3);
        result.Columns[3].mIntrin = c3;
    }

    return result;
}

void FxMat4f::Rotate(FxVec3f rotation)
{
    /*
     *  0  -z   y
     *  z   0  -x
     * -y   x   0
     */
}

#include <string.h>

FxMat4f::FxMat4f(float data[4][4]) noexcept
{
    Columns[0].mIntrin = vld1q_f32(data[0]);
    Columns[1].mIntrin = vld1q_f32(data[1]);
    Columns[2].mIntrin = vld1q_f32(data[2]);
    Columns[3].mIntrin = vld1q_f32(data[3]);
}

// Implementation taken from linmath, https://github.com/datenwolf/linmath.h/blob/master/linmath.h
// TODO: NEON accelerated version of matrix inversion!
FxMat4f FxMat4f::Inverse()
{
    float M[4][4];
    for (int i = 0; i < 4; i++) {
        memcpy(&M[i], Columns[i].mData, sizeof(float) * 4);
    }

    float T[4][4];

    float s[6];
    float c[6];
    s[0] = M[0][0] * M[1][1] - M[1][0] * M[0][1];
    s[1] = M[0][0] * M[1][2] - M[1][0] * M[0][2];
    s[2] = M[0][0] * M[1][3] - M[1][0] * M[0][3];
    s[3] = M[0][1] * M[1][2] - M[1][1] * M[0][2];
    s[4] = M[0][1] * M[1][3] - M[1][1] * M[0][3];
    s[5] = M[0][2] * M[1][3] - M[1][2] * M[0][3];

    c[0] = M[2][0] * M[3][1] - M[3][0] * M[2][1];
    c[1] = M[2][0] * M[3][2] - M[3][0] * M[2][2];
    c[2] = M[2][0] * M[3][3] - M[3][0] * M[2][3];
    c[3] = M[2][1] * M[3][2] - M[3][1] * M[2][2];
    c[4] = M[2][1] * M[3][3] - M[3][1] * M[2][3];
    c[5] = M[2][2] * M[3][3] - M[3][2] * M[2][3];

    /* Assumes it is invertible */
    float idet = 1.0f / (s[0] * c[5] - s[1] * c[4] + s[2] * c[3] + s[3] * c[2] - s[4] * c[1] + s[5] * c[0]);

    T[0][0] = (M[1][1] * c[5] - M[1][2] * c[4] + M[1][3] * c[3]) * idet;
    T[0][1] = (-M[0][1] * c[5] + M[0][2] * c[4] - M[0][3] * c[3]) * idet;
    T[0][2] = (M[3][1] * s[5] - M[3][2] * s[4] + M[3][3] * s[3]) * idet;
    T[0][3] = (-M[2][1] * s[5] + M[2][2] * s[4] - M[2][3] * s[3]) * idet;

    T[1][0] = (-M[1][0] * c[5] + M[1][2] * c[2] - M[1][3] * c[1]) * idet;
    T[1][1] = (M[0][0] * c[5] - M[0][2] * c[2] + M[0][3] * c[1]) * idet;
    T[1][2] = (-M[3][0] * s[5] + M[3][2] * s[2] - M[3][3] * s[1]) * idet;
    T[1][3] = (M[2][0] * s[5] - M[2][2] * s[2] + M[2][3] * s[1]) * idet;

    T[2][0] = (M[1][0] * c[4] - M[1][1] * c[2] + M[1][3] * c[0]) * idet;
    T[2][1] = (-M[0][0] * c[4] + M[0][1] * c[2] - M[0][3] * c[0]) * idet;
    T[2][2] = (M[3][0] * s[4] - M[3][1] * s[2] + M[3][3] * s[0]) * idet;
    T[2][3] = (-M[2][0] * s[4] + M[2][1] * s[2] - M[2][3] * s[0]) * idet;

    T[3][0] = (-M[1][0] * c[3] + M[1][1] * c[1] - M[1][2] * c[0]) * idet;
    T[3][1] = (M[0][0] * c[3] - M[0][1] * c[1] + M[0][2] * c[0]) * idet;
    T[3][2] = (-M[3][0] * s[3] + M[3][1] * s[1] - M[3][2] * s[0]) * idet;
    T[3][3] = (M[2][0] * s[3] - M[2][1] * s[1] + M[2][2] * s[0]) * idet;

    return FxMat4f(T);
}

FxMat4f FxMat4f::GetWithoutTranslation() const
{
    FxMat4f mat = *this;
    mat.Columns[3].mIntrin = vsetq_lane_f32(1, vdupq_n_f32(0), 3);

    return mat;
}

void FxMat4f::LookAt(FxVec3f position, FxVec3f target, FxVec3f upvec)
{
    const FxVec3f forward = (target - position).Normalize();
    const FxVec3f right = upvec.Cross(forward).Normalize();
    const FxVec3f up = forward.Cross(right);

    Columns[0].Load4(right.X, up.X, forward.X, 0.0f);
    Columns[1].Load4(right.Y, up.Y, forward.Y, 0.0f);
    Columns[2].Load4(right.Z, up.Z, forward.Z, 0.0f);
    Columns[3].Load4(-right.Dot(position), -up.Dot(position), -forward.Dot(position), 1.0f);

    // Columns[0].Load4(right.GetX(),   right.GetY(),   right.GetZ(),   -right.Dot(position));
    // Columns[1].Load4(up.GetX(),      up.GetY(),      up.GetZ(),      -up.Dot(position));
    // Columns[2].Load4(forward.GetX(), forward.GetY(), forward.GetZ(), -forward.Dot(position));
    // Columns[3].Load4(0,              0,              0,              1.0f);
}

// Mat4f Mat4f::Multiply(Mat4f &other)
// {
//     Mat4f result;

//     result.Data[0].mIntrin = MultiplyVec4f_Neon(other.Data[0]);
//     result.Data[1].mIntrin = MultiplyVec4f_Neon(other.Data[1]);
//     result.Data[2].mIntrin = MultiplyVec4f_Neon(other.Data[2]);
//     result.Data[3].mIntrin = MultiplyVec4f_Neon(other.Data[3]);

//     return result;
// }
//
//


#endif
