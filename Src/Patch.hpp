/*
 * File:        Patch.hpp
 * Author:      emd22
 * Created:     15/10/2026
 * Description: Curved surfaces made of Bezier patches or vertex grids (detail meshes) which are tessellated
 *              into triangles
 */

#pragma once

#include <Core/SizedArray.hpp>
#include <Math/Mat4.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <Math/Vec3d.hpp>

namespace fx {

struct PatchColor
{
	float32 R = 1.0f;
	float32 G = 1.0f;
	float32 B = 1.0f;
	float32 A = 1.0f;

	bool IsWhite() const { return R == 1.0f && G == 1.0f && B == 1.0f && A == 1.0f; }

	uint32 Pack() const;

	bool operator==(const PatchColor& other) const = default;
};

struct PatchPoint
{
	Vec3f Position = Vec3f::sZero;
	Vec2f Texcoord = Vec2f(0.0f, 0.0f);
	float32 Weight = 1.0f;
	PatchColor Color;
};

struct PatchVertex
{
	Vec3d Position = Vec3d(0.0);
	Vec3d Normal = Vec3d(0.0);
	Vec3d Tangent = Vec3d(0.0);
	double Handedness = 1.0;
	Vec2d Texcoord = Vec2d(0.0, 0.0);
	Vec2d Parameter = Vec2d(0.0, 0.0);
	PatchColor Color;
};

struct PatchMesh
{
	uint32 Columns = 0;
	uint32 Rows = 0;

	SizedArray<PatchVertex> Vertices;
	SizedArray<uint32> Indices;
};

enum class ePatchPrimitive : uint8
{
	Flat,
	Cylinder,
	Cone,
	Bevel,
	Endcap,
	DetailMesh,

	Count,
};

enum class ePatchClosure : uint8
{
	None,
	Cap,
	Thicken,

	Count,
};

class Patch
{
public:
	static constexpr uint32 scMinSize = 3;
	static constexpr uint32 scMaxSize = 31;

	static constexpr uint32 scMinDetailMeshSize = 2;
	static constexpr uint32 scMaxDetailMeshSize = 33;

	static constexpr uint32 scMinSubdivisions = 1;
	static constexpr uint32 scMaxSubdivisions = 16;
	static constexpr uint32 scDefaultSubdivisions = 4;

	static constexpr float32 scDefaultTextureScale = 2.0f;

	static constexpr float32 scMinWeight = 1e-3f;
	static constexpr float32 scQuarterArcWeight = 0.70710678f;

	static constexpr uint32 scEdgeCount = 4;
	static constexpr uint32 scMaxThickenPieces = scEdgeCount + 1;
	static constexpr uint32 scMaxCapPieces = scEdgeCount * ((scMaxSize - 1) / 2);
	static constexpr uint32 scMaxClosurePieces = scMaxCapPieces;

	static constexpr float32 scDefaultThickness = 0.25f;

	static constexpr uint32 scAutoTextureAxis = 3;

public:
	static Patch FromGrid(uint32 width, uint32 height);
	static Patch FromDetailMesh(uint32 width, uint32 height);

	static Patch MakeFlat(const Vec3f min, const Vec3f max, uint32 axis);
	static Patch MakeCylinder(const Vec3f min, const Vec3f max, uint32 axis);
	static Patch MakeCone(const Vec3f min, const Vec3f max, uint32 axis);
	static Patch MakeBevel(const Vec3f min, const Vec3f max, uint32 axis);
	static Patch MakeEndcap(const Vec3f min, const Vec3f max, uint32 axis);
	static Patch MakeDetailMesh(const Vec3f min, const Vec3f max, uint32 axis, uint32 cells_u = scDefaultSubdivisions,
								uint32 cells_v = scDefaultSubdivisions);
	static Patch MakePrimitive(ePatchPrimitive primitive, const Vec3f min, const Vec3f max, uint32 axis);

	static bool IsValidSize(uint32 size) { return size >= scMinSize && size <= scMaxSize && (size & 1) == 1; }
	static bool IsValidDetailMeshSize(uint32 size)
	{
		return size >= scMinDetailMeshSize && size <= scMaxDetailMeshSize;
	}

	Patch Clone() const;

	bool IsValid() const;

	uint32 GetSubPatchColumns() const
	{
		if (bIsDetailMesh) {
			return Width - 1;
		}

		return (Width - 1) / 2;
	}

	uint32 GetSubPatchRows() const
	{
		if (bIsDetailMesh) {
			return Height - 1;
		}

		return (Height - 1) / 2;
	}

	PatchPoint& GetPoint(uint32 column, uint32 row) { return Points[row * Width + column]; }
	const PatchPoint& GetPoint(uint32 column, uint32 row) const { return Points[row * Width + column]; }

	Vec3f GetBoundsMin() const;
	Vec3f GetBoundsMax() const;
	Vec3f GetCenter() const { return (GetBoundsMin() + GetBoundsMax()) * 0.5f; }

	bool IsCellTurned(uint32 column, uint32 row) const;
	void TurnCell(uint32 column, uint32 row);
	bool ResampleDetailMesh(uint32 cells_u, uint32 cells_v);

	PatchVertex Evaluate(double u, double v) const;

	void Tessellate(PatchMesh& out_mesh) const;

	void GenerateMesh(SizedArray<Vec3f>& positions, SizedArray<Vec3f>& normals, SizedArray<Vec3f>& tangents,
					  SizedArray<float32>& tangent_handedness, SizedArray<Vec2f>& texcoords,
					  SizedArray<uint32>& indices, const Mat4f& to_world = Mat4f::scIdentity,
					  SizedArray<uint32>* packed_colors = nullptr) const;

	bool HasVertexColors() const;

	void Invert();
	void Transpose();

	bool InsertColumns(uint32 sub_column);
	bool InsertRows(uint32 sub_row);
	bool RemoveColumns(uint32 sub_column);
	bool RemoveRows(uint32 sub_row);

	void NaturalTexture(float32 scale = scDefaultTextureScale);
	void FitTexture(float32 repeats_u, float32 repeats_v);
	void WorldTexture(const Mat4f& to_world = Mat4f::scIdentity, float32 scale = scDefaultTextureScale,
					  uint32 axis = scAutoTextureAxis);

	void GetEdgePoints(uint32 edge, SizedArray<PatchPoint>& out_points) const;
	bool IsEdgeClosed(uint32 edge) const;
	bool IsEdgeRing(uint32 edge) const;
	Vec3d GetEdgeOutward(uint32 edge) const;

	Patch MakeOffset(float32 distance) const;
	Patch MakeCap(uint32 edge, const Mat4f& to_world = Mat4f::scIdentity) const;

	uint32 Thicken(float32 distance, Patch out_pieces[scMaxThickenPieces]) const;
	uint32 Cap(Patch out_caps[scMaxCapPieces], const Mat4f& to_world = Mat4f::scIdentity) const;

	uint32 BuildClosure(Patch out_pieces[scMaxClosurePieces], const Mat4f& to_world = Mat4f::scIdentity) const;

public:
	uint32 Width = 0;
	uint32 Height = 0;

	SizedArray<PatchPoint> Points;

	bool bIsDetailMesh = false;
	SizedArray<uint8> TurnedCells;

	uint32 SubdivisionsU = scDefaultSubdivisions;
	uint32 SubdivisionsV = scDefaultSubdivisions;

	bool bSmooth = true;
	uint32 SmoothingGroup = 0;

	ePatchClosure Closure = ePatchClosure::None;
	float32 Thickness = scDefaultThickness;
};

} // namespace fx
