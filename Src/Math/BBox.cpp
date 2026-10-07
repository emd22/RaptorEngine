#include <Math/BBox.hpp>


namespace fx {


BBox::BBox(Vec3f min, Vec3f max) : Min(min), Max(max) {}

BBox& BBox::operator+=(const BBox& other)
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


void BBox::Add(const BBox& other)
{
	Min = Vec3f::Min(other.Min, Min);
	Max = Vec3f::Max(other.Max, Max);
}


BBox BBox::operator+(const BBox& other) const
{
	BBox result = (*this);
	result += other;

	return result;
}


BBox& BBox::operator=(const BBox& other)
{
	this->Min = other.Min;
	this->Max = other.Max;

	return *this;
}

/////////////////////////////////////
// Oriented Bounding Box
/////////////////////////////////////

OBBox OBBox::FromLocalBounds(const BBox& local_bounds, const Mat4f transform)
{
	OBBox obb {};

	for (uint32 corner = 0; corner < 8; corner++) {
		const Vec4f local((corner & (1 << 0)) ? local_bounds.Max.X : local_bounds.Min.X,
						  (corner & (1 << 1)) ? local_bounds.Max.Y : local_bounds.Min.Y,
						  (corner & (1 << 2)) ? local_bounds.Max.Z : local_bounds.Min.Z, 1.0f);

		const Vec4f world = transform * local;
		obb.Corners[corner] = Vec3f(world.X, world.Y, world.Z);
	}

	return obb;
}

BBox OBBox::GetWorldAABB() const
{
	Vec3f out_min { std::numeric_limits<float32>::max() };
	Vec3f out_max { -std::numeric_limits<float32>::max() };

	for (const Vec3f corner : Corners) {
		out_min = Vec3f::Min(out_min, corner);
		out_max = Vec3f::Max(out_max, corner);
	}

	return BBox(out_min, out_max);
}


} // namespace fx
