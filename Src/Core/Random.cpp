#include "Random.hpp"

namespace fx {


uint32 FastRand32()
{
	static uint32 sHash = 2463534242;

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


} // namespace fx
