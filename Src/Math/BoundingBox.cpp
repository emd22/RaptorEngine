#include <Math/BoundingBox.hpp>

namespace fx {


BoundingBox::BoundingBox(Vec3f min, Vec3f max) : Min(min), Max(max) {}

BoundingBox& BoundingBox::operator+=(const BoundingBox& other)
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


void BoundingBox::Add(const BoundingBox& other)
{
	Min = Vec3f::Min(other.Min, Min);
	Max = Vec3f::Max(other.Max, Max);
}


BoundingBox BoundingBox::operator+(const BoundingBox& other) const
{
	BoundingBox result = (*this);
	result += other;

	return result;
}


BoundingBox& BoundingBox::operator=(const BoundingBox& other)
{
	this->Min = other.Min;
	this->Max = other.Max;

	return *this;
}

} // namespace fx
