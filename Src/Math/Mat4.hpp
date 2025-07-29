#pragma once

#include "Vec4.hpp"
#include "Vec3.hpp"

#ifdef FX_USE_NEON
#include <arm_neon.h>
#endif

/**
 * @brief A single-precision 4x4 matrix class using SIMD instructions.
 *
 * This class represents a 4x4 matrix represented using _Column-Major_ order to match
 * the layout of MSL, GLSL, and _modern_ HLSL.
 */
class alignas(16) FxMat4f
{
public:
    FxMat4f() noexcept
    {
        Columns[0].Load1(0);
        Columns[1].Load1(0);
        Columns[2].Load1(0);
        Columns[3].Load1(0);
    }

    FxMat4f(float data[16]) noexcept
    {
        Columns[0].Load4Ptr(data);
        Columns[1].Load4Ptr(data + 4);
        Columns[2].Load4Ptr(data + 8);
        Columns[3].Load4Ptr(data + 12);
    }

    FxMat4f(float data[4][4]) noexcept;

    static FxMat4f FromRows(float data[16])
    {
        return FxMat4f(
            Vec4f(data[0], data[4], data[8], data[12]),
            Vec4f(data[1], data[5], data[9], data[13]),
            Vec4f(data[2], data[6], data[10], data[14]),
            Vec4f(data[3], data[7], data[11], data[15])
        );
    }

    static const FxMat4f Identity;

    static FxMat4f AsTranslation(FxVec3f position)
    {
        return FxMat4f(
            (float32 [16]){
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                position.X, position.Y, position.Z, 1
            }
        );
    }

    static FxMat4f AsScale(FxVec3f scale)
    {
        return FxMat4f(
            (float32 [16]){
                scale.X, 0, 0, 0,
                0, scale.Y, 0, 0,
                0, 0, scale.Z, 0,
                0, 0, 0, 1
            }
        );
    }

    void Rotate(FxVec3f rotation);

    void LookAt(FxVec3f position, FxVec3f target, FxVec3f up);

    FxMat4f(float scalar) noexcept
    {
        Columns[0].Load1(scalar);
        Columns[1].Load1(scalar);
        Columns[2].Load1(scalar);
        Columns[3].Load1(scalar);
    }

    FxMat4f(Vec4f c0, Vec4f c1, Vec4f c2, Vec4f c3) noexcept
    {
        Columns[0] = c0;
        Columns[1] = c1;
        Columns[2] = c2;
        Columns[3] = c3;
    }

    FxMat4f(Vec4f columns[4]) noexcept
    {
        Columns[0] = columns[0];
        Columns[1] = columns[1];
        Columns[2] = columns[2];
        Columns[3] = columns[3];
    }

    inline void LoadIdentity()
    {
        Columns[0].Load4(1.0f, 0.0f, 0.0f, 0.0f);
        Columns[1].Load4(0.0f, 1.0f, 0.0f, 0.0f);
        Columns[2].Load4(0.0f, 0.0f, 1.0f, 0.0f);
        Columns[3].Load4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    void Set(float data[16])
    {
        Columns[0].Load4Ptr(data);
        Columns[1].Load4Ptr(data + 4);
        Columns[2].Load4Ptr(data + 8);
        Columns[3].Load4Ptr(data + 12);
    }

    FxMat4f& operator = (const FxMat4f& other)
    {
        Columns[0] = other.Columns[0];
        Columns[1] = other.Columns[1];
        Columns[2] = other.Columns[2];
        Columns[3] = other.Columns[3];
        return *this;
    }

    FxMat4f Inverse();

    FxMat4f operator * (const FxMat4f &other) const;

    void Print()
    {
        printf("\t=== Matrix ===\n");
        for (int i = 0; i < 4; i++) {
            Vec4f &column = Columns[i];
            printf("{ %.06f\t%.06f\t%.06f\t%.06f }", column.mData[0], column.mData[1], column.mData[2], column.mData[3]);
            putchar('\n');
        }
    }

    // Mat4f Multiply(const Mat4f &other);
    Vec4f MultiplyVec4f(Vec4f &vec);

private:
    float32x4_t MultiplyVec4f_Neon(Vec4f &vec);

public:
    union {
        Vec4f Columns[4];
        float32 RawData[16];
    };


    friend class Vec4f;

private:
};
