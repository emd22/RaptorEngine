#include "Random.hpp"

namespace fx {


static uint32 sRand32State = 2463534242U;

uint32 FastRand32()
{
	sRand32State ^= (sRand32State << 13);
	sRand32State ^= (sRand32State >> 17);

	return (sRand32State ^= (sRand32State << 5));
}


void SeedFastRand(uint32 seed)
{
	sRand32State = 2463534242U ^ (seed * 2654435761U);

	if (sRand32State == 0) {
		sRand32State = 2463534242U;
	}
}


float32 FastRandUnit() { return static_cast<float32>(FastRand32() >> 8) * (1.0f / 16777216.0f); }


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
	alignas(16) static uint32 sSeeds[4] = { 2463534258U, 480676568U, 784724923U, 521288629U };

	UINT4 vr = simd::LoadUInt4(sSeeds);
	UINT4 tmp;

#ifdef FX_USE_NEON
	vr = vld1q_u32(sSeeds);

	tmp = vshlq_n_u32(vr, 13);
	vr = veorq_u32(vr, tmp);

	tmp = vshrq_n_u32(vr, 17);
	vr = veorq_u32(vr, tmp);

	tmp = vshlq_n_u32(vr, 5);
	vr = veorq_u32(vr, tmp);

#else
	vr = _mm_load_si128(reinterpret_cast<const __m128i*>(sSeeds));

	tmp = _mm_slli_epi32(vr, 13);
	vr = _mm_xor_si128(vr, tmp);

	tmp = _mm_srli_epi32(vr, 17);
	vr = _mm_xor_si128(vr, tmp);

	tmp = _mm_slli_epi32(vr, 5);
	vr = _mm_xor_si128(vr, tmp);
#endif

	// Store the seeds back for next call
	simd::StoreUInt4(sSeeds, vr);

	return vr;
}


} // namespace fx
