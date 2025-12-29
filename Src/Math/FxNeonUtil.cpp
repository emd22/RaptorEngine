#include <Core/FxDefines.hpp>

#ifdef FX_USE_NEON

#include "FxMathConsts.hpp"
#include "FxNeonUtil.hpp"

static constexpr uint32 sConstSignMask = 0x80000000;

static constexpr float32 sConstHalfPi = FX_HALF_PI;
static constexpr float32 sConstPi = FX_PI;
static constexpr float32 sConst3HalfPi = sConstHalfPi * 3.0f;

static constexpr float32 sConst2Pi = FX_PI * 2.0;
static constexpr float32 sConstInv2Pi = 1.0f / sConst2Pi;


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

namespace FxNeon {

/**
 * @brief Fast limited precision polynomial cosine approximation.
 *
 */
FX_FORCE_INLINE static float32x4_t CosFastApprox(float32x4_t angle)
{
    // Algorithm adapted from https://www.ganssle.com/item/approximations-for-trig-c-code.htm and FastTrigo.

    const float32x4_t c1 = vdupq_n_f32(0.99940307f);
    const float32x4_t c2 = vdupq_n_f32(-0.49558072f);
    const float32x4_t c3 = vdupq_n_f32(0.03679168f);

    const float32x4_t angle_sq = vmulq_f32(angle, angle);

    return vaddq_f32(c1, vmulq_f32(angle_sq, vaddq_f32(c2, vmulq_f32(c3, angle_sq))));
}


void SinCos4_Fast(float32x4_t angles, float32x4_t* out_sine, float32x4_t* out_cosine)
{
    // Sine/cosine algorithm based on the SSE implementation in FastTrigo at https://github.com/divideconcept/FastTrigo.

    const float32x4_t v_f32_one = vdupq_n_f32(1.0f);
    const float32x4_t v_signmask = vreinterpretq_f32_u32(vdupq_n_u32(sConstSignMask));


    // 1 / (2 * Pi)
    const float32x4_t v_inv_2pi = vdupq_n_f32(sConstInv2Pi);

    const float32x4_t v_pi = vdupq_n_f32(sConstPi);
    const float32x4_t v_halfpi = vdupq_n_f32(sConstHalfPi);

    // 2 * Pi
    const float32x4_t v_2pi = vdupq_n_f32(sConst2Pi);

    const float32x4_t v_3halfpi = vdupq_n_f32(sConst3HalfPi);

    uint32x4_t angle_signs = vorrq_u32(ReinterpretAsUInt(v_f32_one), vandq_u32(v_signmask, ReinterpretAsUInt(angles)));

    // Remove the sign bit
    angles = AndNot(v_signmask, angles);
    angles = vsubq_f32(angles, vmulq_f32(Floor(vmulq_f32(angles, v_inv_2pi)), v_2pi));


    float32x4_t cos_angles = angles;

    cos_angles = Xor(cos_angles,
                     And(ReinterpretAsFloat(vcgeq_f32(angles, v_halfpi)), Xor(cos_angles, vsubq_f32(v_pi, angles))));
    cos_angles = Xor(cos_angles, And(ReinterpretAsFloat(vcgeq_f32(angles, v_pi)), v_signmask));
    cos_angles = Xor(cos_angles,
                     And(ReinterpretAsFloat(vcgeq_f32(angles, v_3halfpi)), Xor(cos_angles, vsubq_f32(v_2pi, angles))));

    float32x4_t result = CosFastApprox(cos_angles);

    // Calculate the cosine result
    result = Xor(result, And(And(ReinterpretAsFloat(vcgeq_f32(angles, v_halfpi)),
                                 ReinterpretAsFloat(vcltq_f32(angles, v_3halfpi))),
                             v_signmask));

    (*out_cosine) = result;

    const float32x4_t sine_multiplier = vmulq_f32(
        angle_signs,
        ReinterpretAsFloat(vorrq_u32(ReinterpretAsUInt(v_f32_one), vandq_u32(vcgtq_f32(angles, v_pi), v_signmask))));

    (*out_sine) = vmulq_f32(sine_multiplier, Sqrt(vsubq_f32(v_f32_one, vmulq_f32(result, result))));
}

// Implementation based on Cephes math library (https://www.moshier.net/index.html#Cephes)
// And the modified algorithm from http://gruntthepeon.free.fr/ssemath/sse_mathfun.h.

void SinCos4(float32x4_t in_values, float32x4_t* ysin, float32x4_t* ycos)
{
    float32x4_t x, y;

    uint32x4_t emm2;

    uint32x4_t sine_sign;

    {
        const uint32x4_t v_high_sign = vdupq_n_u32(0x80000000U);

        sine_sign = vandq_u32(ReinterpretAsUInt(in_values), v_high_sign);
        x = Xor(in_values, sine_sign);
    }

    {
        const float32x4_t c_f32_fopi = vdupq_n_f32(c_cephes_FOPI);
        y = vmulq_f32(x, c_f32_fopi);

        // Store the integer part of y
        emm2 = vcvtq_u32_f32(y);

        // j=(j+1) & (~1) (see the cephes sources)
        emm2 = vaddq_u32(emm2, vdupq_n_u32(1));
        emm2 = vandq_u32(emm2, vdupq_n_u32(~1));
        y = vcvtq_f32_u32(emm2);
    }

    uint32x4_t poly_mask = vtstq_u32(emm2, vdupq_n_u32(2));

    x = vmlaq_f32(x, y, vdupq_n_f32(c_minus_cephes_DP1));
    x = vmlaq_f32(x, y, vdupq_n_f32(c_minus_cephes_DP2));
    x = vmlaq_f32(x, y, vdupq_n_f32(c_minus_cephes_DP3));

    sine_sign = veorq_u32(sine_sign, vtstq_u32(emm2, vdupq_n_u32(4)));
    float32x4_t cosine_sign = vtstq_u32(vsubq_u32(emm2, vdupq_n_u32(2)), vdupq_n_u32(4));

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

    float32x4_t ys = vbslq_f32(poly_mask, y1, y2);
    float32x4_t yc = vbslq_f32(poly_mask, y2, y1);
    *ysin = vbslq_f32(sine_sign, vnegq_f32(ys), ys);
    *ycos = vbslq_f32(cosine_sign, yc, vnegq_f32(yc));
}
}; // namespace FxNeon

#endif // #ifdef FX_USE_NEON
