#include "Random.hpp"

namespace fx {


uint32 FastRand32()
{
	static uint32 sHash = 2463534242U;

	sHash ^= (sHash << 13);
	sHash ^= (sHash >> 17);

	return (sHash ^= (sHash << 5));
}


uint64 FastRand64()
{
	static uint64 sHash = 88172645463325252ULL;

	sHash ^= (sHash << 13);
	sHash ^= (sHash >> 7);

	return (sHash ^= (sHash << 17));
}


/////////////////////////////////////
// SIMD functions
/////////////////////////////////////

UINT4 FastRand4()
{
	// A bunch of random seeds (needs to be non-zero)
	static uint32 sSeeds[4] __attribute__((aligned(16))) = { 2463534258U, 480676568U, 784724923U, 521288629U };

	UINT4 vr;
	UINT4 tmp;

#ifdef FX_USE_NEON
	vr = vld1q_u32(sSeeds);

	tmp = vshlq_n_u32(vr, 13);
	vr = veorq_u32(vr, tmp);

	tmp = vshrq_n_u32(vr, 17);
	vr = veorq_u32(vr, tmp);

	tmp = vshlq_n_u32(vr, 5);
	vr = veorq_u32(vr, tmp);

	// Store the seeds back for next call
	vst1q_u32(sSeeds, vr);
#else
	vr = _mm_load_si128(reinterpret_cast<const __m128i*>(sSeeds));

	tmp = _mm_slli_epi32(vr, 13);
	vr = _mm_xor_si128(vr, tmp);

	tmp = _mm_srli_epi32(vr, 17);
	vr = _mm_xor_si128(vr, tmp);

	tmp = _mm_slli_epi32(vr, 5);
	vr = _mm_xor_si128(vr, tmp);

	_mm_store_si128(reinterpret_cast<__m128i*>(sSeeds), vr);
#endif

	return vr;
}


} // namespace fx
