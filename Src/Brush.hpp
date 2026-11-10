/*
 * File:        Brush.hpp
 * Author:      emd22
 * Created:     23/09/2026
 * Description: Convex brush geometry, defined by a set of bounding planes.
 */

#pragma once

#include <Core/DynArray.hpp>
#include <Core/SizedArray.hpp>
#include <Core/StackArray.hpp>
#include <Core/String.hpp>
#include <Math/BBox.hpp>
#include <Math/FVec3.hpp>
#include <Math/Vec2.hpp>

namespace fx {

/**
 * @brief How the face on a brush plane is textured. UVs are projected along the axis the face is most aligned with,
 * then rotated, divided by the scale and offset.
 */
struct BrushFaceTexture
{
	/// Added after scaling, in texture repeats
	FVec2 Offset = FVec2(0.0f, 0.0f);
	FVec2 Scale = FVec2(2.0f, 2.0f);

	/// In degrees
	float32 Rotation = 0.0f;
};

struct BrushPlane
{
	FVec3 Normal = FVec3::sZero;
	float32 Distance = 0.0f;

	BrushFaceTexture Texture;

	String Material;
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

	/// The pieces a brush is cut into, one plane list each
	using PieceList = DynArray<PlaneList, GrowthFunctions::Double>;

	struct Face
	{
		uint32 PlaneIndex = 0;
		SizedArray<FVec3> Vertices;
	};

public:
	/**
	 * @brief Creates a box with its textures anchored to its minimum corner, matching MeshGen::MakeCube with bAlignUVs
	 */
	static Brush FromBox(const FVec3 min, const FVec3 max);
	static Brush FromPlanes(const PlaneList& planes);

	static Brush FromVertices(const SizedArray<FVec3>& points, const PlaneList& reference);

	bool Rebuild();

	bool IsValid() const { return Faces.IsNotEmpty(); }
	bool IsBox() const;

	FVec3 GetBoundsMin() const { return mBounds.Min; }
	FVec3 GetBoundsMax() const { return mBounds.Max; }
	FVec3 GetCenter() const { return (mBounds.Min + mBounds.Max) * 0.5f; }

	/**
	 * @brief Returns the unique corner points of the brush
	 */
	const SizedArray<FVec3>& GetVertices() const { return mVertices; }

	bool ContainsPoint(const FVec3 point, float32 tolerance = 0.0f) const;

	/**
	 * @brief Returns the index of the plane facing along `normal`, or scNoPlane
	 */
	int32 FindPlane(const FVec3 normal) const;

	/**
	 * @brief Returns how far the brush reaches along `direction`. For a face normal this is the distance of its plane
	 */
	float32 GetSupport(const FVec3 direction) const;

	/**
	 * @brief Returns the average of the corners of the face on a plane
	 */
	FVec3 GetFaceCenter(uint32 plane_index) const;

	/**
	 * @brief Finds where a ray starting outside the brush enters it
	 * @param out_distance Distance to the hit in multiples of the length of `direction`
	 */
	bool Raycast(const FVec3 origin, const FVec3 direction, float32& out_distance, uint32& out_plane_index) const;

	/**
	 * @brief Splits the brush in two along a plane. The new faces get the texture layout of the brush's local space.
	 * @param out_back The planes of the part behind the plane
	 * @param out_front The planes of the part in front of it
	 * @param origin Where the brush's local origin is in the world. The two new faces get their textures lined up with
	 * the world grid from it, like AlignTexturesToWorld(), instead of starting from the local origin.
	 * @returns false if the plane misses the brush, or the brush has no room for another plane
	 */
	bool Split(const FVec3 normal, float32 distance, const FVec3 origin, PlaneList& out_back,
			   PlaneList& out_front) const;

	bool Subtract(const PlaneList& cutter, const FVec3 origin, PieceList& out_pieces) const;

	/**
	 * @brief Resets a face's texture layout to the one FromBox() uses
	 */
	void ResetFaceTexture(uint32 plane_index);

	/**
	 * @brief Lines every face's texture up with the world grid, for a brush on an unrotated object at `origin`.
	 */
	void AlignTexturesToWorld(const FVec3 origin);
	void AlignFaceTextureToWorld(uint32 plane_index, const FVec3 origin);

	/**
	 * @brief Returns true if every face has the default layout from ResetFaceTexture()
	 */
	bool HasDefaultTextures() const;

	bool HasFaceMaterials() const;

	void GetFaceMaterials(StackArray<String, scMaxPlanes>& out_materials) const;

	/**
	 * @brief Returns how fast a plane's U and V change across the brush's local space. A point's texture coordinate is
	 * its dot product with each gradient, plus the plane's texture offset.
	 */
	static void GetTextureGradients(const BrushPlane& plane, FVec3& out_u_gradient, FVec3& out_v_gradient);

	/**
	 * @brief Generates a triangle mesh for rendering
	 */
	void GenerateMesh(SizedArray<FVec3>& positions, SizedArray<FVec3>& normals, SizedArray<FVec3>& tangents,
					  SizedArray<float32>& tangent_handedness, SizedArray<FVec2>& texcoords,
					  SizedArray<uint32>& indices, const String* only_material = nullptr) const;

public:
	PlaneList Planes;
	SizedArray<Face> Faces;

private:
	SizedArray<FVec3> mVertices;

	BBox mBounds;
};

} // namespace fx
