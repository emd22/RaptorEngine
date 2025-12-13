#include <Core/FxDefines.hpp>

#ifdef FX_USE_AVX

#include <immintrin.h>

#include <Math/Mat4.hpp>
#include <Math/FxAVXUtil.hpp>

#include <Math/FxQuat.hpp>
#include <Core/FxTypes.hpp>

static const float32 scIdentityData[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

const FxMat4f FxMat4f::Identity = FxMat4f(scIdentityData);

__m128 FxMat4f::MultiplyVec4f_SSE(const FxVec4f& vec)
{
    __m128 result = _mm_setzero_ps();

    __m128 a0 = Columns[0].mIntrin;
    __m128 a1 = Columns[1].mIntrin;
    __m128 a2 = Columns[2].mIntrin;
    __m128 a3 = Columns[3].mIntrin;

    // {X, Y, Z, W}
    /*
     1*X,  2*Y,  3*Z,  4*W,
     5*X,  6*Y,  7*Z,  8*W,
     9*X, 10*Y, 11*Z, 12*W,
    13*X, 14*Y, 15*Z, 16*W,
    
    */

    // Multiply each lane (component) from the vector to each column in the matrix.

    /*result = _mm_mul_ps(result, a0);
    result = _mm_add_ps(result, FxSSE::Permute4<FxShuffle_AX, FxShuffle_AX, FxShuffle_AX, FxShuffle_AX>(vec.mIntrin));

    result = _mm_mul_ps(result, a1);
    result = _mm_add_ps(result, FxSSE::Permute4<FxShuffle_AY, FxShuffle_AY, FxShuffle_AY, FxShuffle_AY>(vec.mIntrin));

    result = _mm_mul_ps(result, a2);
    result = _mm_add_ps(result, FxSSE::Permute4<FxShuffle_AZ, FxShuffle_AZ, FxShuffle_AZ, FxShuffle_AZ>(vec.mIntrin));

    result = _mm_mul_ps(result, a3);
    result = _mm_add_ps(result, FxSSE::Permute4<FxShuffle_AW, FxShuffle_AW, FxShuffle_AW, FxShuffle_AW>(vec.mIntrin));*/

    //result = vfmaq_laneq_f32(result, a0, vec.mIntrin, 0);
    //result = vfmaq_laneq_f32(result, a1, vec.mIntrin, 1);
    //result = vfmaq_laneq_f32(result, a2, vec.mIntrin, 2);
    //result = vfmaq_laneq_f32(result, a3, vec.mIntrin, 3);

    return result;
}

FxVec4f FxMat4f::MultiplyVec4f(FxVec4f& vec) { return FxVec4f(MultiplyVec4f_SSE(vec)); }

FxMat4f FxMat4f::AsRotation(const FxQuat& quat)
{
    float x = quat.GetX();
    float y = quat.GetY();
    float z = quat.GetZ();
    float w = quat.GetW();

    const float tx = x * 2.0f;
    const float ty = y * 2.0f;
    const float tz = z * 2.0f;

    float xx = tx * x;
    float yy = ty * y;
    float zz = tz * z;

    float xy = tx * y;
    float xz = tx * z;
    float xw = tx * w;

    float yz = ty * z;
    float yw = ty * w;
    float zw = tz * w;

    return FxMat4f(FxVec4f((1.0f - yy) - zz, xy + zw, xz - yw, 0.0f), /* */
                   FxVec4f(xy - zw, (1.0f - zz) - xx, yz + xw, 0.0f), /* */
                   FxVec4f(xz + yw, yz - xw, (1.0f - xx) - yy, 0.0f), /* */
                   FxVec4f(0.0f, 0.0f, 0.0f, 1.0f));
}


FxMat4f FxMat4f::AsRotationX(float rad)
{
    // I doubt these functions are any more efficient than just plopping the sin's and cos's into
    // a matrix directly. But hey, gotta sharpen my neon skills somehow

    FxMat4f result;

    const float cr = cos(rad);
    const float sr = sin(rad);

    // {1, 0, 0, 0}
    const __m128 c0 = _mm_set_ss(1.0f);
    result.Columns[0].mIntrin = c0;

    // {0, cos(x), sin(x), 0}
    const float32 c1_v[4] = { 0, cr, sr, 0 };

    const __m128 c1 = _mm_load_ps(c1_v);
    result.Columns[1].mIntrin = c1;

    // {0, sin(x), cos(x), 0}
    //const float32x4_t c2 = vrev64q_f32(result.Columns[1].mIntrin);



    //// {0, sin(x)}, {cos(x), 0}
    //const float32x2_t c2_lo = vget_low_f32(c2);
    //const float32x2_t c2_hi = vget_high_f32(c2);

    //// {0, -sin(x), cos(x), 0}
    //result.Columns[2].mIntrin = vcombine_f32(vmul_f32(c2_hi, vdup_n_f32(-1)), c2_lo);

    __m128 c2 = FxSSE::Permute4<FxShuffle_AX, FxShuffle_AZ, FxShuffle_AY, FxShuffle_AW>(c1);
    c2 = FxSSE::SetSign<1, -1, 1, 1>(c2);
    result.Columns[2].mIntrin = c2;

    // {0, 0, 0, 1}
    result.Columns[3].mIntrin = FxSSE::Permute4<FxShuffle_AW, FxShuffle_AY, FxShuffle_AZ, FxShuffle_AX>(c0);

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

    const __m128 one_zzz = _mm_set_ss(1.0f);

    // {sin(x), 0, cos(x), 0}
    float c2_v[4] = { sr, 0, cr, 0 };
    const __m128 c2 = _mm_load_ps(c2_v);
    result.Columns[2].mIntrin = c2;

    // {0, 1, 0, 0}
    result.Columns[1].mIntrin =  FxSSE::Permute4<FxShuffle_AY, FxShuffle_AX, FxShuffle_AZ, FxShuffle_AW>(one_zzz);
    // {0, 0, 0, 1}
    result.Columns[3].mIntrin = FxSSE::Permute4<FxShuffle_AW, FxShuffle_AY, FxShuffle_AZ, FxShuffle_AX>(one_zzz);
    
    // {cos(x), 0, -sin(x), 0}

    const __m128 c0 = FxSSE::Permute4<FxShuffle_AZ, FxShuffle_AY, FxShuffle_AX, FxShuffle_AW>(c2);
    result.Columns[0].mIntrin = FxSSE::SetSign<1, 1, -1, 1>(c0);

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

    //float32x4_t zero = vdupq_n_f32(0);

    // {cos(x), sin(x), 0, 0}
    //float c0_low_v[2] = { cr, sr };
    //float32x4_t c0 = vcombine_f32(vld1_f32(c0_low_v), vget_low_f32(zero));
    //result.Columns[0].mIntrin = c0;

    //// {-1, 1}
    //float32x2_t nmask = vset_lane_f32(-1, vdup_n_f32(1), 0);

    //// {-sin(x), cos(x), 0, 0}
    //result.Columns[1].mIntrin = vcombine_f32(vmul_f32(vrev64_f32(vget_low_f32(c0)), nmask), vget_high_f32(c0));

    //// {0, 0, 1, 0}
    //result.Columns[2].mIntrin = vsetq_lane_f32(1, zero, 2);

    //// {0, 0, 0, 1}
    //result.Columns[3].mIntrin = vsetq_lane_f32(1, zero, 3);

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
    const __m128 a0 = other.Columns[0].mIntrin;
    const __m128 a1 = other.Columns[1].mIntrin;
    const __m128 a2 = other.Columns[2].mIntrin;
    const __m128 a3 = other.Columns[3].mIntrin;


    /*
     Col 0    1    2    3
     -------------------------
     [   1    5    9   13   ]
     [   2    6   10   14   ]
     [   3    7   11   15   ]
     [   4    8   12   16   ]
        ^^^^^^^   ^^^^^^^ 
        ahalf0    ahalf1
    */

    // Load the columns of this matrix into two vectors.

    // Pack both columns into one (256bit) vector.
    __m256 ahalf0 = _mm256_castps128_ps256(Columns[0].mIntrin);
    ahalf0 = _mm256_insertf128_ps(ahalf0, Columns[1].mIntrin, 1);

    // Second half of the matrix, columns 2 and 3.
    __m256 ahalf1 = _mm256_castps128_ps256(Columns[2].mIntrin);
    ahalf1 = _mm256_insertf128_ps(ahalf1, Columns[3].mIntrin, 1);

    // Load the second matrix

    __m256 bhalf0 = _mm256_castps128_ps256(other.Columns[0].mIntrin);
    bhalf0 = _mm256_insertf128_ps(bhalf0, other.Columns[1].mIntrin, 1);

    __m256 bhalf1 = _mm256_castps128_ps256(other.Columns[2].mIntrin);
    bhalf1 = _mm256_insertf128_ps(bhalf1, other.Columns[3].mIntrin, 1);








    __m128 c0 = _mm_setzero_ps();
    __m128 c1 = _mm_setzero_ps();
    __m128 c2 = _mm_setzero_ps();
    __m128 c3 = _mm_setzero_ps();

    FxMat4f result;
    {
        const __m128 b0 = Columns[0].mIntrin;
        // Accumulate to c0
        MulVecFma(c0, b0);
        result.Columns[0].mIntrin = c0;
    }
    {
        const __m128 b1 = Columns[1].mIntrin;
        // Accumulate to c1
        MulVecFma(c1, b1);
        result.Columns[1].mIntrin = c1;
    }
    {
        const __m128 b2 = Columns[2].mIntrin;
        // Accumulate to c2
        MulVecFma(c2, b2);
        result.Columns[2].mIntrin = c2;
    }
    {
        const __m128 b3 = Columns[3].mIntrin;
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
    Columns[0].mIntrin = _mm_load_ps(data[0]);
    Columns[1].mIntrin = _mm_load_ps(data[1]);
    Columns[2].mIntrin = _mm_load_ps(data[2]);
    Columns[3].mIntrin = _mm_load_ps(data[3]);
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

FxMat4f FxMat4f::Transposed()
{
    float32x4x2_t tmp1 = vzipq_f32(Columns[0].mIntrin, Columns[2].mIntrin);
    float32x4x2_t tmp2 = vzipq_f32(Columns[1].mIntrin, Columns[3].mIntrin);
    float32x4x2_t tmp3 = vzipq_f32(tmp1.val[0], tmp2.val[0]);
    float32x4x2_t tmp4 = vzipq_f32(tmp1.val[1], tmp2.val[1]);

    FxMat4f result;
    result.Columns[0].mIntrin = tmp3.val[0];
    result.Columns[1].mIntrin = tmp3.val[1];
    result.Columns[2].mIntrin = tmp4.val[0];
    result.Columns[3].mIntrin = tmp4.val[1];
    return result;
}

FxMat4f FxMat4f::TransposeMat3()
{
    float32x4x2_t tmp1 = vzipq_f32(Columns[0].mIntrin, Columns[2].mIntrin);
    float32x4x2_t tmp2 = vzipq_f32(Columns[1].mIntrin, vdupq_n_f32(0));
    float32x4x2_t tmp3 = vzipq_f32(tmp1.val[0], tmp2.val[0]);
    float32x4x2_t tmp4 = vzipq_f32(tmp1.val[1], tmp2.val[1]);

    FxMat4f result;
    result.Columns[0].mIntrin = tmp3.val[0];
    result.Columns[1].mIntrin = tmp3.val[1];
    result.Columns[2].mIntrin = tmp4.val[0];

    return result;
}

void FxMat4f::CopyAsMat3To(float* dest) const { memcpy(dest, RawData, sizeof(float32) * 12); }

void FxMat4f::LookAt(FxVec3f position, FxVec3f target, FxVec3f upvec)
{
    FxVec3f forward = (target - position).Normalize();
    const FxVec3f right = forward.Cross(upvec).Normalize();
    const FxVec3f up = right.Cross(forward);

    Columns[0].Load4(right.X, up.X, forward.X, 0.0f);
    Columns[1].Load4(right.Y, up.Y, forward.Y, 0.0f);
    Columns[2].Load4(right.Z, up.Z, forward.Z, 0.0f);
    Columns[3].Load4(-position.Dot(right), -position.Dot(up), -position.Dot(forward), 1.0f);
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





#endif

