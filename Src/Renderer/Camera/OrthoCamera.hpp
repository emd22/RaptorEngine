#pragma once

#include "BaseCamera.hpp"

#include <Math/Vec2.hpp>

namespace fx {

class OrthoCamera final : public BaseCamera
{
public:
	OrthoCamera() = default;

	bool Update() override;

	FX_FORCE_INLINE void SetBounds(const FVec2 size)
	{
		mSize = size;
		mbRequireProjectionMatrixUpdate = true;
	}

	void ResolveViewToTexels(FVec3& eye, FVec3& target, const FVec3 world_up, float32 texture_res) const;

	~OrthoCamera() override = default;

public:
	FVec2 mSize = FVec2(45.0f);
};


} // namespace fx
