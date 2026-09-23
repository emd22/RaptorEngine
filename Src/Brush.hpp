/*
 * File:        Brush.hpp
 * Author:      emd22
 * Created:     23/09/2026
 * Description: Convex brush geometry, defined by a set of bounding planes.
 */

#pragma once

#include <Core/SizedArray.hpp>
#include <Core/StackArray.hpp>
#include <Material/MaterialID.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <Object/MeshSection.hpp>

namespace fx {

/**
 * @brief How the face on a brush plane is textured. UVs are projected along the axis the face is most aligned with,
 * then rotated, divided by the scale and offset.
 */
struct BrushFaceTexture
{
	/// Null uses the material of the object the brush belongs to
	MaterialID Material = MaterialID::scNull;

	/// Added after scaling, in texture repeats
	Vec2f Offset = Vec2f(0.0f, 0.0f);

	/// Units per texture repeat. Negative values mirror the texture
	Vec2f Scale = Vec2f(1.0f, 1.0f);

	/// In degrees
	float32 Rotation = 0.0f;
};

struct BrushPlane
{
	Vec3f Normal = Vec3f::sZero;
	float32 Distance = 0.0f;

	BrushFaceTexture Texture;
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
	static constexpr int32 scNoPlane = -1;

	/// Fixed capacity so that brushes can be snapshotted by value (for the undo stack in the editor)
	using PlaneList = StackArray<BrushPlane, scMaxPlanes>;

	struct Face
	{
		uint32 PlaneIndex = 0;
		SizedArray<Vec3f> Vertices;
	};

public:
	/**
	 * @brief Creates a box with its textures anchored to its minimum corner, matching MeshGen::MakeCube with bAlignUVs
	 */
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
	 * @brief Returns the index of the plane facing along `normal`, or scNoPlane
	 */
	int32 FindPlane(const Vec3f& normal) const;

	/**
	 * @brief Returns how far the brush reaches along `direction`. For a face normal this is the distance of its plane
	 */
	float32 GetSupport(const Vec3f& direction) const;

	/**
	 * @brief Returns the average of the corners of the face on a plane
	 */
	Vec3f GetFaceCenter(uint32 plane_index) const;

	/**
	 * @brief Finds where a ray starting outside the brush enters it
	 * @param out_distance Distance to the hit in multiples of the length of `direction`
	 */
	bool Raycast(const Vec3f& origin, const Vec3f& direction, float32& out_distance, uint32& out_plane_index) const;

	/**
	 * @brief Splits the brush in two along a plane. The new faces get the texture layout of the brush's local space.
	 * @param out_back The planes of the part behind the plane
	 * @param out_front The planes of the part in front of it
	 * @returns false if the plane misses the brush, or the brush has no room for another plane
	 */
	bool Split(const Vec3f& normal, float32 distance, PlaneList& out_back, PlaneList& out_front) const;

	/**
	 * @brief Resets a face's texture layout to the one FromBox() uses, keeping its material
	 */
	void ResetFaceTexture(uint32 plane_index);

	/**
	 * @brief Lines every face's texture up with the world grid, for a brush on an unrotated object at `origin`. Faces
	 * keep their materials.
	 */
	void AlignTexturesToWorld(const Vec3f& origin);

	/**
	 * @brief Returns true if every face has the default layout from ResetFaceTexture() and no material of its own
	 */
	bool HasDefaultTextures() const;

	/**
	 * @brief Generates a triangle mesh for rendering
	 * @param sections Filled with a section per material if any face has a material of its own, otherwise left empty
	 */
	void GenerateMesh(SizedArray<Vec3f>& positions, SizedArray<Vec3f>& normals, SizedArray<Vec3f>& tangents,
					  SizedArray<Vec2f>& texcoords, SizedArray<uint32>& indices,
					  SizedArray<MeshSection>& sections) const;

public:
	PlaneList Planes;
	SizedArray<Face> Faces;

private:
	SizedArray<Vec3f> mVertices;

	Vec3f mBoundsMin = Vec3f::sZero;
	Vec3f mBoundsMax = Vec3f::sZero;
};

} // namespace fx
