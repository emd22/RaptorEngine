#pragma once

#ifdef FX_IS_EDITOR

#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>
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
	Vec2f Position = Vec2f::sZero;
};

inline constexpr const char* scAxisNames = "XYZ";

inline constexpr uint32 scArchPointsPerSegment = 8;

inline constexpr uint32 scMaxBendSegments = 128;
inline constexpr double scMinBendAngle = 1e-4;
inline constexpr double scMaxBendSegmentAngle = 3.1241;
inline constexpr double scBendRegionEpsilon = 1e-4;

struct BendFrame
{
	Vec3f Along = Vec3f::sZero;
	Vec3f Across = Vec3f::sZero;
	float32 Start = 0.0f;
	float32 Length = 0.0f;
	float32 Middle = 0.0f;
	float32 CurveStart = 0.0f;
	float32 CurveEnd = 0.0f;
	float32 Angle = 0.0f;
	uint32 Segments = 1;
	bool bPinEnd = false;
};

struct BendSlice
{
	double Low = 0.0;
	double High = 0.0;
	bool bHasLow = false;
	bool bHasHigh = false;
};

inline bool IsBendStraight(const BendFrame& frame)
{
	return std::fabs(frame.Angle) < scMinBendAngle || frame.Length <= 0.0f || frame.CurveEnd <= frame.CurveStart;
}

inline Vec2d BendCoordinates(const BendFrame& frame, double along, double across)
{
	if (IsBendStraight(frame)) {
		return Vec2d(along, across);
	}

	double pivot = frame.CurveStart;

	if (frame.bPinEnd) {
		pivot = frame.CurveEnd;
	}

	const double radius = (frame.CurveEnd - frame.CurveStart) / frame.Angle;
	const double curve_along = std::clamp<double>(along, frame.CurveStart, frame.CurveEnd);
	const double excess = along - curve_along;
	const double angle = (curve_along - pivot) / radius;
	const double sin_angle = std::sin(angle);
	const double cos_angle = std::cos(angle);

	double distance = radius - (across - frame.Middle);

	if (radius > 0.0) {
		distance = std::max(distance, 0.0);
	}
	else {
		distance = std::min(distance, 0.0);
	}

	return Vec2d(pivot + distance * sin_angle + excess * cos_angle,
				 frame.Middle + radius - distance * cos_angle + excess * sin_angle);
}

inline bool HasBendLead(const BendFrame& frame) { return frame.CurveStart > frame.Start + scBendRegionEpsilon; }

inline bool HasBendTail(const BendFrame& frame)
{
	return frame.CurveEnd < frame.Start + frame.Length - scBendRegionEpsilon;
}

inline uint32 GetBendSliceCount(const BendFrame& frame)
{
	uint32 count = std::max<uint32>(frame.Segments, 1);

	if (HasBendLead(frame)) {
		count++;
	}

	if (HasBendTail(frame)) {
		count++;
	}

	return count;
}

inline double GetBendCut(const BendFrame& frame, uint32 cut)
{
	if (HasBendLead(frame)) {
		if (cut == 0) {
			return frame.CurveStart;
		}

		cut--;
	}

	const uint32 segments = std::max<uint32>(frame.Segments, 1);

	if (cut + 1 < segments) {
		return frame.CurveStart + (static_cast<double>(frame.CurveEnd) - frame.CurveStart) * (cut + 1) / segments;
	}

	return frame.CurveEnd;
}

inline BendSlice GetBendSlice(const BendFrame& frame, uint32 slice)
{
	BendSlice out;

	if (slice > 0) {
		out.bHasLow = true;
		out.Low = GetBendCut(frame, slice - 1);
	}

	if (slice + 1 < GetBendSliceCount(frame)) {
		out.bHasHigh = true;
		out.High = GetBendCut(frame, slice);
	}

	return out;
}

inline bool IsUnbentByBend(const BendFrame& frame, double low, double high)
{
	if (frame.bPinEnd) {
		return low >= frame.CurveEnd - scBendRegionEpsilon;
	}

	return high <= frame.CurveStart + scBendRegionEpsilon;
}

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
