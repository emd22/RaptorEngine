
#ifdef FX_USE_NEON

#include <Math/Mat4.hpp>

float32x4_t Mat4f::MultiplyVec4f_Neon(Vec4f &vec)
{
    float32x4_t result = vmovq_n_f32(0);

    float32x4_t a0 = Data[0].mIntrin;
    float32x4_t a1 = Data[1].mIntrin;
    float32x4_t a2 = Data[2].mIntrin;
    float32x4_t a3 = Data[3].mIntrin;

    // Multiply each lane (component) from the vector to each column in the matrix.
    result = vfmaq_laneq_f32(result, a0, vec.mIntrin, 0);
    result = vfmaq_laneq_f32(result, a1, vec.mIntrin, 1);
    result = vfmaq_laneq_f32(result, a2, vec.mIntrin, 2);
    result = vfmaq_laneq_f32(result, a3, vec.mIntrin, 3);

    return result;
}

Vec4f Mat4f::MultiplyVec4f(Vec4f &vec)
{
    return Vec4f(MultiplyVec4f_Neon(vec));
}

#define MAT_COL_FMA(index_, bvalue_) \
    result.Data[index_].mIntrin = vfmaq_laneq_f32(result.Data[index_].mIntrin, a0, bvalue_, 0); \
    result.Data[index_].mIntrin = vfmaq_laneq_f32(result.Data[index_].mIntrin, a1, bvalue_, 1); \
    result.Data[index_].mIntrin = vfmaq_laneq_f32(result.Data[index_].mIntrin, a2, bvalue_, 2); \
    result.Data[index_].mIntrin = vfmaq_laneq_f32(result.Data[index_].mIntrin, a3, bvalue_, 3);

Mat4f Mat4f::Multiply(Mat4f &other)
{
    // Since we will be reusing a[0-3] a lot, this ensures the values
    // are loaded into the q registers.
    const float32x4_t a0 = Data[0].mIntrin;
    const float32x4_t a1 = Data[1].mIntrin;
    const float32x4_t a2 = Data[2].mIntrin;
    const float32x4_t a3 = Data[3].mIntrin;

    Mat4f result;

    // Clear results as we will be accumulating via FMA's
    result.Data[0].mIntrin = vmovq_n_f32(0);
    result.Data[1].mIntrin = vmovq_n_f32(0);
    result.Data[2].mIntrin = vmovq_n_f32(0);
    result.Data[3].mIntrin = vmovq_n_f32(0);

    // For each column:
    // Load column from other matrix.
    // MAT_COL_FMA does 4 FMA operations:
    // -> multiply a0 by b_n's X value. Accumulate to result.Data[n].mIntrin.
    // -> multiply a1 by b_n's Y value. Accumulate to result.Data[n].mIntrin.
    // -> multiply a2 by b_n's Z value. Accumulate to result.Data[n].mIntrin.
    // -> multiply a3 by b_n's W value. Accumulate to result.Data[n].mIntrin.

    float32x4_t b0 = other.Data[0].mIntrin;
    MAT_COL_FMA(0, b0);

    float32x4_t b1 = other.Data[1].mIntrin;
    MAT_COL_FMA(1, b1);

    float32x4_t b2 = other.Data[2].mIntrin;
    MAT_COL_FMA(2, b2);

    float32x4_t b3 = other.Data[3].mIntrin;
    MAT_COL_FMA(3, b3);

    return result;
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

#endif
