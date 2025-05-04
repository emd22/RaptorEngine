
#include <arm_neon.h>
#ifdef FX_USE_NEON

#include <Math/Mat4.hpp>

const Mat4f Mat4f::Identity = Mat4f(
    (float32 [16]){
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    }
);

float32x4_t Mat4f::MultiplyVec4f_Neon(Vec4f &vec)
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

Vec4f Mat4f::MultiplyVec4f(Vec4f &vec)
{
    return Vec4f(MultiplyVec4f_Neon(vec));
}


#define MulVecFma(creg_, breg_) \
    creg_ = vfmaq_laneq_f32(creg_, a0, breg_, 0); \
    creg_ = vfmaq_laneq_f32(creg_, a1, breg_, 1); \
    creg_ = vfmaq_laneq_f32(creg_, a2, breg_, 2); \
    creg_ = vfmaq_laneq_f32(creg_, a3, breg_, 3);


Mat4f Mat4f::operator *(const Mat4f &other)
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

    Mat4f result;
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

void Mat4f::LookAt(Vec3f position, Vec3f target, Vec3f upvec)
{
    const Vec3f forward = (target - position).Normalize();
    const Vec3f right = upvec.Cross(forward).Normalize();
    const Vec3f up = forward.Cross(right);

    Columns[0].Load4(right.GetX(), up.GetX(), forward.GetX(), 0.0f);
    Columns[1].Load4(right.GetY(), up.GetY(), forward.GetY(), 0.0f);
    Columns[2].Load4(right.GetZ(), up.GetZ(), forward.GetZ(), 0.0f);
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

#endif
