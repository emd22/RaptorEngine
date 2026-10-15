#pragma once

#include <Asset/MeshGen.hpp>
#include <Brush.hpp>
#include <Core/DynArray.hpp>
#include <Math/BBox.hpp>
#include <Math/Mat4.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <vector>

namespace fx {

class MeshDecalBuilder
{
public:
	static constexpr float32 scSurfaceOffset = 0.002f;
	static constexpr float32 scMaxAngleDegrees = 80.0f;

public:
	MeshDecalBuilder(const Brush& volume, const Mat4f& volume_to_world);

	void AddBrush(const Brush& brush, const Mat4f& brush_to_world);
	void ClipBrushes();

	void AddTriangles(const Vec3f* positions, const Vec3f* normals, const uint32* indices, uint32 index_count);

	bool IsEmpty() const { return mIndices.IsEmpty(); }

	void Build(MeshGen::GeneratedMesh& out_mesh) const;

	static void BuildPlaceholder(const Brush& volume, MeshGen::GeneratedMesh& out_mesh);

private:
	struct LocalPlane
	{
		Vec3f Normal;
		float32 Distance;
	};

	struct BrushReceiver
	{
		const Brush* pBrush = nullptr;
		Mat4f ToLocal;
		Vec3f Min;
		Vec3f Max;
		std::vector<LocalPlane> Planes;
	};

	using Polygon = std::vector<Vec3f>;

private:
	uint32 ClipToVolume(const Vec3f* points, uint32 count, Vec3f* out_points) const;
	void EmitPolygon(const Vec3f* points, uint32 count, const Vec3f normal);
	bool OverlapsVolume(const Vec3f min, const Vec3f max) const;

	static void SubtractReceiver(std::vector<Polygon>& fragments, const BrushReceiver& occluder, bool occluder_wins_ties,
								 const Vec3f normal);

private:
	template <typename TElementType>
	using GrowingArray = DynArray<TElementType, GrowthFunctions::Double>;

	std::vector<BrushReceiver> mBrushes;

	const Brush& mVolume;
	Mat4f mWorldToLocal;

	Vec3f mLocalMin;
	Vec3f mLocalMax;

	Vec3f mUGradient;
	Vec3f mVGradient;
	float32 mMinFacing = 0.0f;

	GrowingArray<Vec3f> mPositions;
	GrowingArray<Vec3f> mNormals;
	GrowingArray<Vec3f> mTangents;
	GrowingArray<float32> mHandedness;
	GrowingArray<Vec2f> mTexcoords;
	GrowingArray<uint32> mIndices;
};

} // namespace fx
