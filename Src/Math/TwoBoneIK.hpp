#pragma once

#include <Math/Vec3.hpp>

#include <algorithm>
#include <cmath>

namespace fx {

struct TwoBoneIKResult
{
	Vec3f Mid = Vec3f::sZero;
	Vec3f End = Vec3f::sZero;
	bool bReached = false;
};

inline Vec3f GetAnyPerpendicular(const Vec3f direction)
{
	Vec3f axis = Vec3f::sRight;

	if (std::abs(direction.Dot(axis)) > 0.9f) {
		axis = Vec3f::sUp;
	}

	return direction.Cross(axis).Normalize();
}

inline TwoBoneIKResult SolveTwoBoneIK(const Vec3f root, const Vec3f target, float32 upper_length,
									  float32 lower_length, const Vec3f bend_hint)
{
	constexpr float32 scEpsilon = 1e-4f;

	TwoBoneIKResult result;

	const Vec3f to_target = target - root;
	const float32 distance = to_target.Length();

	Vec3f direction;

	if (distance > scEpsilon) {
		direction = to_target / distance;
	}
	else {
		direction = GetAnyPerpendicular(bend_hint);
	}

	const float32 max_reach = upper_length + lower_length;
	const float32 min_reach = std::abs(upper_length - lower_length);
	const float32 reach = std::clamp(distance, min_reach + scEpsilon, std::max(max_reach - scEpsilon, scEpsilon));

	Vec3f bend = bend_hint - direction * bend_hint.Dot(direction);
	const float32 bend_length = bend.Length();

	if (bend_length > scEpsilon) {
		bend = bend / bend_length;
	}
	else {
		bend = GetAnyPerpendicular(direction);
	}

	const float32 cos_upper = std::clamp(
		(upper_length * upper_length + reach * reach - lower_length * lower_length) / (2.0f * upper_length * reach),
		-1.0f, 1.0f);
	const float32 sin_upper = std::sqrt(std::max(1.0f - cos_upper * cos_upper, 0.0f));

	result.Mid = root + direction * (upper_length * cos_upper) + bend * (upper_length * sin_upper);
	result.End = root + direction * reach;
	result.bReached = (distance <= max_reach) && (distance >= min_reach);

	return result;
}

} // namespace fx
