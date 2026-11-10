#include <Math/BBox.hpp>


namespace fx {


BBox::BBox(FVec3 min, FVec3 max) : Min(min), Max(max) {}

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


	Min = FVec3::Min(other.Min, Min);
	Max = FVec3::Max(other.Max, Max);

	return *this;
}

BBox BBox::AddMargin(const FVec3 margin) const { return BBox { Min - margin, Max + margin }; }


void BBox::Add(const BBox& other)
{
	Min = FVec3::Min(other.Min, Min);
	Max = FVec3::Max(other.Max, Max);
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

OBBox OBBox::FromLocalBounds(const BBox& local_bounds, const FMat4 transform)
{
	OBBox obb {};

	for (uint32 corner = 0; corner < 8; corner++) {
		const FVec4 local((corner & (1 << 0)) ? local_bounds.Max.X : local_bounds.Min.X,
						  (corner & (1 << 1)) ? local_bounds.Max.Y : local_bounds.Min.Y,
						  (corner & (1 << 2)) ? local_bounds.Max.Z : local_bounds.Min.Z, 1.0f);

		const FVec4 world = transform * local;
		obb.Corners[corner] = FVec3(world.X, world.Y, world.Z);
	}

	return obb;
}

BBox OBBox::GetWorldAABB() const
{
	FVec3 out_min { std::numeric_limits<float32>::max() };
	FVec3 out_max { -std::numeric_limits<float32>::max() };

	for (const FVec3 corner : Corners) {
		out_min = FVec3::Min(out_min, corner);
		out_max = FVec3::Max(out_max, corner);
	}

	return BBox(out_min, out_max);
}


} // namespace fx
