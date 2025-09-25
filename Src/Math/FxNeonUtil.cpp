#include "FxNeonUtil.hpp"


#define __m128  float32x4_t
#define __m128i int32x4_t

// Represents a constant value as an integer SSE vector
#define AS_INTV(constant)   *(__m128i*)constant
#define AS_FLOATV(constant) *(__m128*)constant

// Implementation based on Cephes math library (https://www.moshier.net/index.html#Cephes)
// And the modified algorithm from http://gruntthepeon.free.fr/ssemath/sse_mathfun.h.

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

// Get the absolute value of a vector
#define SSE_ABS_PS(vx_) _mm_and_ps(vx_, AS_FLOATV(_ps_inv_sign_mask))

namespace FxNeon {

void SinCos4(float32x4_t x, float32x4_t* ysin, float32x4_t* ycos)
{
    // any x
    float32x4_t y = vdupq_n_f32(0);

    uint32x4_t emm2;

    // const uint32x4_t v_u32_one = vdupq_n_f32(1);
    const float32x4_t v_f32_zero = y;


    uint32x4_t sign_mask_sin, sign_mask_cos;
    sign_mask_sin = vcltq_f32(x, v_f32_zero);
    x = vabsq_f32(x);

    // Scale by 4 / PI
    const float32x4_t c_f32_fopi = vdupq_n_f32(c_cephes_FOPI);
    y = vmulq_f32(x, c_f32_fopi);

    // Store the integer part of y
    emm2 = vcvtq_u32_f32(y);

    // j=(j+1) & (~1) (see the cephes sources)
    emm2 = vaddq_u32(emm2, vdupq_n_u32(1));
    emm2 = vandq_u32(emm2, vdupq_n_u32(~1));
    y = vcvtq_f32_u32(emm2);

    /* get the polynom selection mask
     *     there is one polynom for 0 <= x <= Pi/4
     *     and another one for Pi/4<x<=Pi/2
     *
     *     Both branches will be computed.
     */
    uint32x4_t poly_mask = vtstq_u32(emm2, vdupq_n_u32(2));

    /* The magic pass: "Extended precision modular arithmetic"
     *     x = ((x - y * DP1) - y * DP2) - y * DP3; */
    x = vmlaq_f32(x, y, vdupq_n_f32(c_minus_cephes_DP1));
    x = vmlaq_f32(x, y, vdupq_n_f32(c_minus_cephes_DP2));
    x = vmlaq_f32(x, y, vdupq_n_f32(c_minus_cephes_DP3));

    sign_mask_sin = veorq_u32(sign_mask_sin, vtstq_u32(emm2, vdupq_n_u32(4)));
    sign_mask_cos = vtstq_u32(vsubq_u32(emm2, vdupq_n_u32(2)), vdupq_n_u32(4));

    /* Evaluate the first polynom  (0 <= x <= Pi/4) in y1,
     *     and the second polynom      (Pi/4 <= x <= 0) in y2 */
    float32x4_t z = vmulq_f32(x, x);
    float32x4_t y1, y2;

    y1 = vmlaq_f32(vdupq_n_f32(c_coscof_p1), z, vdupq_n_f32(c_coscof_p0));
    y2 = vmlaq_f32(vdupq_n_f32(c_sincof_p1), z, vdupq_n_f32(c_sincof_p0));
    y1 = vmlaq_f32(vdupq_n_f32(c_coscof_p2), y1, z);
    y2 = vmlaq_f32(vdupq_n_f32(c_sincof_p2), y2, z);
    y1 = vmulq_f32(y1, z);
    y2 = vmulq_f32(y2, z);
    y1 = vmulq_f32(y1, z);
    y1 = vmlsq_f32(y1, z, vdupq_n_f32(0.5f));
    y2 = vmlaq_f32(x, y2, x);
    y1 = vaddq_f32(y1, vdupq_n_f32(1));

    /* select the correct result from the two polynoms */
    float32x4_t ys = vbslq_f32(poly_mask, y1, y2);
    float32x4_t yc = vbslq_f32(poly_mask, y2, y1);
    *ysin = vbslq_f32(sign_mask_sin, vnegq_f32(ys), ys);
    *ycos = vbslq_f32(sign_mask_cos, yc, vnegq_f32(yc));
}

}; // namespace FxNeon
