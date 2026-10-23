#pragma once

#ifdef FX_IS_EDITOR

#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
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
	Vec2f Position = Vec2f::scZero;
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
	Vec2f Repeats = Vec2f(1.0f, 1.0f);
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
	Vec2f Amount = Vec2f::scZero;
};

inline const ViewPlaneAxes& GetViewPlaneAxes(eViewPlane plane) { return scViewPlaneAxes[static_cast<uint32>(plane)]; }

inline float32 GetAxis(const Vec3f& vector, uint32 axis)
{
	if (axis == 0) {
		return vector.X;
	}

	if (axis == 1) {
		return vector.Y;
	}

	return vector.Z;
}

} // namespace fx::editor

#endif
