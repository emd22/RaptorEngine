#include <Core/Defines.hpp>

#ifdef FX_USE_AVX

#include <Core/Panic.hpp>
#include <Core/Types.hpp>
#include <Math/Mat4.hpp>
#include <Math/Quat.hpp>
#include <Math/SSE.hpp>
#include <Math/SSEUtil.hpp>

namespace fx {

static const float32 scIdentityData[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                            0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

const Mat4f Mat4f::sIdentity = Mat4f(scIdentityData);

__m128 Mat4f::MultiplyVec4f_SSE(const Vec4f& vec)
{
    __m128 v = vec.mIntrin;
    __m128 v0 = SSE::Permute4<SSE::Shuffle_AX>(v);
    __m128 v1 = SSE::Permute4<SSE::Shuffle_AY>(v);
    __m128 v2 = SSE::Permute4<SSE::Shuffle_AZ>(v);
    __m128 v3 = SSE::Permute4<SSE::Shuffle_AW>(v);

    __m128 result = _mm_mul_ps(Columns[0].mIntrin, v0);
    result = _mm_fmadd_ps(Columns[1].mIntrin, v1, result);
    result = _mm_fmadd_ps(Columns[2].mIntrin, v2, result);
    result = _mm_fmadd_ps(Columns[3].mIntrin, v3, result);

    return result;
}

Vec4f Mat4f::MultiplyVec4f(Vec4f& vec) { return Vec4f(MultiplyVec4f_SSE(vec)); }

Mat4f Mat4f::AsRotation(const Quat& quat)
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

    return Mat4f(Vec4f((1.0f - yy) - zz, xy + zw, xz - yw, 0.0f), /* */
                 Vec4f(xy - zw, (1.0f - zz) - xx, yz + xw, 0.0f), /* */
                 Vec4f(xz + yw, yz - xw, (1.0f - xx) - yy, 0.0f), /* */
                 Vec4f(0.0f, 0.0f, 0.0f, 1.0f));
}

void Mat4f::LoadPerspectiveMatrix(float32 yfov, float32 aspect_ratio, float32 near_plane, float32 far_plane)
{
    LoadIdentity();

    const float32 height = 1.0f / tan(yfov * 0.5f);
    const float32 width = height / aspect_ratio;

    const float32 depth_range = near_plane / (far_plane - near_plane);

    /*
    Column 0    1    2    3
    ------------------------
           Sa,   0,   0,   0,
           0,  -Sv,   0,   0,
           0,    0,   a,   b,
           0,    0,  -1,   0
    */

    Columns[0].X = (width);
    Columns[1].Y = (-height);

    Columns[2].Z = (-depth_range);
    Columns[2].W = (1.0f);

    Columns[3].Z = (far_plane * depth_range);
    Columns[3].W = (0);
}


void Mat4f::LoadOrthographicMatrix(float32 width, float32 height, float32 near_plane, float32 far_plane)
{
    Assert(near_plane > 0.0f && far_plane > 0.0f);

    const float32 depth_range = 1.0f / (far_plane - near_plane);

    Columns[0].Set(2.0f / width, 0.0f, 0.0f, 0.0f);
    Columns[1].Set(0.0f, -2.0f / height, 0.0f, 0.0f);
    Columns[2].Set(0.0f, 0.0f, depth_range, 0.0f);
    Columns[3].Set(0.0f, 0.0f, -depth_range * near_plane, 1.0f);
}


Mat4f Mat4f::AsRotationX(float rad)
{
    // I doubt these functions are any more efficient than just plopping the sin's and cos's into
    // a matrix directly. But hey, gotta sharpen my neon skills somehow

    Mat4f result;

    const float cr = cos(rad);
    const float sr = sin(rad);

    // {1, 0, 0, 0}
    const __m128 c0 = _mm_set_ss(1.0f);
    result.Columns[0].mIntrin = c0;

    // {0, cos(x), sin(x), 0}
    const float32 c1_v alignas(16)[4] = { 0, cr, sr, 0 };

    const __m128 c1 = _mm_load_ps(c1_v);
    result.Columns[1].mIntrin = c1;

    // {0, sin(x), cos(x), 0}
    // const float32x4_t c2 = vrev64q_f32(result.Columns[1].mIntrin);


    //// {0, sin(x)}, {cos(x), 0}
    // const float32x2_t c2_lo = vget_low_f32(c2);
    // const float32x2_t c2_hi = vget_high_f32(c2);

    //// {0, -sin(x), cos(x), 0}
    // result.Columns[2].mIntrin = vcombine_f32(vmul_f32(c2_hi, vdup_n_f32(-1)), c2_lo);

    __m128 c2 = SSE::Permute4<SSE::Shuffle_AX, SSE::Shuffle_AZ, SSE::Shuffle_AY, SSE::Shuffle_AW>(c1);
    c2 = SSE::FlipSigns<1, -1, 1, 1>(c2);
    result.Columns[2].mIntrin = c2;

    // {0, 0, 0, 1}
    result.Columns[3].mIntrin = SSE::Permute4<SSE::Shuffle_AW, SSE::Shuffle_AY, SSE::Shuffle_AZ, SSE::Shuffle_AX>(c0);

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

Mat4f Mat4f::AsRotationY(float rad)
{
    Mat4f result;

    const float cr = cos(rad);
    const float sr = sin(rad);

    const __m128 one_zzz = _mm_set_ss(1.0f);

    // {sin(x), 0, cos(x), 0}
    float c2_v alignas(16)[4] = { sr, 0, cr, 0 };
    const __m128 c2 = _mm_load_ps(c2_v);
    result.Columns[2].mIntrin = c2;

    // {0, 1, 0, 0}
    result.Columns[1].mIntrin = SSE::Permute4<SSE::Shuffle_AY, SSE::Shuffle_AX, SSE::Shuffle_AZ, SSE::Shuffle_AW>(
        one_zzz);
    // {0, 0, 0, 1}
    result.Columns[3].mIntrin = SSE::Permute4<SSE::Shuffle_AW, SSE::Shuffle_AY, SSE::Shuffle_AZ, SSE::Shuffle_AX>(
        one_zzz);

    // {cos(x), 0, -sin(x), 0}

    const __m128 c0 = SSE::Permute4<SSE::Shuffle_AZ, SSE::Shuffle_AY, SSE::Shuffle_AX, SSE::Shuffle_AW>(c2);
    result.Columns[0].mIntrin = SSE::FlipSigns<1, 1, -1, 1>(c0);

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


Mat4f Mat4f::AsRotationZ(float rad)
{
    Mat4f result;

    const float cr = cos(rad);
    const float sr = sin(rad);

    // float32x4_t zero = vdupq_n_f32(0);

    // {cos(x), sin(x), 0, 0}
    // float c0_low_v[2] = { cr, sr };
    // float32x4_t c0 = vcombine_f32(vld1_f32(c0_low_v), vget_low_f32(zero));
    // result.Columns[0].mIntrin = c0;

    //// {-1, 1}
    // float32x2_t nmask = vset_lane_f32(-1, vdup_n_f32(1), 0);

    //// {-sin(x), cos(x), 0, 0}
    // result.Columns[1].mIntrin = vcombine_f32(vmul_f32(vrev64_f32(vget_low_f32(c0)), nmask), vget_high_f32(c0));

    //// {0, 0, 1, 0}
    // result.Columns[2].mIntrin = vsetq_lane_f32(1, zero, 2);

    //// {0, 0, 0, 1}
    // result.Columns[3].mIntrin = vsetq_lane_f32(1, zero, 3);

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


// AVX implementation adapted from DirectXMath:
// https://github.com/microsoft/DirectXMath/blob/main/Inc/DirectXMathMatrix.inl
Mat4f Mat4f::operator*(const Mat4f& other) const
{
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


    __m256 temp0_l, temp0_r;
    __m256 temp1;

    // Splat A1, A5, A9, A13 down to rest of column.
    temp0_l = _mm256_shuffle_ps(ahalf0, ahalf0, _MM_SHUFFLE(0, 0, 0, 0));
    temp0_r = _mm256_shuffle_ps(ahalf1, ahalf1, _MM_SHUFFLE(0, 0, 0, 0));

    // Permute to include only the low 128 bits ([Lo][Hi] -> [Lo][Lo])
    temp1 = _mm256_permute2f128_ps(bhalf0, bhalf0, 0x00);
    __m256 r0_l = _mm256_mul_ps(temp0_l, temp1);
    __m256 r0_r = _mm256_mul_ps(temp0_r, temp1);

    temp0_l = _mm256_shuffle_ps(ahalf0, ahalf0, _MM_SHUFFLE(1, 1, 1, 1));
    temp0_r = _mm256_shuffle_ps(ahalf1, ahalf1, _MM_SHUFFLE(1, 1, 1, 1));

    // Reverse the order of the 128 bit components ([Lo][Hi] -> [Hi][Lo])
    temp1 = _mm256_permute2f128_ps(bhalf0, bhalf0, 0x11);
    __m256 r1_l = _mm256_fmadd_ps(temp0_l, temp1, r0_l);
    __m256 r1_r = _mm256_fmadd_ps(temp0_r, temp1, r0_r);


    temp0_l = _mm256_shuffle_ps(ahalf0, ahalf0, _MM_SHUFFLE(2, 2, 2, 2));
    temp0_r = _mm256_shuffle_ps(ahalf1, ahalf1, _MM_SHUFFLE(2, 2, 2, 2));
    __m256 b1 = _mm256_permute2f128_ps(bhalf1, bhalf1, 0x00);
    __m256 r2_l = _mm256_mul_ps(temp0_l, b1);
    __m256 r2_r = _mm256_mul_ps(temp0_r, b1);

    temp0_l = _mm256_shuffle_ps(ahalf0, ahalf0, _MM_SHUFFLE(3, 3, 3, 3));
    temp0_r = _mm256_shuffle_ps(ahalf1, ahalf1, _MM_SHUFFLE(3, 3, 3, 3));
    b1 = _mm256_permute2f128_ps(bhalf1, bhalf1, 0x11);
    __m256 c6 = _mm256_fmadd_ps(temp0_l, b1, r2_l);
    __m256 c7 = _mm256_fmadd_ps(temp0_r, b1, r2_r);

    temp0_l = _mm256_add_ps(r1_l, c6);
    temp0_r = _mm256_add_ps(r1_r, c7);

    Mat4f result;

    // Convert back to 128 bit vectors and return the matrix
    result.Columns[0].mIntrin = _mm256_castps256_ps128(temp0_l);
    result.Columns[1].mIntrin = _mm256_extractf128_ps(temp0_l, 1);
    result.Columns[2].mIntrin = _mm256_castps256_ps128(temp0_r);
    result.Columns[3].mIntrin = _mm256_extractf128_ps(temp0_r, 1);

    return result;
}

void Mat4f::Rotate(Vec3f rotation)
{
    /*
     *  0  -z   y
     *  z   0  -x
     * -y   x   0
     */
}

#include <string.h>

Mat4f::Mat4f(float data[4][4]) noexcept
{
    // Its likely that this 64 byte buffer is aligned on a 32 byte boundary, but it cannot be assumed
    // given aligned laods can throw a GP exception.
    Columns[0].mIntrin = _mm_loadu_ps(data[0]);
    Columns[1].mIntrin = _mm_loadu_ps(data[1]);
    Columns[2].mIntrin = _mm_loadu_ps(data[2]);
    Columns[3].mIntrin = _mm_loadu_ps(data[3]);
}

// Implementation taken from linmath, https://github.com/datenwolf/linmath.h/blob/master/linmath.h
// TODO: NEON accelerated version of matrix inversion!
Mat4f Mat4f::Inverse() const
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

    return Mat4f(T);
}

Mat4f Mat4f::GetWithoutTranslation() const
{
    Mat4f mat = *this;

    __m128 single1 = _mm_set_ss(1.0f);

    // Set column 3 to identity (Z component to 1.0f)
    mat.Columns[3].mIntrin = _mm_shuffle_ps(single1, single1, _MM_SHUFFLE(1, 0, 1, 1));

    return mat;
}

Mat4f Mat4f::Transposed() const
{
    /*
        [ A E I M ]
        [ B F J N ]
        [ C G K O ]
        [ D H L P ]

        Transposed:

        [ A B C D ]
        [ E F G H ]
        [ I J K L ]
        [ M N O P ]
    */

    // Build a AVX vector for each half of the matrix
    __m256 ahalf0 = _mm256_castps128_ps256(Columns[0].mIntrin);
    ahalf0 = _mm256_insertf128_ps(ahalf0, Columns[1].mIntrin, 1);
    __m256 ahalf1 = _mm256_castps128_ps256(Columns[2].mIntrin);
    ahalf1 = _mm256_insertf128_ps(ahalf1, Columns[3].mIntrin, 1);

    // Unpack (and interleave) the low 64 bits for each half of the matrix.
    // E.g the low 64 bit chunks {A, E} and {B, F} will give us {A, B, E, F}.
    __m256 temp0 = _mm256_unpacklo_ps(ahalf0, ahalf1);

    // Unpack and interleave the high 64 bits of each half of the matrix.
    // E.g the high 64 bit chunks {I, M} and {J, N} will give us {I, J, M, N}.
    __m256 temp1 = _mm256_unpackhi_ps(ahalf0, ahalf1);

    ahalf0 = _mm256_permute2f128_ps(temp0, temp1, 0x20);
    ahalf1 = _mm256_permute2f128_ps(temp0, temp1, 0x31);

    temp0 = _mm256_unpacklo_ps(ahalf0, ahalf1);
    temp1 = _mm256_unpackhi_ps(ahalf0, ahalf1);

    ahalf0 = _mm256_permute2f128_ps(temp0, temp1, 0x20);
    ahalf1 = _mm256_permute2f128_ps(temp0, temp1, 0x31);

    // Unpack the columns from the AVX vectors
    Mat4f result;
    result.Columns[0].mIntrin = _mm256_castps256_ps128(ahalf0);
    result.Columns[1].mIntrin = _mm256_extractf128_ps(ahalf0, 1);
    result.Columns[2].mIntrin = _mm256_castps256_ps128(ahalf1);
    result.Columns[3].mIntrin = _mm256_extractf128_ps(ahalf1, 1);

    return result;
}

Mat4f Mat4f::TransposeMat3() { return Transposed(); }

void Mat4f::CopyAsMat3To(float* dest) const { memcpy(dest, RawData, sizeof(float32) * 12); }

void Mat4f::LookAt(Vec3f eye, Vec3f target, Vec3f upvec)
{
    Vec3f forward = (target - eye);
    forward.NormalizeIP();

    Vec3f right = upvec.Cross(forward);
    right.NormalizeIP();

    const Vec3f up = forward.Cross(right);

    Columns[0].Set(right.X, up.X, forward.X, 0.0f);
    Columns[1].Set(right.Y, up.Y, forward.Y, 0.0f);
    Columns[2].Set(right.Z, up.Z, forward.Z, 0.0f);
    Columns[3].Set(-eye.Dot(right), -eye.Dot(up), -eye.Dot(forward), 1.0f);
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
};
