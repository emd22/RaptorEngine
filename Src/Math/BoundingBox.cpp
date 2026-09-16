#include <Math/BoundingBox.hpp>

namespace fx {


AABB::AABB(Vec3f min, Vec3f max) : Min(min), Max(max) {}

AABB& AABB::operator+=(const AABB& other)
{
	/*

			Min X          Max X
			|                |

	Max Y - +---------+ . . .
			|      +=========+
			|      |  |      |
			+------|--+      |
	Min Y -  . . . +=========+

	 */


	Min = Vec3f::Min(other.Min, Min);
	Max = Vec3f::Max(other.Max, Max);

	return *this;
}


void AABB::Add(const AABB& other)
{
	Min = Vec3f::Min(other.Min, Min);
	Max = Vec3f::Max(other.Max, Max);
}


AABB AABB::operator+(const AABB& other) const
{
	AABB result = (*this);
	result += other;

	return result;
}


AABB& AABB::operator=(const AABB& other)
{
	this->Min = other.Min;
	this->Max = other.Max;

	return *this;
}

} // namespace fx
