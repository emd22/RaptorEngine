#pragma once

#ifdef FX_IS_EDITOR

#include <Core/Types.hpp>

namespace fx::editor {

enum class eViewPlane : uint8
{
	Top,
	Front,
	Side,

	Count,
};

static constexpr uint32 scViewPlaneCount = static_cast<uint32>(eViewPlane::Count);

struct ViewPlaneAxes
{
	uint32 U;
	uint32 V;
	uint32 W;

	float32 DepthSign;
	float32 RotateSign;

	const char* pName;
	const char* pBaseLabel;
	const char* pSizeLabel;
};

inline constexpr ViewPlaneAxes scViewPlaneAxes[scViewPlaneCount] = {
	{ 0, 2, 1, 1.0f, -1.0f, "T", "Base Y", "Height" },
	{ 0, 1, 2, -1.0f, 1.0f, "F", "Base Z", "Depth" },
	{ 2, 1, 0, 1.0f, -1.0f, "S", "Base X", "Width" },
};

struct TopViewVertexHandle
{
	uint32 ObjectId = 0;
	float32 U = 0.0f;
	float32 V = 0.0f;
};

inline constexpr const char* scAxisNames = "XYZ";

inline const ViewPlaneAxes& GetViewPlaneAxes(eViewPlane plane) { return scViewPlaneAxes[static_cast<uint32>(plane)]; }

} // namespace fx::editor

#endif
