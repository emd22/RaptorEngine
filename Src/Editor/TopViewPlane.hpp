#pragma once

#ifdef FX_IS_EDITOR

#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>

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
	uint32 ObjectId = 0;
	Vec2f Position = Vec2f::sZero;
};

inline constexpr const char* scAxisNames = "XYZ";

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
