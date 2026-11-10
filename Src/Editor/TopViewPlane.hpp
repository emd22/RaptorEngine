#pragma once

#ifdef FX_IS_EDITOR

#include <Core/Types.hpp>
#include <Math/FVec3.hpp>
#include <Math/Vec2.hpp>
#include <Object/ObjectID.hpp>
#include <Patch.hpp>
#include <algorithm>
#include <cmath>

namespace fx::editor {

enum class eViewPlane : uint8
{
	Top,
	Front,
	Back,
	Left,
	Right,

	Count,
};

static constexpr uint32 scViewPlaneCount = static_cast<uint32>(eViewPlane::Count);

struct ViewPlaneAxes
{
	uint32 U;
	uint32 V;
	uint32 W;

	float32 USign;
	float32 DepthSign;
	float32 RotateSign;

	const char* pName;
	const char* pBaseLabel;
	const char* pSizeLabel;
};

inline constexpr ViewPlaneAxes scViewPlaneAxes[scViewPlaneCount] = {
	{ 0, 2, 1, 1.0f, 1.0f, -1.0f, "T", "Base Y", "Height" }, { 0, 1, 2, 1.0f, -1.0f, 1.0f, "F", "Base Z", "Depth" },
	{ 0, 1, 2, -1.0f, 1.0f, -1.0f, "B", "Base Z", "Depth" }, { 2, 1, 0, -1.0f, -1.0f, 1.0f, "L", "Base X", "Width" },
	{ 2, 1, 0, 1.0f, 1.0f, -1.0f, "R", "Base X", "Width" },
};

struct TopViewVertexHandle
{
	ObjectID ObjectId = ObjectID::scNull;
	FVec2 Position = FVec2::scZero;
};

struct TopViewFaceRef
{
	ObjectID ObjectId = ObjectID::scNull;
	FVec3 Normal = FVec3::sZero;

	bool operator==(const TopViewFaceRef& other) const { return ObjectId == other.ObjectId && Normal == other.Normal; }
};

inline constexpr const char* scAxisNames = "XYZ";

inline constexpr uint32 scArchPointsPerSegment = 8;

enum class ePatchCommand : uint8
{
	Invert,
	Transpose,
	InsertColumns,
	InsertRows,
	RemoveColumns,
	RemoveRows,
	MapTexture,
	SetSubdivisions,
	SetSmooth,
	SetClosure,
	SetWeight,
	RoundWeights,
	TurnCells,
	SetColor,
	SetAlpha,
};

enum class ePatchTextureMapping : uint8
{
	Natural,
	World,
	Fit,

	Count,
};

enum class ePatchTextureAxis : uint8
{
	Auto,
	X,
	Y,
	Z,

	Count,
};

struct PatchTextureSettings
{
	ePatchTextureMapping Mapping = ePatchTextureMapping::Natural;
	ePatchTextureAxis Axis = ePatchTextureAxis::Auto;
	float32 Scale = Patch::scDefaultTextureScale;
	FVec2 Repeats = FVec2(1.0f, 1.0f);
};

struct PatchCommand
{
	ePatchCommand Type = ePatchCommand::Invert;
	uint32 SubdivisionsU = 4;
	uint32 SubdivisionsV = 4;
	bool bSmooth = true;
	ePatchClosure Closure = ePatchClosure::None;
	float32 Thickness = Patch::scDefaultThickness;
	float32 Weight = 1.0f;
	PatchColor Color;
	PatchTextureSettings Texture;
};

enum class eTextureCommand : uint8
{
	Shift,
	SetOffsetU,
	SetOffsetV,
	Scale,
	SetScale,
	Rotate,
	SetRotation,
	Reset,
	WorldAlign,
};

struct TextureCommand
{
	eTextureCommand Type = eTextureCommand::Shift;
	FVec2 Amount = FVec2::scZero;
};

enum class eRoomShape : uint8
{
	Room,
	Tunnel,

	Count,
};

struct RoomSettings
{
	eRoomShape Shape = eRoomShape::Room;
	float32 WallThickness = 0.25f;
	bool bCeiling = true;
};

inline constexpr uint32 scMaxRoomPieces = 6;

struct RoomPiece
{
	FVec3 Min = FVec3::sZero;
	FVec3 Max = FVec3::sZero;
};

inline uint32 BuildRoomPieces(const FVec3 min, const FVec3 max, const RoomSettings& settings,
							  RoomPiece (&out_pieces)[scMaxRoomPieces])
{
	const float32 wall = settings.WallThickness;
	const FVec3 extent = max - min;

	const bool is_tunnel = (settings.Shape == eRoomShape::Tunnel);
	const bool open_x = is_tunnel && extent.X >= extent.Z;
	const bool open_z = is_tunnel && !open_x;

	float32 vertical_walls = 1.0f;
	if (settings.bCeiling) {
		vertical_walls = 2.0f;
	}

	if (!(wall > 0.0f) || extent.Y <= wall * vertical_walls || (!open_x && extent.X <= wall * 2.0f) ||
		(!open_z && extent.Z <= wall * 2.0f)) {
		return 0;
	}

	float32 wall_inset_x = wall;
	if (open_x) {
		wall_inset_x = 0.0f;
	}

	float32 wall_inset_z = wall;
	if (open_z) {
		wall_inset_z = 0.0f;
	}

	float32 ceiling_inset = 0.0f;
	if (settings.bCeiling) {
		ceiling_inset = wall;
	}

	const FVec3 inner_min = min + FVec3(wall_inset_x, wall, wall_inset_z);
	const FVec3 inner_max = max - FVec3(wall_inset_x, ceiling_inset, wall_inset_z);

	uint32 count = 0;

	out_pieces[count++] = RoomPiece { min, FVec3(max.X, inner_min.Y, max.Z) };

	if (settings.bCeiling) {
		out_pieces[count++] = RoomPiece { FVec3(min.X, inner_max.Y, min.Z), max };
	}

	if (!open_x) {
		out_pieces[count++] = RoomPiece { FVec3(min.X, inner_min.Y, min.Z), FVec3(inner_min.X, inner_max.Y, max.Z) };
		out_pieces[count++] = RoomPiece { FVec3(inner_max.X, inner_min.Y, min.Z), FVec3(max.X, inner_max.Y, max.Z) };
	}

	if (!open_z) {
		out_pieces[count++] = RoomPiece { FVec3(inner_min.X, inner_min.Y, min.Z),
										  FVec3(inner_max.X, inner_max.Y, inner_min.Z) };
		out_pieces[count++] = RoomPiece { FVec3(inner_min.X, inner_min.Y, inner_max.Z),
										  FVec3(inner_max.X, inner_max.Y, max.Z) };
	}

	return count;
}

inline const ViewPlaneAxes& GetViewPlaneAxes(eViewPlane plane) { return scViewPlaneAxes[static_cast<uint32>(plane)]; }

inline float32 GetAxis(const FVec3& vector, uint32 axis)
{
	if (axis == 0) {
		return vector.X;
	}

	if (axis == 1) {
		return vector.Y;
	}

	return vector.Z;
}

inline void SetAxis(FVec3& vector, uint32 axis, float32 value)
{
	if (axis == 0) {
		vector.X = value;
	}
	else if (axis == 1) {
		vector.Y = value;
	}
	else {
		vector.Z = value;
	}
}

} // namespace fx::editor

#endif
