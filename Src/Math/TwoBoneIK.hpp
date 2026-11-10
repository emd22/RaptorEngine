#pragma once

#include <Math/FVec3.hpp>
#include <algorithm>
#include <cmath>

namespace fx {

struct TwoBoneIKResult
{
	FVec3 Mid = FVec3::sZero;
	FVec3 End = FVec3::sZero;
	bool bReached = false;
};

inline FVec3 GetAnyPerpendicular(const FVec3 direction)
{
	FVec3 axis = FVec3::sRight;

	if (std::abs(direction.Dot(axis)) > 0.9f) {
		axis = FVec3::sUp;
	}

	return direction.Cross(axis).Normalize();
}

inline TwoBoneIKResult SolveTwoBoneIK(const FVec3 root, const FVec3 target, float32 upper_length, float32 lower_length,
									  const FVec3 bend_hint)
{
	constexpr float32 scEpsilon = 1e-4f;

	TwoBoneIKResult result;

	const FVec3 to_target = target - root;
	const float32 distance = to_target.Length();

	FVec3 direction;

	if (distance > scEpsilon) {
		direction = to_target / distance;
	}
	else {
		direction = GetAnyPerpendicular(bend_hint);
	}

	const float32 max_reach = upper_length + lower_length;
	const float32 min_reach = std::abs(upper_length - lower_length);
	const float32 reach = std::clamp(distance, min_reach + scEpsilon, std::max(max_reach - scEpsilon, scEpsilon));

	FVec3 bend = bend_hint - direction * bend_hint.Dot(direction);
	const float32 bend_length = bend.Length();

	if (bend_length > scEpsilon) {
		bend = bend / bend_length;
	}
	else {
		bend = GetAnyPerpendicular(direction);
	}

	const float32 cos_upper = std::clamp((upper_length * upper_length + reach * reach - lower_length * lower_length) /
											 (2.0f * upper_length * reach),
										 -1.0f, 1.0f);
	const float32 sin_upper = std::sqrt(std::max(1.0f - cos_upper * cos_upper, 0.0f));

	result.Mid = root + direction * (upper_length * cos_upper) + bend * (upper_length * sin_upper);
	result.End = root + direction * reach;
	result.bReached = (distance <= max_reach) && (distance >= min_reach);

	return result;
}

} // namespace fx
