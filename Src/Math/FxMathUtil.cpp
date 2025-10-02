#include "FxMathUtil.hpp"

namespace FxMath {

#define c_minus_cephes_DP1 -0.78515625
#define c_minus_cephes_DP2 -2.4187564849853515625e-4
#define c_minus_cephes_DP3 -3.77489497744594108e-8
#define c_sincof_p0        -1.9515295891E-4
#define c_sincof_p1        8.3321608736E-3
#define c_sincof_p2        -1.6666654611E-1
#define c_coscof_p0        2.443315711809948E-005
#define c_coscof_p1        -1.388731625493765E-003
#define c_coscof_p2        4.166664568298827E-002
#define c_cephes_FOPI      1.27323954473516 // 4 / M_PI


FX_FORCE_INLINE static uint32 TstU32(uint32 a, uint32 b) { return (a & b) ? (~0) : 0; }


void SinCos(float32 in_angle, float32* out_sine, float32* out_cosine)
{
    // TODO: Clean up this function to be more tailored to scalar

    float32 x, y;

    uint32 emm2;
    uint32 sine_sign;

    {
        const uint32 v_high_sign = (0x80000000U);

        sine_sign = (std::bit_cast<uint32>(in_angle) & v_high_sign);
        x = std::bit_cast<float32>(std::bit_cast<uint32>(in_angle) ^ sine_sign);
    }

    {
        y = (x * c_cephes_FOPI);

        // Store the integer part of y
        emm2 = static_cast<uint32>(y);

        // j=(j+1) & (~1) (see the cephes sources)
        emm2 += 1;
        emm2 &= (~1);

        // emm2 = ((emm2 + 1) & (~1));

        y = static_cast<float32>(emm2);
    }

    /* get the polynom selection mask
     *     there is one polynom for 0 <= x <= Pi/4
     *     and another one for Pi/4<x<=Pi/2
     *
     *     Both branches will be computed.
     */
    uint32 poly_mask = TstU32(emm2, 2);

    /* The magic pass: "Extended precision modular arithmetic"
     *     x = ((x - y * DP1) - y * DP2) - y * DP3; */
    x += y * (c_minus_cephes_DP1);
    x += y * (c_minus_cephes_DP2);
    x += y * (c_minus_cephes_DP3);

    sine_sign = sine_sign ^ TstU32(emm2, 4);
    float32 cosine_sign = TstU32((emm2 - 2), 4);

    /* Evaluate the first polynom  (0 <= x <= Pi/4) in y1,
     *     and the second polynom      (Pi/4 <= x <= 0) in y2 */
    float32 z = x * x;
    float32 y1, y2;

    y1 = (c_coscof_p1 + z * c_coscof_p0);
    y2 = (c_sincof_p1 + z * c_sincof_p0);

    y1 = (c_coscof_p2 + y1 * z);
    y2 = (c_sincof_p2 + y2 * z);

    y1 = y1 * z;
    y2 = y2 * z;

    y1 = y1 * z;
    y1 = (y1 - (z * 0.5f));

    y2 = x + (y2 * x);

    y1 += 1.0f;

    float32 ys = (poly_mask) ? y1 : y2;
    float32 yc = (poly_mask) ? y2 : y1;

    /* select the correct result from the two polynoms */
    // float32 ys = vbslq_f32(poly_mask, y1, y2);
    // float32x4_t yc = vbslq_f32(poly_mask, y2, y1);

    (*out_sine) = (sine_sign) ? (-ys) : ys;
    (*out_cosine) = (cosine_sign) ? (yc) : -yc;

    // *ysin = vbslq_f32(sine_sign, vnegq_f32(ys), ys);
    // *ycos = vbslq_f32(cosine_sign, yc, vnegq_f32(yc));
}


} // namespace FxMath
