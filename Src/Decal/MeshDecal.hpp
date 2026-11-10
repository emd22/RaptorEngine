#pragma once

#include <Asset/MeshGen.hpp>
#include <Brush.hpp>
#include <Core/DynArray.hpp>
#include <Math/BBox.hpp>
#include <Math/FVec3.hpp>
#include <Math/Mat4.hpp>
#include <Math/Vec2.hpp>
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

	void AddTriangles(const FVec3* positions, const FVec3* normals, const uint32* indices, uint32 index_count);

	bool IsEmpty() const { return mIndices.IsEmpty(); }

	void Build(MeshGen::GeneratedMesh& out_mesh) const;

	static void BuildPlaceholder(const Brush& volume, MeshGen::GeneratedMesh& out_mesh);

private:
	struct LocalPlane
	{
		FVec3 Normal;
		float32 Distance;
	};

	struct BrushReceiver
	{
		const Brush* pBrush = nullptr;
		Mat4f ToLocal;
		FVec3 Min;
		FVec3 Max;
		std::vector<LocalPlane> Planes;
	};

	using Polygon = std::vector<FVec3>;

private:
	uint32 ClipToVolume(const FVec3* points, uint32 count, FVec3* out_points) const;
	void EmitPolygon(const FVec3* points, uint32 count, const FVec3 normal);
	bool OverlapsVolume(const FVec3 min, const FVec3 max) const;

	static void SubtractReceiver(std::vector<Polygon>& fragments, const BrushReceiver& occluder,
								 bool occluder_wins_ties, const FVec3 normal);

private:
	template <typename TElementType>
	using GrowingArray = DynArray<TElementType, GrowthFunctions::Double>;

	std::vector<BrushReceiver> mBrushes;

	const Brush& mVolume;
	Mat4f mWorldToLocal;

	FVec3 mLocalMin;
	FVec3 mLocalMax;

	FVec3 mUGradient;
	FVec3 mVGradient;
	float32 mMinFacing = 0.0f;

	GrowingArray<FVec3> mPositions;
	GrowingArray<FVec3> mNormals;
	GrowingArray<FVec3> mTangents;
	GrowingArray<float32> mHandedness;
	GrowingArray<Vec2f> mTexcoords;
	GrowingArray<uint32> mIndices;
};

} // namespace fx
