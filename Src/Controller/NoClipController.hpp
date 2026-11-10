#pragma once

#include <Controller/BaseController.hpp>

namespace fx {

class PerspectiveCamera;

class NoClipController : public BaseController
{
public:
	NoClipController();

	FVec3 Update(double delta_time, const FVec3 input, const PerspectiveCamera& camera, bool sprinting) override;
};

} // namespace fx
