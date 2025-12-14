#include <Core/FxDefines.hpp>


#ifdef FX_USE_AVX

#include <Math/FxAVXUtil.hpp>
#include <Math/FxMathConsts.hpp>

#include <immintrin.h>

static constexpr uint32 sConstSignMask = 0x80000000;

static constexpr float32 sConstHalfPi = FX_PI_2;
static constexpr float32 sConstPi = M_PI;
static constexpr float32 sConst3HalfPi = sConstHalfPi * 3.0f;

static constexpr float32 sConst2Pi = M_PI * 2.0;
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

namespace FxSSE {

// Implementation based on and DirectXMath: https://github.com/microsoft/DirectXMath/
// Minimax approximation for sine/cosine
void SinCos4(__m128 in_values, __m128* ysin, __m128* ycos)
{
    const __m128 cvOne = _mm_set1_ps(1.0);
    const __m128 cvNegZero = _mm_set1_ps(-0.0f);

    const __m128 cvSineCof1 = _mm_set1_ps(-2.3889859e-08f);
    const __m128 cvSineCof0 = _mm_setr_ps(-0.16666667f, +0.0083333310f, -0.00019840874f, +2.7525562e-06f);

    const __m128 cvCosineCof1 = _mm_set1_ps(-2.6051615e-07f);
    const __m128 cvCosineCof0 = _mm_setr_ps(-0.5f, +0.041666638f, -0.0013888378f, +2.4760495e-05f);

    const __m128 cvPi = _mm_set1_ps(FX_PI);
    const __m128 cvHalfPi = _mm_set1_ps(FX_PI_2);

    __m128 sign = _mm_and_ps(in_values, cvNegZero);
    __m128 pi_or_neg_pi = _mm_or_ps(cvPi, sign);
    __m128 le_result = _mm_cmple_ps(_mm_andnot_ps(sign, in_values), cvHalfPi);

    __m128 sel0 = _mm_and_ps(le_result, in_values);
    __m128 sel1 = _mm_andnot_ps(le_result, _mm_sub_ps(pi_or_neg_pi, in_values));

    in_values = _mm_or_ps(sel0, sel1);

    sel0 = _mm_and_ps(le_result, cvOne);
    sel1 = _mm_andnot_ps(le_result, _mm_or_ps(cvOne, cvNegZero));
    sign = _mm_or_ps(sel0, sel1);

    __m128 values_sq = _mm_mul_ps(in_values, in_values);
    
    // Sine approximation
    __m128 scof0 = _mm_permute_ps(cvSineCof0, _MM_SHUFFLE(3, 3, 3, 3));
    __m128 result = _mm_fmadd_ps(cvSineCof1, values_sq, scof0);

    scof0 = _mm_permute_ps(cvSineCof0, _MM_SHUFFLE(2, 2, 2, 2));
    result = _mm_fmadd_ps(result, values_sq, scof0);
    
    scof0 = _mm_permute_ps(cvSineCof0, _MM_SHUFFLE(1, 1, 1, 1));
    result = _mm_fmadd_ps(result, values_sq, scof0);
    
    scof0 = _mm_permute_ps(cvSineCof0, _MM_SHUFFLE(0, 0, 0, 0));
    result = _mm_fmadd_ps(result, values_sq, scof0);

    result = _mm_fmadd_ps(result, values_sq, cvOne);
    result = _mm_mul_ps(result, in_values);
    
    (*ysin) = result;

    // Cosine approximation

    __m128 ccof0 = _mm_permute_ps(cvCosineCof0, _MM_SHUFFLE(3, 3, 3, 3));
    result = _mm_fmadd_ps(cvCosineCof1, values_sq, ccof0);
    
    ccof0 = _mm_permute_ps(cvCosineCof0, _MM_SHUFFLE(2, 2, 2, 2));
    result = _mm_fmadd_ps(result, values_sq, ccof0);
    
    ccof0 = _mm_permute_ps(cvCosineCof0, _MM_SHUFFLE(1, 1, 1, 1));
    result = _mm_fmadd_ps(result, values_sq, ccof0);

    ccof0 = _mm_permute_ps(cvCosineCof0, _MM_SHUFFLE(0, 0, 0, 0));
    result = _mm_fmadd_ps(result, values_sq, ccof0);

    result = _mm_fmadd_ps(result, values_sq, cvOne);
    result = _mm_mul_ps(result, sign);

    (*ycos) = result;
}

} // namespace FxSSE

#endif