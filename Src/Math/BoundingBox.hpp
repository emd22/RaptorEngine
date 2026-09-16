#pragma once

#include "Vec3.hpp"

namespace fx {

class AABB
{
public:
	AABB() = default;
	AABB(Vec3f min, Vec3f max);

	FX_FORCE_INLINE Vec3f GetSize() { return Max - Min; }

	void Add(const AABB& other);

	AABB operator+(const AABB& other) const;
	AABB& operator+=(const AABB& other);

	AABB& operator=(const AABB& other);

public:
	Vec3f Min = Vec3f::sZero;
	Vec3f Max = Vec3f::sZero;
};

} // namespace fx
