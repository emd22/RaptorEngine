/*
 * File:        Brush.hpp
 * Author:      emd22
 * Created:     23/09/2026
 * Description: Convex brush geometry, defined by a set of bounding planes.
 */

#pragma once

#include <Core/SizedArray.hpp>
#include <Core/StackArray.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>

namespace fx {

struct BrushPlane
{
	Vec3f Normal = Vec3f::sZero;
	float32 Distance = 0.0f;
};

/**
 * @brief A convex solid defined by the intersection of its planes
 *
 * @note Brushes are move-only, so to copy one rebuild it from its planes with FromPlanes()
 */
class Brush
{
public:
	static constexpr uint32 scMaxPlanes = 32;

	/// Fixed capacity so that brushes can be snapshotted by value (for the undo stack in the editor)
	using PlaneList = StackArray<BrushPlane, scMaxPlanes>;

	struct Face
	{
		uint32 PlaneIndex = 0;
		SizedArray<Vec3f> Vertices;
	};

public:
	static Brush FromBox(const Vec3f& min, const Vec3f& max);
	static Brush FromPlanes(const PlaneList& planes);

	bool Rebuild();

	bool IsValid() const { return Faces.IsNotEmpty(); }
	bool IsBox() const;

	Vec3f GetBoundsMin() const { return mBoundsMin; }
	Vec3f GetBoundsMax() const { return mBoundsMax; }
	Vec3f GetCenter() const { return (mBoundsMin + mBoundsMax) * 0.5f; }

	/**
	 * @brief Returns the unique corner points of the brush
	 */
	const SizedArray<Vec3f>& GetVertices() const { return mVertices; }

	bool ContainsPoint(const Vec3f& point, float32 tolerance = 0.0f) const;

	/**
	 * @brief Generates a triangle mesh for rendering
	 */
	void GenerateMesh(SizedArray<Vec3f>& positions, SizedArray<Vec3f>& normals, SizedArray<Vec3f>& tangents,
					  SizedArray<Vec2f>& texcoords, SizedArray<uint32>& indices) const;

public:
	PlaneList Planes;
	SizedArray<Face> Faces;

private:
	SizedArray<Vec3f> mVertices;

	Vec3f mBoundsMin = Vec3f::sZero;
	Vec3f mBoundsMax = Vec3f::sZero;
};

} // namespace fx
