#pragma once

#include "Vec4.hpp"

#ifdef FX_USE_NEON
#include <arm_neon.h>
#endif

/**
 * @brief A single-precision 4x4 matrix class using SIMD instructions.
 *
 * This class represents a 4x4 matrix represented using _Column-Major_ order to match
 * the layout of MSL, GLSL, and _modern_ HLSL.
 */
class alignas(16) Mat4f
{
public:
    Mat4f() noexcept
    {
    }

    Mat4f(float scalar) noexcept
    {
        Data[0].Load1(scalar);
        Data[1].Load1(scalar);
        Data[2].Load1(scalar);
        Data[3].Load1(scalar);
    }

    Mat4f(Vec4f columns[4]) noexcept
    {
        Data[0] = columns[0];
        Data[1] = columns[1];
        Data[2] = columns[2];
        Data[3] = columns[3];
    }

    inline void LoadIdentity()
    {
        Data[0].Load4(1.0f, 0.0f, 0.0f, 0.0f);
        Data[1].Load4(0.0f, 1.0f, 0.0f, 0.0f);
        Data[2].Load4(0.0f, 0.0f, 1.0f, 0.0f);
        Data[3].Load4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    void Print()
    {
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                printf("\t%.02f", Data[j].mData[i]);
            }
            putchar('\n');
        }
    }


    Mat4f Multiply(Mat4f &other);
    Vec4f MultiplyVec4f(Vec4f &vec);

private:
    float32x4_t MultiplyVec4f_Neon(Vec4f &vec);

public:
    Vec4f Data[4];

    friend class Vec4f;

private:
};
